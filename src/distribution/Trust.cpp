#include "Distribution.h"
#include "TrustKey.h"
#include <bcrypt.h>
#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
namespace aegis::distribution {
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string Utf8(const std::wstring& text){if(text.empty())return {};auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);Require(n>0,"Invalid UTF-16");std::string out(n,'\0');Require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),n,nullptr,nullptr)>0,"Encode UTF-8");return out;}
std::wstring Wide(const std::string& text){if(text.empty())return {};auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);Require(n>0,"Invalid UTF-8");std::wstring out(n,L'\0');Require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),n)>0,"Decode UTF-8");return out;}
std::string Hex(std::span<const uint8_t> bytes){std::string out;for(auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::vector<uint8_t> Unhex(const std::string& value){Require(value.size()%2==0&&value.find_first_not_of("0123456789abcdef")==value.npos,"Invalid hexadecimal data");std::vector<uint8_t> out;for(size_t i=0;i<value.size();i+=2)out.push_back(uint8_t((std::string("0123456789abcdef").find(value[i])<<4)|std::string("0123456789abcdef").find(value[i+1])));return out;}
namespace {
struct Algorithm{BCRYPT_ALG_HANDLE h=nullptr;~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Digest{BCRYPT_HASH_HANDLE h=nullptr;~Digest(){if(h)BCryptDestroyHash(h);}};
struct Key{BCRYPT_KEY_HANDLE h=nullptr;~Key(){if(h)BCryptDestroyKey(h);}};
struct MetadataSecurity {
    PSECURITY_DESCRIPTOR descriptor=nullptr;File token;TOKEN_PRIVILEGES previous{};DWORD previousBytes=sizeof(previous);bool adjusted=false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    explicit MetadataSecurity(const fs::path& parent,bool forceSystem=false){
        PSID owner=nullptr;PSECURITY_DESCRIPTOR parentDescriptor=nullptr;
        Require(GetNamedSecurityInfoW(parent.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&parentDescriptor)==ERROR_SUCCESS,"Inspect metadata parent ownership");const bool system=forceSystem||(owner&&IsWellKnownSid(owner,WinLocalSystemSid));LocalFree(parentDescriptor);if(!system)return;
        Require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token.h)!=FALSE,"Open metadata ownership token");
        DWORD bytes=0;GetTokenInformation(token.h,TokenUser,nullptr,0,&bytes);std::vector<uint8_t> user(bytes);Require(GetTokenInformation(token.h,TokenUser,user.data(),bytes,&bytes)!=FALSE,"Read metadata owner identity");
        if(!IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,WinLocalSystemSid)){
            TOKEN_PRIVILEGES requested{};requested.PrivilegeCount=1;Require(LookupPrivilegeValueW(nullptr,L"SeRestorePrivilege",&requested.Privileges[0].Luid)!=FALSE,"Locate metadata ownership privilege");requested.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;SetLastError(ERROR_SUCCESS);
            Require(AdjustTokenPrivileges(token.h,FALSE,&requested,sizeof(previous),&previous,&previousBytes)!=FALSE&&GetLastError()!=ERROR_NOT_ALL_ASSIGNED,"Administrator ownership privilege required");adjusted=true;
        }
        Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE,"Create protected metadata descriptor");attributes.lpSecurityDescriptor=descriptor;
    }
    ~MetadataSecurity(){if(adjusted)AdjustTokenPrivileges(token.h,FALSE,&previous,0,nullptr,nullptr);if(descriptor)LocalFree(descriptor);}
    SECURITY_ATTRIBUTES* Get(){return descriptor?&attributes:nullptr;}
};
File Open(const fs::path& path,bool directory=false){
    File file(CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_FLAG_SEQUENTIAL_SCAN),nullptr));
    Require(file.h!=INVALID_HANDLE_VALUE,"Open ordinary distribution member");BY_HANDLE_FILE_INFORMATION info{};
    Require(GetFileInformationByHandle(file.h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory&&(directory||info.nNumberOfLinks==1),"Distribution member type/link rejected");return file;
}
std::string DigestFile(HANDLE file){
    LARGE_INTEGER size{};Require(GetFileSizeEx(file,&size)&&size.QuadPart>=0&&size.QuadPart<=256ll<<20,"Distribution file exceeds 256 MiB");
    Algorithm algorithm;Digest hash;Require(BCryptOpenAlgorithmProvider(&algorithm.h,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(algorithm.h,&hash.h,nullptr,0,nullptr,0,0)>=0,"Create distribution digest");
    std::array<uint8_t,65536> buffer{};uint64_t total=0;const auto deadline=GetTickCount64()+60000;
    for(;;){Require(GetTickCount64()<deadline,"Distribution hash deadline");DWORD n=0;Require(ReadFile(file,buffer.data(),DWORD(buffer.size()),&n,nullptr),"Read distribution member");if(!n)break;total+=n;Require(total<=uint64_t(size.QuadPart)&&BCryptHashData(hash.h,buffer.data(),n,0)>=0,"Hash distribution member");}
    Require(total==uint64_t(size.QuadPart),"Distribution member changed");std::array<uint8_t,32> value{};Require(BCryptFinishHash(hash.h,value.data(),DWORD(value.size()),0)>=0,"Finish distribution digest");return Hex(value);
}
std::string Lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return value;}
bool Managed(const std::string& name){return name=="active.json"||name=="update.pending.json"||name=="update.last.json"||name=="update.lock"||(name.starts_with("write-")&&name.ends_with(".tmp")&&name.size()==42&&name.substr(6,32).find_first_not_of("0123456789abcdef")==std::string::npos);}
std::vector<fs::path> Members(const fs::path& root,bool managed,bool retainedRuntime=false,std::vector<File>* directoryLocks=nullptr){
    std::vector<fs::path> files;size_t entries=0;
    for(auto it=fs::recursive_directory_iterator(root);it!=fs::recursive_directory_iterator();++it){
        Require(++entries<=8192,"Distribution directory entry quota");
        const auto name=Utf8(it->path().lexically_relative(root).generic_wstring());RelativeName(name);
        const auto attr=GetFileAttributesW(it->path().c_str());Require(attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT),"Distribution reparse point rejected");
        if(managed&&(name=="versions"||(retainedRuntime&&name=="runtime"))){Require(it->is_directory(),"Managed data path must be a directory");it.disable_recursion_pending();continue;}
        if(it->is_directory()){if(directoryLocks)directoryLocks->push_back(Open(it->path(),true));continue;}
        if(name==Utf8(ManifestName)||name==Utf8(SignatureName)||(managed&&Managed(name)))continue;
        Require(files.size()<4096,"Distribution member count limit");files.push_back(it->path());
    }return files;
}
}
void Ordinary(const fs::path& path){
    Require(path.is_absolute()&&path.wstring().find(L'\0')==std::wstring::npos,"Absolute local distribution path required");const auto drive=path.root_name().wstring();Require(drive.size()==2&&drive[1]==L':',"Network/device distribution paths rejected");const auto type=GetDriveTypeW(path.root_path().c_str());Require(type==DRIVE_FIXED||type==DRIVE_REMOVABLE||type==DRIVE_RAMDISK,"Local distribution drive required");auto at=path.root_path();for(const auto& p:path.relative_path()){at/=p;const auto a=GetFileAttributesW(at.c_str());Require(a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_REPARSE_POINT),"Distribution path missing or redirected");}
}
std::string Hash(std::span<const uint8_t> bytes){Algorithm algorithm;Digest digest;Require(BCryptOpenAlgorithmProvider(&algorithm.h,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(algorithm.h,&digest.h,nullptr,0,nullptr,0,0)>=0,"Create SHA-256");Require(bytes.size()<=2u<<20,"Metadata digest limit");Require(BCryptHashData(digest.h,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0)>=0,"Hash metadata");std::array<uint8_t,32> result{};Require(BCryptFinishHash(digest.h,result.data(),DWORD(result.size()),0)>=0,"Finish metadata hash");return Hex(result);}
std::string HashFile(const fs::path& path){Ordinary(path);auto file=Open(path);return DigestFile(file.h);}
std::string Read(const fs::path& path,uint64_t limit){Ordinary(path);auto file=Open(path);LARGE_INTEGER size{};Require(GetFileSizeEx(file.h,&size)&&size.QuadPart>=0&&uint64_t(size.QuadPart)<=limit&&limit<=2u<<20,"Distribution metadata size limit");std::string text(size_t(size.QuadPart),'\0');DWORD n=0;Require(ReadFile(file.h,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size(),"Read distribution metadata");return text;}
Json ReadJson(const fs::path& path){return Json::parse(Read(path),[](int depth,Json::parse_event_t,Json&){Require(depth<=32,"Distribution JSON nesting limit");return true;});}
void AtomicWrite(const fs::path& path,const std::string& text){
    Require(text.size()<=2u<<20,"Atomic metadata quota");Ordinary(path.parent_path());if(fs::exists(path))Ordinary(path);std::array<uint8_t,16> random{};Require(BCryptGenRandom(nullptr,random.data(),DWORD(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Create transaction identity");const auto temporary=path.parent_path()/Wide("write-"+Hex(random)+".tmp");
    MetadataSecurity security(path.parent_path());
    {File file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,security.Get(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Require(file.h!=INVALID_HANDLE_VALUE,"Create atomic metadata");DWORD n=0;Require(WriteFile(file.h,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size()&&FlushFileBuffers(file.h),"Flush atomic metadata");}
    Require(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Commit atomic metadata");
}
void AtomicJson(const fs::path& path,const Json& value){AtomicWrite(path,value.dump()+"\n");}
void CreateDirectorySecure(const fs::path& path,bool forceSystem){
    if(fs::exists(path)){Ordinary(path);Require(fs::is_directory(path),"Expected distribution directory");return;}
    const auto parent=path.parent_path();Require(parent!=path&&!parent.empty(),"Invalid distribution directory root");CreateDirectorySecure(parent);MetadataSecurity security(parent,forceSystem);
    Require(CreateDirectoryW(path.c_str(),security.Get())!=FALSE||GetLastError()==ERROR_ALREADY_EXISTS,"Create protected distribution directory");Ordinary(path);
}
void CopyMember(const fs::path& source,const fs::path& target){
    Ordinary(source);CreateDirectorySecure(target.parent_path());auto input=Open(source);LARGE_INTEGER size{};Require(GetFileSizeEx(input.h,&size)&&size.QuadPart>=0&&size.QuadPart<=256ll<<20,"Copy member size limit");MetadataSecurity security(target.parent_path());
    File output(CreateFileW(target.c_str(),GENERIC_WRITE,0,security.Get(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Require(output.h!=INVALID_HANDLE_VALUE,"Create verified copy destination");std::array<uint8_t,65536> data{};uint64_t total=0;
    for(;;){DWORD n=0;Require(ReadFile(input.h,data.data(),DWORD(data.size()),&n,nullptr),"Read verified copy source");if(!n)break;DWORD written=0;Require(WriteFile(output.h,data.data(),n,&written,nullptr)&&written==n,"Write verified copy destination");total+=n;Require(total<=uint64_t(size.QuadPart),"Copy source changed");}Require(total==uint64_t(size.QuadPart)&&FlushFileBuffers(output.h),"Flush verified copy destination");
}
void RelativeName(const std::string& name){
    Require(!name.empty()&&name.size()<=768&&name.front()!='/'&&name.find_first_of("\\:\0",0,3)==name.npos&&std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32&&c<127;}),"Unsafe distribution member name");
    size_t start=0;while(start<name.size()){auto end=name.find('/',start);if(end==name.npos)end=name.size();const auto part=name.substr(start,end-start);Require(!part.empty()&&part!="."&&part!=".."&&part.back()!='.'&&part.back()!=' ',"Distribution traversal/alias rejected");auto base=Lower(part.substr(0,part.find('.')));Require(base!="con"&&base!="prn"&&base!="aux"&&base!="nul"&&!(base.size()==4&&(base.starts_with("com")||base.starts_with("lpt"))&&base[3]>='1'&&base[3]<='9'),"Reserved Windows member name");start=end+1;}Require(name.back()!='/',"Trailing member separator");
}
std::string PublicKey(){
#ifdef AEGIS_DISTRIBUTION_TESTING
    extern std::string TestPublisherPublicKey;return TestPublisherPublicKey;
#else
    return PublisherPublicKey;
#endif
}
void VerifySignature(const std::string& data,const std::string& signature,const std::string& supplied){
    auto publicKey=Unhex(supplied.empty()?PublicKey():supplied);auto sig=Unhex(signature);Require(publicKey.size()==72&&sig.size()==64,"Publisher signature/key length rejected");Algorithm algorithm;Key key;
    Require(BCryptOpenAlgorithmProvider(&algorithm.h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0&&BCryptImportKeyPair(algorithm.h,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key.h,publicKey.data(),ULONG(publicKey.size()),0)>=0,"Import publisher verification key");
    auto digest=Unhex(Hash({reinterpret_cast<const uint8_t*>(data.data()),data.size()}));Require(BCryptVerifySignature(key.h,nullptr,digest.data(),ULONG(digest.size()),sig.data(),ULONG(sig.size()),0)>=0,"Publisher signature verification failed");
}
Json MakeManifest(const fs::path& input,const std::string& kind,uint64_t sequence,const std::string& version,const std::string& configuration){
    auto root=fs::absolute(input).lexically_normal();Ordinary(root);Require((kind=="portable"||kind=="engines"||kind=="definitions")&&sequence>0&&version.size()<=64&&(configuration=="Release"||configuration=="Debug"),"Invalid release metadata");Json members=Json::object();std::set<std::string> seen;uint64_t total=0;
    for(const auto& file:Members(root,false)){auto name=Utf8(file.lexically_relative(root).generic_wstring());Require(seen.insert(Lower(name)).second,"Case-insensitive member collision");const auto bytes=fs::file_size(file);total+=bytes;Require(total<=2ull<<30,"Distribution total quota");members[name]={{"bytes",bytes},{"sha256",HashFile(file)}};}
    Require(!members.empty(),"Empty distribution package");return {{"schema",1},{"product","Aegis"},{"kind",kind},{"sequence",sequence},{"version",version},{"configuration",configuration},{"architecture","x64"},{"serviceProtocol",1},{"dataSchema",1},{"files",members}};
}
VerifiedTree Verify(const fs::path& input,const std::string& kind,bool managed,const std::string& supplied){
    VerifiedTree result;result.root=fs::absolute(input).lexically_normal();Ordinary(result.root);auto ancestor=result.root.root_path();result.locks.push_back(Open(ancestor,true));for(const auto& part:result.root.relative_path()){ancestor/=part;result.locks.push_back(Open(ancestor,true));}result.locks.push_back(Open(result.root/ManifestName));result.locks.push_back(Open(result.root/SignatureName));const auto raw=Read(result.root/ManifestName);auto sig=Read(result.root/SignatureName,129);if(!sig.empty()&&sig.back()=='\n')sig.pop_back();VerifySignature(raw,sig,supplied);
    result.manifest=Json::parse(raw,[](int depth,Json::parse_event_t,Json&){Require(depth<=16,"Signed manifest nesting limit");return true;});const auto& m=result.manifest;
    Require(m.is_object()&&m.size()==10&&m.at("schema")==1&&m.at("product")=="Aegis"&&m.at("kind")==kind&&m.at("architecture")=="x64"&&m.at("serviceProtocol")==1&&m.at("dataSchema")==1&&m.at("sequence").is_number_unsigned()&&m.at("sequence").get<uint64_t>()>0&&m.at("version").is_string()&&m.at("version").get<std::string>().size()<=64&&(m.at("configuration")=="Release"||m.at("configuration")=="Debug"),"Signed package contract rejected");
    const auto& members=m.at("files");Require(members.is_object()&&!members.empty()&&members.size()<=4096,"Signed file inventory rejected");std::set<std::string> seen;uint64_t total=0;
    auto actual=Members(result.root,managed,kind=="engines"&&managed,&result.locks);Require(actual.size()==members.size(),"Unlisted or missing distribution members");for(const auto& path:actual)Require(members.contains(Utf8(path.lexically_relative(result.root).generic_wstring())),"Unlisted distribution member");
    for(auto it=members.begin();it!=members.end();++it){RelativeName(it.key());Require(seen.insert(Lower(it.key())).second&&it.value().is_object()&&it.value().size()==2&&it.value().at("bytes").is_number_unsigned(),"Signed member entry rejected");const auto path=result.root/Wide(it.key());Ordinary(path);auto lock=Open(path);LARGE_INTEGER bytes{};Require(GetFileSizeEx(lock.h,&bytes)&&uint64_t(bytes.QuadPart)==it.value().at("bytes").get<uint64_t>(),"Signed member size mismatch");total+=uint64_t(bytes.QuadPart);Require(total<=2ull<<30&&DigestFile(lock.h)==it.value().at("sha256").get<std::string>(),"Signed member hash mismatch");result.locks.push_back(std::move(lock));}
    // Keep the signed metadata itself locked through launch/copy as well.
    result.locks.push_back(Open(result.root/ManifestName));result.locks.push_back(Open(result.root/SignatureName));result.identity=Hash({reinterpret_cast<const uint8_t*>(raw.data()),raw.size()});return result;
}
fs::path ExecutablePath(){wchar_t value[32768]{};const auto n=GetModuleFileNameW(nullptr,value,32768);Require(n&&n<32768,"Executable path unavailable");return fs::path(std::wstring(value,n));}
} // namespace aegis::distribution
