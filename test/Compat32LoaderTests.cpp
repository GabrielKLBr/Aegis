#include "../guest/PeLoader.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace p=guardian::pe;
namespace {
uint32_t checks=0;
void Need(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
struct Region {uint64_t base=0;std::vector<uint8_t> data;std::vector<p::Protection> pages;};
struct Memory {
    std::array<Region,p::MaxModules> regions{};uint32_t mapped=0;
    Region* Find(uint64_t at,uint32_t bytes){for(auto& r:regions)if(r.base&&at>=r.base&&at-r.base<r.data.size()&&bytes<=r.data.size()-(at-r.base))return &r;return nullptr;}
    static bool Map(void* raw,uint64_t at,uint32_t bytes) noexcept {auto& m=*static_cast<Memory*>(raw);if(at>=0x80000000||!bytes||bytes>p::MaxImageBytes)return false;for(auto& r:m.regions)if(!r.base){r.base=at;r.data.assign(bytes,0);r.pages.assign(bytes/4096,p::Protection::ReadWrite);++m.mapped;return true;}return false;}
    static bool Read(void* raw,uint64_t at,void* out,uint32_t bytes) noexcept {auto& m=*static_cast<Memory*>(raw);auto* r=m.Find(at,bytes);if(!r)return false;std::memcpy(out,r->data.data()+at-r->base,bytes);return true;}
    static bool Write(void* raw,uint64_t at,const void* in,uint32_t bytes) noexcept {auto& m=*static_cast<Memory*>(raw);auto* r=m.Find(at,bytes);if(!r)return false;for(auto i=uint32_t((at-r->base)/4096);i<=(at-r->base+bytes-1)/4096;++i)if(r->pages[i]!=p::Protection::ReadWrite)return false;std::memcpy(r->data.data()+at-r->base,in,bytes);return true;}
    static bool Protect(void* raw,uint64_t at,uint32_t bytes,p::Protection access) noexcept {auto* r=static_cast<Memory*>(raw)->Find(at,bytes);if(!r||at%4096||bytes%4096)return false;for(uint32_t i=0;i<bytes/4096;++i)r->pages[(at-r->base)/4096+i]=access;return true;}
    static bool Unmap(void* raw,uint64_t at) noexcept {auto& m=*static_cast<Memory*>(raw);for(auto& r:m.regions)if(r.base==at){r={};--m.mapped;return true;}return false;}
};
std::vector<uint8_t> ReadFile(const std::filesystem::path& path){const auto n=std::filesystem::file_size(path);Need(n&&n<8u<<20,"input size");std::vector<uint8_t> data(static_cast<size_t>(n));std::ifstream f(path,std::ios::binary);Need(bool(f.read(reinterpret_cast<char*>(data.data()),std::streamsize(n))),"input read");return data;}
uint32_t U32(const std::vector<uint8_t>& b,uint32_t at){uint32_t n=0;std::memcpy(&n,b.data()+at,4);return n;}
void Put(std::vector<uint8_t>& b,uint32_t at,uint32_t n){std::memcpy(b.data()+at,&n,4);}
using Inputs=std::array<std::vector<uint8_t>,5>;
bool Load(Inputs& bytes,bool requireSuccess=false){
    const char* names[]={"app.exe","kernel32.dll","user32.dll","win32u.dll","msvcrt.dll"};p::Source sources[5]{};
    for(uint32_t i=0;i<5;++i)sources[i]={names[i],bytes[i].data(),uint32_t(bytes[i].size())};
    Memory memory;p::Loader loader;const p::Backend backend{Memory::Map,Memory::Read,Memory::Write,Memory::Protect,Memory::Unmap};uint32_t index=0;
    const auto ok=loader.Configure(backend,&memory,sources,5,0x40040000)&&loader.Load("app.exe",index);
    if(ok&&requireSuccess){const auto* app=loader.Get(0);Need(app&&app->x86&&app->base<0x80000000&&app->imports>0&&app->relocated>0,"PE32 fixups and IAT");uint64_t api=0;Need(loader.Resolve(1,"GetCurrentThreadId",0,api)&&api<0x80000000&&loader.Executable(api),"32-bit export target");uint8_t v=0;Need(!Memory::Write(&memory,app->entry,&v,1),"sealed RX is not writable");}
    Need(loader.UnloadAll()&&memory.mapped==0,"transactional cleanup");return ok;
}
}
int main(int argc,char** argv){try{
    Need(argc==3,"usage: compat32-tests RUNTIME GUI-EXE");const std::filesystem::path runtime=argv[1];Inputs seed{ReadFile(argv[2]),ReadFile(runtime/"kernel32.dll"),ReadFile(runtime/"user32.dll"),ReadFile(runtime/"win32u.dll"),ReadFile(runtime/"msvcrt.dll")};
    Need(Load(seed,true),"positive PE32 GUI graph");const auto opt=U32(seed[0],60)+24;
    auto bad=seed;Put(bad[0],opt-20,0x8664);Need(!Load(bad),"x64 machine cannot enter x86 graph");
    bad=seed;Put(bad[1],U32(bad[1],60)+4,0x8664);Need(!Load(bad),"mixed-architecture DLL rejected");
    bad=seed;Put(bad[0],opt+96+9*8,0x1000);Put(bad[0],opt+100+9*8,24);Need(!Load(bad),"unsupported PE32 TLS rejected");
    bad=seed;Put(bad[0],opt+60,UINT32_MAX);Need(!Load(bad),"header overflow rejected");
    for(size_t n=0;n<seed[0].size();++n){bad=seed;bad[0].resize(n);Need(!Load(bad),"truncated PE32 rejected");}
    uint32_t random=0x14120001;constexpr uint32_t mutations=1000;
    for(uint32_t i=0;i<mutations;++i){bad=seed;random^=random<<13;random^=random>>17;random^=random<<5;bad[0][random%bad[0].size()]^=uint8_t(1u<<(random%8));Load(bad);}
    std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"mutations\":"<<mutations<<",\"truncations\":"<<seed[0].size()<<",\"inputsExecuted\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
