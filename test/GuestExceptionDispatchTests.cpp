#include "../guest/ExceptionDispatch.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace e=guardian::exceptions;namespace u=guardian::unwind;
namespace {
uint32_t checks=0;
void Check(bool value){++checks;if(!value)throw std::runtime_error("Exception dispatch check "+std::to_string(checks));}
struct Fixture {
    alignas(16) std::array<uint8_t,4096> image{},stack{};uint32_t reads=0,fail=0;
    Fixture(){image.fill(0x90);Word(0x400,0x100);Word(0x404,0x180);Word(0x408,0x500);
        Word(0x40C,0x200);Word(0x410,0x280);Word(0x414,0x520);
        image[0x500]=0x19;image[0x501]=4;image[0x502]=2;image[0x503]=0;
        image[0x504]=4;image[0x505]=0x32;image[0x506]=1;image[0x507]=0x30;Word(0x508,0x300);
        image[0x520]=0x19;image[0x521]=4;image[0x522]=1;image[0x523]=0;image[0x524]=4;image[0x525]=0x32;Word(0x528,0x310);
        Saved(0x120,0xBEEF);Saved(0x128,Base()+0x240);Saved(0x150,0xDEAD);
    }
    uint64_t Base() const{return reinterpret_cast<uint64_t>(image.data());}
    uint64_t Low() const{return reinterpret_cast<uint64_t>(stack.data());}
    void Word(uint32_t at,uint32_t value){std::memcpy(image.data()+at,&value,4);}
    void Saved(uint32_t at,uint64_t value){std::memcpy(stack.data()+at,&value,8);}
    e::Context Context() const {e::Context c{};c.gpr[4]=Low()+0x100;c.gpr[3]=9;c.rip=Base()+0x140;c.fx[24]=0x80;c.fx[25]=0x1F;return c;}
    u::Stack Bounds() const {return {Low(),Low()+stack.size()};}
    static bool Image(void* p,uint64_t pc,u::Image& out) noexcept {auto& f=*static_cast<Fixture*>(p);if(pc<f.Base() || pc>=f.Base()+4096)return false;out={f.Base(),4096,0x400,2};return true;}
    static bool Read(void* p,uint64_t at,void* out,uint32_t n) noexcept {auto& f=*static_cast<Fixture*>(p);if(++f.reads==f.fail)return false;
        const bool image=at>=f.Base() && at<=f.Base()+4096 && n<=f.Base()+4096-at;
        const bool stack=at>=f.Low() && at<=f.Low()+4096 && n<=f.Low()+4096-at;
        if(!image && !stack)return false;std::memcpy(out,reinterpret_cast<void*>(at),n);return true;}
    static bool Rx(void* p,uint64_t at) noexcept {const auto& f=*static_cast<Fixture*>(p);return at>=f.Base()+0x100 && at<f.Base()+0x400;}
    u::Backend Api() const{return {Image,Read,Rx};}
    e::Record Record() const {e::Record r{};r.code=e::IllegalInstruction;r.address=Context().rip;return r;}
};
void Recovery(){Fixture f;auto c=f.Context();e::Dispatch dispatch;e::Action a{};
    Check(e::Dispatch::ValidContext(c,f.Bounds(),f.Api(),&f));Check(dispatch.Begin(f.Record(),c,f.Bounds()));Check(!dispatch.Begin(f.Record(),c,f.Bounds()));
    Check(dispatch.Next(f.Api(),&f,a)==e::Result::Call && a.handler.establisher==c.gpr[4]);
    Check(dispatch.Next(f.Api(),&f,a)==e::Result::Invalid);Check(!dispatch.Reply(3,c));
    c.rip+=2;c.gpr[0]=123;Check(dispatch.Reply(0,c));Check(dispatch.Next(f.Api(),&f,a)==e::Result::Resume);
    Check(dispatch.ResumeContext().rip==c.rip && dispatch.ResumeContext().gpr[0]==123);Check(!dispatch.Reply(0,c));
    for(uint32_t kind=0;kind<7;++kind){auto bad=f.Context();
        switch(kind){case 0:bad.rip=0x100000;break;case 1:bad.gpr[4]=f.Low()-8;break;case 2:bad.gpr[4]++;break;case 3:bad.eflags|=0x3000;break;case 4:bad.mxcsr=UINT32_MAX;break;case 5:bad.fx[27]=255;break;case 6:bad.cs=8;break;}
        Check(!e::Dispatch::ValidContext(bad,f.Bounds(),f.Api(),&f));}
    e::Dispatch noncontinuable;auto r=f.Record();r.flags=1;Check(noncontinuable.Begin(r,f.Context(),f.Bounds()));Check(noncontinuable.Next(f.Api(),&f,a)==e::Result::Call);Check(!noncontinuable.Reply(0,f.Context()));
}
bool Unwind(Fixture& f,bool inject=false){const auto c=f.Context();e::Dispatch d;e::Action a{};Check(d.Begin(f.Record(),c,f.Bounds()));
    auto result=d.Next(f.Api(),&f,a);if(inject && result!=e::Result::Call)return false;Check(result==e::Result::Call && a.handler.routine==f.Base()+0x300);
    Check(d.Reply(1,c));result=d.Next(f.Api(),&f,a);if(inject && result!=e::Result::Call)return false;
    Check(result==e::Result::Call && a.handler.establisher==c.gpr[4]+48 && a.frame.gpr[3]==0xBEEF);
    if(!inject){Check(!d.UnwindTo(f.Api(),&f,c.gpr[4]+1,f.Base()+0x250,99));Check(!d.UnwindTo(f.Api(),&f,c.gpr[4]+48,0x100000,99));Check(!d.UnwindTo(f.Api(),&f,c.gpr[4]+48,f.Base()+0x150,99));}
    const bool target=d.UnwindTo(f.Api(),&f,c.gpr[4]+48,f.Base()+0x250,99);if(inject && !target)return false;Check(target);
    result=d.Next(f.Api(),&f,a);if(inject && result!=e::Result::Call)return false;Check(result==e::Result::Call && a.record.flags==e::Unwinding);
    Check(!d.Reply(0,c));Check(d.Reply(1,c));result=d.Next(f.Api(),&f,a);if(inject && result!=e::Result::Call)return false;
    Check(result==e::Result::Call && a.record.flags==(e::Unwinding|e::TargetUnwind));Check(d.Reply(1,c));Check(d.Next(f.Api(),&f,a)==e::Result::Resume);
    const auto& end=d.ResumeContext();Check(end.rip==f.Base()+0x250 && end.gpr[4]==c.gpr[4]+48 && end.gpr[0]==99 && end.gpr[3]==0xBEEF);return true;
}
void Failures(){Fixture baseline;Check(Unwind(baseline));Fixture sweep;Check(Unwind(sweep,true));const auto operations=sweep.reads;
    for(uint32_t i=1;i<=operations;++i){Fixture f;f.fail=i;Check(!Unwind(f,true));}
    Fixture f;e::Dispatch d;e::Action a{};Check(d.Begin(f.Record(),f.Context(),f.Bounds()));
    Check(d.Next(f.Api(),&f,a)==e::Result::Call && d.Reply(1,f.Context()));Check(d.Next(f.Api(),&f,a)==e::Result::Call && d.Reply(1,f.Context()));
    Check(d.Next(f.Api(),&f,a)==e::Result::Unhandled);
    for(uint32_t kind=0;kind<5;++kind){e::Dispatch bad;auto r=f.Record();switch(kind){case 0:r.code=0;break;case 1:r.flags=2;break;case 2:r.nested=1;break;case 3:r.count=16;break;case 4:r.padding=1;break;}Check(!bad.Begin(r,f.Context(),f.Bounds()));}
    Fixture limit;for(uint32_t i=0;i<33;++i){limit.Saved(0x100+i*48+32,0xBEEF);limit.Saved(0x100+i*48+40,limit.Base()+0x140);}
    e::Dispatch bounded;e::Action action{};Check(bounded.Begin(limit.Record(),limit.Context(),limit.Bounds()));
    for(uint32_t i=0;i<32;++i)Check(bounded.Next(limit.Api(),&limit,action)==e::Result::Call && bounded.Reply(1,limit.Context()));
    Check(bounded.Next(limit.Api(),&limit,action)==e::Result::Limit);
    std::cout<<operations<<" injected dispatch/unwind read fault points PASS\n";
}
}
int main(){try{
#ifdef _WIN32
    static_assert(sizeof(CONTEXT)==sizeof(e::Context) && offsetof(CONTEXT,Rax)==offsetof(e::Context,gpr) && offsetof(CONTEXT,FltSave)==offsetof(e::Context,fx));
#endif
    Recovery();Failures();std::cout<<checks<<" exception dispatch/continuation/nonlocal unwind checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
