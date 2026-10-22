// Native Linux entry point. Never launches input files or a Windows program.
#include "../microvm/boot/KernelImage.h"
#include "KvmMachine.h"
int GuardianKvmCli(int,char**);
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace fs=std::filesystem;
constexpr size_t ContentLimit=4*1024*1024;
constexpr size_t MaxRules=10000,MaxRuleBytes=4096;
constexpr uint32_t MaxDirectoryDepth=128;
struct File {
    int fd=-1;
    explicit File(const fs::path& path):fd(::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)){
        if(fd<0)throw std::system_error(errno,std::generic_category(),"open");
    }
    ~File(){if(fd>=0)::close(fd);}
    File(const File&)=delete;File& operator=(const File&)=delete;
};
struct ReadResult {std::string sha256,content;uint64_t bytes=0;};
ReadResult ReadFile(const fs::path& path,uint64_t maxBytes=UINT64_MAX){
    File file(path);struct stat before{},after{};
    if(::fstat(file.fd,&before))throw std::system_error(errno,std::generic_category(),"fstat");
    if(!S_ISREG(before.st_mode) || before.st_size<0)throw std::runtime_error("regular file required");
    const auto size=static_cast<uint64_t>(before.st_size);
    if(size>maxBytes)throw std::runtime_error("file exceeds the configured size limit");
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    if(!hash || EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)!=1)throw std::runtime_error("SHA-256 initialization failed");
    ReadResult result;result.content.reserve(static_cast<size_t>(std::min<uint64_t>(size,ContentLimit)));
    std::array<char,65536> buffer{};
    while(result.bytes<size){
        const auto want=static_cast<size_t>(std::min<uint64_t>(buffer.size(),size-result.bytes));
        const auto n=::read(file.fd,buffer.data(),want);
        if(n<0){if(errno==EINTR)continue;throw std::system_error(errno,std::generic_category(),"read");}
        if(!n)throw std::runtime_error("file shortened during reading");
        const auto count=static_cast<size_t>(n);
        if(EVP_DigestUpdate(hash.get(),buffer.data(),count)!=1)throw std::runtime_error("SHA-256 update failed");
        result.content.append(buffer.data(),std::min(count,ContentLimit-result.content.size()));
        result.bytes+=count;
    }
    // A regular virtual file can report a size of zero yet contain data. Do
    // not silently hash it as empty or consume an indefinitely growing input.
    ssize_t extra=0;do {extra=::read(file.fd,buffer.data(),1);}while(extra<0 && errno==EINTR);
    if(extra<0)throw std::system_error(errno,std::generic_category(),"read at end");
    if(extra)throw std::runtime_error("file grew or has an unsupported virtual extent");
    if(::fstat(file.fd,&after))throw std::system_error(errno,std::generic_category(),"final fstat");
    if(before.st_size!=after.st_size || before.st_mtim.tv_sec!=after.st_mtim.tv_sec ||
       before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec || before.st_ctim.tv_sec!=after.st_ctim.tv_sec ||
       before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)throw std::runtime_error("file changed during reading");
    std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned length=0;
    if(EVP_DigestFinal_ex(hash.get(),digest.data(),&length)!=1 || length!=32)throw std::runtime_error("SHA-256 finalization failed");
    static constexpr char hex[]="0123456789abcdef";result.sha256.reserve(64);
    for(unsigned i=0;i<length;++i){result.sha256+=hex[digest[i]>>4];result.sha256+=hex[digest[i]&15];}
    return result;
}

