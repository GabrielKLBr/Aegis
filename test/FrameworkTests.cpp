#include "guardian/Cli.h"
#include "guardian/ConsoleUtf8.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::vector<uint8_t> Read(const std::filesystem::path& path){const auto n=std::filesystem::file_size(path);Need(n>0&&n<=guardian::cli::MaxFile,"test file size");std::vector<uint8_t> out(size_t(n),uint8_t(0));std::ifstream file(path,std::ios::binary);Need(bool(file.read(reinterpret_cast<char*>(out.data()),std::streamsize(n))),"test read");return out;}
bool Valid(const std::vector<uint8_t>& bytes){guardian::cli::Image image;return image.Open(bytes.data(),uint32_t(bytes.size()))&&image.Validate();}
void Utf8(){
    for(const auto* text:{"ASCII\r\n\t","ação 🎉","\xF4\x8F\xBF\xBF"}){const std::string s=text;Need(guardian::ConsoleUtf8(reinterpret_cast<const uint8_t*>(s.data()),uint32_t(s.size())),"valid UTF8");}
    for(const auto* text:{"\x80","\xC0\x80","\xED\xA0\x80","\xF4\x90\x80\x80","\xE2\x82","\x1B","\xC2\x85"}){const std::string s=text;Need(!guardian::ConsoleUtf8(reinterpret_cast<const uint8_t*>(s.data()),uint32_t(s.size())),"invalid UTF8");}
    guardian::ConsoleUtf8Decoder decoder;Need(decoder.Put(0xED)&&decoder.Put(0xA0)&&!decoder.Put(0x80)&&decoder.failed&&!decoder.Put('a'),"invalid UTF8 sticky failure");
}
}
int main(int argc,char** argv){try{
    Need(argc==2,"Usage: guardian-framework-tests MANAGED_EXE");Utf8();const auto seed=Read(argv[1]);Need(Valid(seed),"positive CIL fixture");
    for(size_t n=0;n<seed.size();++n){const std::vector<uint8_t> truncated(seed.begin(),seed.begin()+n);Need(!Valid(truncated),"truncated PE must fail");}
    guardian::cli::Image image;Need(image.Open(seed.data(),uint32_t(seed.size()))&&image.Validate(),"CIL body access");guardian::cli::Signature signature;guardian::cli::Body body;
    Need(image.Method(image.entry,signature,body)&&body.bytes>=10,"test body length");const auto offset=size_t(body.code-seed.data());auto branch=seed;
    for(uint32_t i=0;i<body.bytes;++i)branch[offset+i]=0;
    branch[offset]=0x2B;branch[offset+1]=1;branch[offset+2]=0x20;for(size_t i=3;i<7;++i)branch[offset+i]=0;branch[offset+7]=0x26;branch[offset+8]=0x16;branch[offset+9]=0x2A;
    guardian::cli::Image rejected;Need(rejected.Open(branch.data(),uint32_t(branch.size()))&&!rejected.Validate()&&rejected.error==guardian::cli::Error::Branch,"branch into instruction rejected");
    auto token=seed;token[offset]=0x28;token[offset+1]=0xFF;token[offset+2]=0xFF;token[offset+3]=0xFF;token[offset+4]=6;Need(!Valid(token),"method token rejected");
    uint32_t random=0x14110001;uint32_t accepted=0;constexpr uint32_t cases=20000;
    for(uint32_t i=0;i<cases;++i){auto mutated=seed;for(uint32_t j=0;j<1+i%4;++j){random^=random<<13;random^=random>>17;random^=random<<5;mutated[random%mutated.size()]^=uint8_t(1u<<(random%8));}if(Valid(mutated))++accepted;}
    std::cout<<"{\"status\":\"PASS\",\"mutations\":"<<cases<<",\"acceptedForInspection\":"<<accepted<<",\"truncated\":"<<seed.size()<<",\"mutantsExecuted\":false,\"utf8Checks\":11}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
