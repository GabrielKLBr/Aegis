#include "../guest/Unwind.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace guardian;
namespace {
struct Fixture {alignas(16) std::array<uint8_t,4096> image{};alignas(16) uint64_t stack[64]{};
    uint64_t Base()const{return reinterpret_cast<uint64_t>(image.data());}uint64_t Stack()const{return reinterpret_cast<uint64_t>(stack);}};
bool Image(void* raw,uint64_t ip,unwind::Image& out) noexcept {auto& f=*static_cast<Fixture*>(raw);if(ip<f.Base()||ip-f.Base()>=f.image.size())return false;out={f.Base(),4096,0x200,1};return true;}
bool Read(void* raw,uint64_t at,void* out,uint32_t n) noexcept {auto& f=*static_cast<Fixture*>(raw);if((at>=f.Base()&&at-f.Base()<=4096&&n<=4096-(at-f.Base()))||(at>=f.Stack()&&at-f.Stack()<=sizeof(f.stack)&&n<=sizeof(f.stack)-(at-f.Stack()))){std::memcpy(out,reinterpret_cast<void*>(at),n);return true;}return false;}
void Check(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
}
int main(){try{
    Fixture f;auto* entry=reinterpret_cast<PRUNTIME_FUNCTION>(f.image.data()+0x200);*entry={0x100,0x140,0x300};
    std::fill(f.image.begin()+0x100,f.image.begin()+0x140,uint8_t(0x90));const uint8_t prolog[]{0x48,0x83,0xEC,0x28},epilog[]{0x48,0x83,0xC4,0x28,0xC3};
    std::memcpy(f.image.data()+0x100,prolog,4);std::memcpy(f.image.data()+0x13B,epilog,5);
    const uint8_t v2[]{2,4,3,0,1,0x16,0,6,4,0x42,0,0};std::memcpy(f.image.data()+0x300,v2,sizeof(v2));
    unwind::Backend api{Image,Read,nullptr};const unwind::Stack bounds{f.Stack(),f.Stack()+sizeof(f.stack)};
    for(uint32_t pc:{0x102u,0x120u,0x13Fu}){unwind::Context ours;CONTEXT windows{};windows.ContextFlags=CONTEXT_FULL;
        const uint32_t index=pc==0x13F?21:16;f.stack[index+(pc==0x120?5:0)]=f.Base()+0x180;
        ours.rip=windows.Rip=f.Base()+pc;ours.gpr[4]=windows.Rsp=f.Stack()+index*8;
        void* handler=nullptr;DWORD64 establisher=0;RtlVirtualUnwind(0,f.Base(),windows.Rip,entry,&windows,&handler,&establisher,nullptr);
        Check(unwind::Step(api,&f,bounds,ours)==unwind::Status::Ok&&ours.rip==windows.Rip&&ours.gpr[4]==windows.Rsp,"Version 2 prolog/body/epilog differs from Windows");}
    const uint8_t frame[]{1,4,1,5,4,0x53,0,0};std::memcpy(f.image.data()+0x300,frame,sizeof(frame));
    unwind::Context ours;CONTEXT windows{};windows.ContextFlags=CONTEXT_FULL;ours.rip=windows.Rip=f.Base()+0x120;ours.gpr[4]=windows.Rsp=f.Stack()+96;ours.gpr[5]=windows.Rbp=f.Stack()+128;f.stack[16]=f.Base()+0x180;
    void* handler=nullptr;DWORD64 establisher=0;RtlVirtualUnwind(0,f.Base(),windows.Rip,entry,&windows,&handler,&establisher,nullptr);
    Check(unwind::Step(api,&f,bounds,ours)==unwind::Status::Ok&&ours.rip==windows.Rip&&ours.gpr[4]==windows.Rsp,"Reserved frame op-info differs from Windows");
    std::memcpy(f.image.data()+0x300,v2,sizeof(v2));f.image[0x306]=0xFF;f.image[0x307]=0xF6;ours.rip=f.Base()+0x120;ours.gpr[4]=f.Stack()+128;const auto saved=ours;
    Check(unwind::Step(api,&f,bounds,ours)==unwind::Status::Invalid&&ours.rip==saved.rip&&ours.gpr[4]==saved.gpr[4],"Invalid epilog scope changed the context");
    std::cout<<"Native unwind v2 and frame metadata match Windows; malformed scope leaves context intact PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