// Keep valid UTF-8 intact; quote control characters and escape invalid bytes.
// A filename cannot inject terminal controls or break a JSON report.
std::string Json(std::string_view text){
    static constexpr char hex[]="0123456789abcdef";std::string out="\"";
    const auto escape=[&](unsigned char c){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];};
    for(size_t i=0;i<text.size();){const auto c=static_cast<unsigned char>(text[i]);
        if(c=='"' || c=='\\'){out+='\\';out+=static_cast<char>(c);++i;continue;}
        if(c<32 || c==127){escape(c);++i;continue;}
        if(c<128){out+=static_cast<char>(c);++i;continue;}
        size_t n=0;uint32_t cp=0,min=0;
        if(c>=0xC2 && c<=0xDF){n=2;cp=c&31;min=0x80;}
        else if(c>=0xE0 && c<=0xEF){n=3;cp=c&15;min=0x800;}
        else if(c>=0xF0 && c<=0xF4){n=4;cp=c&7;min=0x10000;}
        bool valid=n && n<=text.size()-i;
        for(size_t k=1;valid && k<n;++k){const auto b=static_cast<unsigned char>(text[i+k]);valid=(b&0xC0)==0x80;cp=(cp<<6)|(b&63);}
        valid=valid && cp>=min && cp<=0x10FFFF && !(cp>=0xD800 && cp<=0xDFFF);
        if(valid){out.append(text.substr(i,n));i+=n;}else {escape(c);++i;}
    }
    return out+'"';
}
std::string Trim(std::string_view text){
    const auto a=text.find_first_not_of(" \t\r\n");if(a==std::string_view::npos)return {};
    return std::string(text.substr(a,text.find_last_not_of(" \t\r\n")-a+1));
}
struct Rule {bool hash;std::string value,name;};
std::vector<Rule> LoadRules(const fs::path& path){
    const auto data=ReadFile(path,ContentLimit);std::vector<Rule> rules;size_t start=0;uint32_t number=0;
    while(start<data.content.size()){
        const auto end=data.content.find('\n',start);const auto line=Trim(std::string_view(data.content).substr(start,end==std::string::npos?end:end-start));
        start=end==std::string::npos?data.content.size():end+1;++number;
        if(line.empty() || line[0]=='#')continue;
        const auto fail=[&](){throw std::runtime_error("invalid signature at line "+std::to_string(number));};
        if(line.size()>MaxRuleBytes || rules.size()>=MaxRules)fail();
        const auto first=line.find(':'),second=first==std::string::npos?first:line.find(':',first+1);
        if(second==std::string::npos)fail();
        Rule rule{line.substr(0,first)=="sha256",line.substr(first+1,second-first-1),line.substr(second+1)};
        if(line.substr(0,first)!="sha256" && line.substr(0,first)!="substr")fail();
        if(rule.value.empty() || rule.name.empty() || line.find('\0')!=std::string::npos)fail();
        if(std::any_of(rule.name.begin(),rule.name.end(),[](unsigned char c){return c<32 || c==127;}))fail();
        if(rule.hash){if(rule.value.size()!=64)fail();for(auto& c:rule.value){
            if(c>='A' && c<='F')c=static_cast<char>(c+32);
            if(!((c>='0' && c<='9') || (c>='a' && c<='f')))fail();}}
        rules.push_back(std::move(rule));
    }
    return rules;
}
struct Row {std::string path,status,sha256,threat,reason;bool partial=false;};
void PrintRow(const Row& row,bool json){
    if(json){std::cout<<"{\"path\":"<<Json(row.path)<<",\"status\":"<<Json(row.status)<<",\"sha256\":"<<Json(row.sha256)
        <<",\"threat\":"<<Json(row.threat)<<",\"reason\":"<<Json(row.reason)<<",\"partial\":"<<(row.partial?"true":"false")<<'}';}
    else {std::cout<<'['<<row.status<<"] "<<Json(row.path);if(!row.threat.empty())std::cout<<" "<<Json(row.threat);
        if(!row.reason.empty())std::cout<<" ("<<Json(row.reason)<<')';std::cout<<'\n';}
}
struct Report {
    uint64_t files=0,threats=0,errors=0,skipped=0,partial=0;
    bool json=false,first=true;
    explicit Report(bool useJson):json(useJson){if(json)std::cout<<"{\"platform\":\"linux\",\"results\":[";}
    void Add(Row row){
        if(row.status=="error")++errors;else if(row.status=="skipped")++skipped;else ++files;
        if(!row.threat.empty())++threats;if(row.partial)++partial;
        // Stream rows so walking a large tree does not retain all file paths.
        if(json && !first)std::cout<<',';first=false;PrintRow(row,json);
    }
    void Finish() const {
        if(json)std::cout<<"],\"filesScanned\":"<<files<<",\"threatsFound\":"<<threats
            <<",\"errors\":"<<errors<<",\"skipped\":"<<skipped<<",\"partial\":"<<partial<<"}\n";
        else std::cout<<"Arquivos: "<<files<<" | Correspondências: "<<threats<<" | Erros: "<<errors
            <<" | Ignorados: "<<skipped<<" | Parciais: "<<partial<<'\n';
        if(!std::cout)throw std::runtime_error("report output could not be written");
    }
};
void ScanOne(const fs::path& path,const std::vector<Rule>& rules,Report& report){
    Row row{path.string(),"no_match",{},{},{},false};
    try {
        const auto file=ReadFile(path);row.sha256=file.sha256;
        for(const auto& rule:rules)if(rule.hash && rule.value==file.sha256){row.threat=rule.name;row.reason="sha256 match";break;}
        if(row.threat.empty()){
            const auto eicar=std::string("EICAR-STANDARD-")+"ANTIVIRUS-"+"TEST-FILE";
            if(file.content.find(eicar)!=std::string::npos){row.threat="EICAR-Test-File";row.reason="content signature (EICAR)";}
            else for(const auto& rule:rules)if(!rule.hash && file.content.find(rule.value)!=std::string::npos){row.threat=rule.name;row.reason="content signature";break;}
        }
        if(!row.threat.empty())row.status="matched";
        // A full-file hash match establishes this detection. Otherwise the
        // bounded content check cannot describe the rest of a large file.
        if(file.bytes>ContentLimit && row.reason!="sha256 match"){
            row.partial=true;if(row.threat.empty())row.status="partial";
            if(row.reason.empty())row.reason="content scanned only through 4 MiB; entire file hashed";
        }
    }catch(const std::exception& e){row.status="error";row.reason=e.what();}
    report.Add(std::move(row));
}
void ScanPath(const fs::path& path,const std::vector<Rule>& rules,Report& report){
    std::error_code ec;const auto status=fs::symlink_status(path,ec);
    if(ec){report.Add({path.string(),"error",{},{},ec.message()});return;}
    if(fs::is_regular_file(status)){ScanOne(path,rules,report);return;}
    if(!fs::is_directory(status)){report.Add({path.string(),"skipped",{},{},"symlink or special file; not followed"});return;}
    fs::recursive_directory_iterator it(path,fs::directory_options::none,ec),end;
    if(ec){report.Add({path.string(),"error",{},{},ec.message()});return;}
    while(it!=end){const auto current=it->path();const auto type=it->symlink_status(ec);
        if(ec)report.Add({current.string(),"error",{},{},ec.message()});
        else if(fs::is_regular_file(type))ScanOne(current,rules,report);
        else if(fs::is_directory(type)){
            if(static_cast<uint32_t>(it.depth())>=MaxDirectoryDepth){it.disable_recursion_pending();report.Add({current.string(),"skipped",{},{},"directory depth limit"});}
        }else {it.disable_recursion_pending();report.Add({current.string(),"skipped",{},{},"symlink or special file; not followed"});}
        it.increment(ec);if(ec){report.Add({path.string(),"error",{},{},"directory traversal: "+ec.message()});break;}
    }
}
void Help(){
    std::cout<<"GuardianAV - ferramentas nativas Linux\n"
        "Uso:\n  guardianav scan <arquivo-ou-pasta> [...] [--signatures arquivo] [--json]\n"
        "  guardianav hash <arquivo> [--json]\n  guardianav inspect-kernel <imagem.gk> [--json]\n"
        "  guardianav boot-kernel <imagem.gk> <contrato> <relatorio.json> [opções]\n"
        "  guardianav --capabilities\n  guardianav --version\n"
        "O scanner somente lê arquivos. Correspondências são limitadas às assinaturas.\n"
        "Saída: 0=sem correspondências, 1=detecção, 2=uso/base inválidos, 3=leitura ou cobertura incompleta.\n"
        "A VM usa KVM; exige NX e acesso a /dev/kvm. A interface Windows depende do Windows.\n";
}
fs::path DefaultRules(const char* executable){
    std::error_code ec;auto binary=fs::read_symlink("/proc/self/exe",ec);
    if(ec)binary=fs::absolute(executable);
    const auto sibling=binary.parent_path()/"signatures.txt";
    if(fs::exists(sibling))return sibling;
    return binary.parent_path()/".."/"share"/"guardianav"/"signatures.txt";
}
int Run(int argc,char** argv){
    if(argc<2 || std::string_view(argv[1])=="--help"){Help();return 0;}
    const std::string command=argv[1];
    if(command=="boot-kernel")return GuardianKvmCli(argc-1,argv+1);
    if(command=="--version"){if(argc!=2)throw std::invalid_argument("unexpected arguments");std::cout<<"GuardianAV 1.0.0 Linux native x86-64\n";return 0;}
    if(command=="--capabilities"){
        if(argc!=2)throw std::invalid_argument("unexpected arguments");
        std::cout<<"{\"platform\":\"linux\",\"native\":true,\"wine\":false,\"scanner\":true,\"kernelImageInspection\":true,"
            "\"vmExecution\":true,\"vmBackend\":\"KVM\",\"vmAvailable\":"<<(av::microvm::MicroVm::Available()?"true":"false")<<",\"secondStageExecuteProtection\":false,\"windowsUI\":false,\"windowsDifferentialTests\":false,\"quarantine\":false}\n";return 0;
    }
    if(command!="scan" && command!="hash" && command!="inspect-kernel")throw std::invalid_argument("unsupported command: "+command);
    bool json=false,options=true;fs::path database=DefaultRules(argv[0]);std::vector<fs::path> paths;
    for(int i=2;i<argc;++i){const std::string arg=argv[i];
        if(options && arg=="--"){options=false;continue;}
        if(options && arg=="--json"){json=true;continue;}
        if(options && arg=="--signatures"){
            if(command!="scan" || ++i==argc)throw std::invalid_argument("--signatures requires a file and the scan command");
            database=argv[i];continue;
        }
        if(options && arg.starts_with("-"))throw std::invalid_argument("unknown option: "+arg);
        paths.emplace_back(arg);
    }
    if(paths.empty() || (command!="scan" && paths.size()!=1))throw std::invalid_argument("file or directory argument required");
    if(command=="hash"){
        try {const auto file=ReadFile(paths[0]);if(json)std::cout<<"{\"path\":"<<Json(paths[0].string())<<",\"sha256\":"<<Json(file.sha256)<<",\"bytes\":"<<file.bytes<<"}\n";
            else std::cout<<file.sha256<<"  "<<Json(paths[0].string())<<'\n';return 0;
        }catch(const std::exception& e){std::cerr<<"Falha de leitura: "<<e.what()<<'\n';return 3;}
    }
    if(command=="inspect-kernel"){
        try {const auto image=av::microvm::boot::KernelImage::Read(paths[0]);
            std::cout<<"{\"valid\":true,\"executed\":false,\"base\":"<<image.Base()<<",\"bytes\":"<<image.Size()<<",\"entry\":"<<image.Entry()<<",\"sections\":"<<image.Sections().size()<<"}\n";return 0;
        }catch(const std::exception& e){std::cerr<<"Imagem recusada: "<<e.what()<<'\n';return 3;}
    }
    const auto rules=LoadRules(database);Report report(json);
    for(const auto& path:paths)ScanPath(path,rules,report);
    report.Finish();
    return report.errors || report.skipped || report.partial?3:(report.threats?1:0);
}
}
int main(int argc,char** argv){
    try {return Run(argc,argv);}catch(const std::exception& e){std::cerr<<"Erro: "<<e.what()<<'\n';return 2;}
}
