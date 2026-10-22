#include "../guest/PeServices.h"
#include "../guest/Volume.h"
#include "../guest/Heap.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace rt=guardian::runtime;namespace fs=guardian::storage;
namespace {
uint32_t checks=0;void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct State {
    uint8_t disk[65536]{};fs::Volume volume;guardian::threads::Scheduler scheduler;rt::Services services;bool fail=false;uint32_t console=0;
    static bool Block(void* p,guardian::io::Op op,uint64_t off,uint8_t* b) noexcept {auto& s=*static_cast<State*>(p);if(op==guardian::io::Op::Flush)return true;
        if(off>65536-512)return false;if(op==guardian::io::Op::Read)std::memcpy(b,s.disk+off,512);else if(op==guardian::io::Op::Write)std::memcpy(s.disk+off,b,512);else return false;return true;}
    static uint32_t Storage(void* p,uint32_t pid,const guardian::processes::Token& t,fs::Command& c) noexcept {auto& s=*static_cast<State*>(p);if(s.fail){s.fail=false;return fs::IoError;}return s.volume.Execute(pid,t,c);}
    static uint32_t Console(void* p,uint32_t,const uint8_t*,uint32_t n,uint32_t& out) noexcept {static_cast<State*>(p)->console+=n;out=n;return 0;}
};
void Make(rt::Command& c,rt::Op op,uint64_t h=0){c={};c.op=op;c.handle=h;}
void Text(char* out,const char* in){for(uint32_t i=0;;++i){out[i]=in[i];if(!in[i])return;}}
void Core(){
    auto s=std::make_unique<State>();s->volume.Configure(s.get(),State::Block,s->scheduler);Check(s->volume.Mount()==0,"Mount");s->services.Configure(s.get(),State::Storage,State::Console);
    guardian::processes::Token t{};t.user=100;t.primaryGroup=10;t.integrity=2;rt::Command c{};
    const auto run=[&](uint32_t pid=1){return s->services.Execute(pid,t,c);};
    Make(c,rt::Op::Open);Text(c.path,"C:\\test.bin");c.access=0xC0000000;c.share=3;c.disposition=4;s->fail=true;Check(run()==fs::IoError && !s->services.Count() && !s->volume.Handles(),"Open rollback");
    c.status=0;Check(run()==0 && !c.flags,"Open new");const auto h=c.handle;
    Make(c,rt::Op::Write,h);c.length=4;std::memcpy(c.data,"test",4);Check(run()==0 && c.transferred==4,"Write");
    Make(c,rt::Op::Size,h);Check(run()==0 && c.argument==4,"Size");
    Make(c,rt::Op::Seek,h);c.share=2;c.argument=uint64_t(-4ll);Check(run()==0 && !c.argument,"Seek end");
    Make(c,rt::Op::Read,h);c.length=4;s->fail=true;Check(run()==fs::IoError,"Read injected error");c.status=0;Check(run()==0 && c.transferred==4 && std::memcmp(c.data,"test",4)==0,"Read position changed on failure");
    Make(c,rt::Op::Read,h);c.length=4;Check(run()==0 && !c.transferred,"EOF");
    Make(c,rt::Op::Seek,h);c.argument=UINT64_MAX;Check(run()==rt::NegativeSeek,"Negative seek");
    Make(c,rt::Op::Seek,h);c.argument=513;Check(run()==fs::NoSpace,"Seek quota");
    Make(c,rt::Op::Write,h);c.length=509;Check(run()==fs::NoSpace,"Write beyond file capacity");
    Make(c,rt::Op::Read,h);Check(run(2)==fs::InvalidHandle,"Cross-process handle");
    Make(c,rt::Op::Open);Text(c.path,"C:\\test.bin");c.access=0x80000000;c.share=3;c.disposition=3;Check(run()==0 && c.flags==1,"Open existing");const auto ro=c.handle;
    Make(c,rt::Op::Write,ro);c.length=1;Check(run()==fs::Denied,"Read-only write");
    Make(c,rt::Op::Flush,ro);Check(run()==fs::Denied,"Read-only flush");
    Make(c,rt::Op::Close,ro);Check(run()==0,"Close");c.status=0;Check(run()==fs::InvalidHandle,"Stale handle");
    Make(c,rt::Op::Open);Text(c.path,"C:\\test.bin");c.access=0x80000000;c.share=3;c.disposition=2;Check(run()==fs::Denied,"Create-always denied before truncate");
    Make(c,rt::Op::Size,h);Check(run()==0 && c.argument==4,"Denied create changed file");
    Make(c,rt::Op::Open);Text(c.path,"C:\\test.bin");c.access=0xC0000000;c.share=3;c.disposition=1;Check(run()==fs::Collision,"Create-new collision");
    Make(c,rt::Op::RegOpen,rt::CurrentUser);c.flags=1;c.access=0x2001F;Text(c.path,"Runtime");Check(run()==0 && c.disposition==1,"Create key");const auto key=c.handle;
    Make(c,rt::Op::RegSet,key);c.type=4;c.length=4;Text(c.name,"Number");c.data[0]=42;Check(run()==0,"Set value");
    Make(c,rt::Op::RegGet,key);c.length=1;Text(c.name,"Number");c.data[0]=0xAA;Check(run()==fs::Small && c.length==4 && c.type==4 && c.data[0]==0xAA,"Small registry query");
    Make(c,rt::Op::RegGet,key);c.flags=1;Text(c.name,"Number");Check(run()==0 && c.length==4 && c.type==4 && !c.transferred,"Query size");
    Make(c,rt::Op::RegGet,key);c.length=512;Text(c.name,"Number");Check(run()==0 && c.length==4 && c.transferred==4 && c.data[0]==42,"Get value");
    Make(c,rt::Op::RegOpen,rt::CurrentUser);c.access=1;Text(c.path,"Runtime");Check(run()==0 && c.disposition==2,"Open read-only key");const auto kr=c.handle;
    Make(c,rt::Op::RegSet,kr);c.type=4;c.length=4;Check(run()==fs::Denied,"Kernel key handle rights");
    Make(c,rt::Op::RegOpen,kr);c.flags=1;c.access=3;Text(c.path,"Child");Check(run()==fs::Denied,"Kernel parent key rights");
    Make(c,rt::Op::RegGet,key);Check(run(2)==fs::InvalidHandle,"Cross-process key");
    Make(c,rt::Op::RegClose,h);Check(run()==fs::InvalidHandle,"File treated as key");
    Make(c,rt::Op::Close,key);Check(run()==fs::InvalidHandle,"Key treated as file");
    Make(c,rt::Op::ConsoleWrite,rt::Console);c.length=3;std::memcpy(c.data,"ok\n",3);Check(run()==0 && c.transferred==3 && s->console==3,"Console");
    Make(c,rt::Op::ConsoleWrite,rt::Console);c.length=1;c.data[0]=1;Check(run()==fs::Invalid && s->console==3,"Control character reached host");
    Make(c,rt::Op::RegOpen,rt::CurrentUser);c.access=3;std::memset(c.path,'x',128);Check(run()==fs::Invalid,"Unterminated key path");
    for(uint32_t i=0;i<6;++i){Make(c,rt::Op::Open);Text(c.path,"C:\\test.bin");c.access=0x80000000;c.share=3;c.disposition=3;const auto status=run();if(i<5)Check(status==0,"Handle quota early");else Check(status==fs::Quota,"Handle quota exceeded");}
    Make(c,rt::Op::RegOpen,rt::CurrentUser);c.flags=1;c.access=3;Text(c.path,"MustNotExist");Check(run()==fs::Quota,"Key quota mutated volume");
    s->fail=true;Check(!s->services.Exit(1,t) && s->services.Count()==1 && s->volume.Handles()==1,"Failed exit discarded live handle");
    Check(s->services.Exit(1,t) && !s->services.Count() && !s->volume.Handles(),"Process-exit leaked handles");
    Make(c,rt::Op::RegOpen,rt::CurrentUser);c.access=1;Text(c.path,"MustNotExist");Check(run()==fs::NotFound,"Quota left created key");
    for(uint32_t field=0;field<5;++field){Make(c,rt::Op::Open);if(field==0)c.version=0;if(field==1)c.bytes=0;if(field==2)c.reserved=1;if(field==3)c.status=1;if(field==4)c.transferred=1;
        Check(run()==fs::Invalid && !s->services.Count(),"Malformed request mutated state");}
    Make(c,rt::Op::Open);Check(run(0)==fs::Invalid,"Zero PID accepted");
}
void ExitIsolation(){
    auto s=std::make_unique<State>();s->volume.Configure(s.get(),State::Block,s->scheduler);Check(s->volume.Mount()==0,"Isolation mount");s->services.Configure(s.get(),State::Storage,State::Console);
    guardian::processes::Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;rt::Command c{};uint64_t parent=0;
    for(uint32_t pid=1;pid<=2;++pid){Make(c,rt::Op::Open);Text(c.path,"C:\\shared.bin");c.access=0xC0000000;c.share=3;c.disposition=4;Check(s->services.Execute(pid,token,c)==0,"Isolation open");if(pid==1)parent=c.handle;
        Make(c,rt::Op::RegOpen,rt::CurrentUser);c.flags=1;c.access=3;Text(c.path,"Isolation");Check(s->services.Execute(pid,token,c)==0,"Isolation key");}
    Check(s->services.Count()==4 && s->services.Count(1)==2 && s->services.Count(2)==2 && !s->services.Count(3),"Counts per process");
    s->fail=true;Check(!s->services.Exit(2,token) && s->services.Count(2)==1 && s->services.Count(1)==2 && s->services.Count()==3,"Failed child cleanup touched parent");
    Check(s->services.Exit(2,token) && !s->services.Count(2) && s->services.Count(1)==2 && s->volume.Handles()==1,"Child cleanup touched parent");
    Make(c,rt::Op::Size,parent);Check(s->services.Execute(1,token,c)==0 && !c.argument,"Parent handle lost");
    Check(s->services.Exit(1,token) && !s->services.Count() && !s->volume.Handles(),"Parent cleanup incomplete");
}
void Heap(){
    alignas(16) uint8_t bytes[1024]{};guardian::memory::Heap h;Check(h.Initialize(bytes,sizeof(bytes)),"Heap init");auto* p=static_cast<uint8_t*>(h.Allocate(64,true));Check(p && !(reinterpret_cast<uint64_t>(p)&15) && h.Size(p)==64,"Heap allocation");
    for(uint32_t i=0;i<64;++i){Check(p[i]==0,"Heap zero");p[i]=uint8_t(i+1);}Check(!h.Reallocate(p,2048) && h.Size(p)==64 && h.Used()==1,"Heap failed realloc lost allocation");
    auto* next=static_cast<uint8_t*>(h.Reallocate(p,128));Check(next && h.Size(next)==128,"Heap realloc");for(uint32_t i=0;i<64;++i)Check(next[i]==i+1,"Heap data lost");
    Check(!h.Release(next+1) && h.Release(next) && !h.Release(next) && !h.Used(),"Heap free validation");
    auto* all=h.Allocate(1008);Check(all && h.Release(all),"Heap coalescing");
}
}
int main(){try{Core();Heap();std::cout<<checks<<" runtime service/heap checks PASS\n";const auto baseline=checks;ExitIsolation();std::cout<<(checks-baseline)<<" per-process service exit checks PASS\n";return 0;}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
