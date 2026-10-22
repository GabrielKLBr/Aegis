#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#include "../guest/Unwind.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace u=guardian::unwind;
namespace {
uint32_t checks=0,injected=0,scenarios=0;
#ifdef _WIN32
uint32_t comparisons=0;
#endif
void Check(bool ok,const char* what){
    ++checks;if(!ok){std::fprintf(stderr,"check %u: %s\n",checks,what);std::exit(1);}
}
struct Fixture {
    alignas(16) std::array<uint8_t,4096> image{};
    alignas(16) std::array<uint8_t,4096> stack{};
    uint32_t calls=0,fail=0,count=1,size=4096;bool executable=true,outside=false;
    Fixture(){image.fill(0x90);Table(0,0x100,0x200,0x500);Header(0x500,1,16,1,0);Node(0x504,4,2,3);Word(0x508,0x300);}
    uint64_t Base() const {return reinterpret_cast<uint64_t>(image.data());}
    uint64_t Low() const {return reinterpret_cast<uint64_t>(stack.data());}
    u::Stack Bounds() const {return {Low(),Low()+stack.size()};}
    void Word(uint32_t at,uint32_t value){std::memcpy(image.data()+at,&value,4);}
    void Table(uint32_t index,uint32_t begin,uint32_t end,uint32_t info){Word(0x400+index*12,begin);Word(0x404+index*12,end);Word(0x408+index*12,info);}
    void Header(uint32_t at,uint8_t flags,uint8_t prolog,uint8_t n,uint8_t frame){
        image[at]=static_cast<uint8_t>(1|(flags<<3));image[at+1]=prolog;image[at+2]=n;image[at+3]=frame;
    }
    void Node(uint32_t at,uint8_t offset,uint8_t op,uint8_t info){const auto value=static_cast<uint16_t>(offset|(op<<8)|(info<<12));std::memcpy(image.data()+at,&value,2);}
    void Saved(uint64_t at,uint64_t value){Check(at>=Low() && at<=Low()+stack.size()-8,"fixture stack bound");std::memcpy(reinterpret_cast<void*>(at),&value,8);}
    u::Context Context(){
        u::Context c{};for(uint32_t i=0;i<16;++i){c.gpr[i]=0xA000+i;for(uint32_t j=0;j<16;++j)c.xmm[i][j]=static_cast<uint8_t>(i*16+j);}
        c.rip=Base()+0x140;c.gpr[4]=Low()+0x100;Saved(c.gpr[4]+32,0x1234);return c;
    }
    static bool Image(void* raw,uint64_t pc,u::Image& out) noexcept {
        auto& f=*static_cast<Fixture*>(raw);if(f.outside || pc<f.Base() || pc>=f.Base()+f.size)return false;
        out={f.Base(),f.size,0x400,f.count};return true;
    }
    static bool Read(void* raw,uint64_t at,void* out,uint32_t n) noexcept {
        auto& f=*static_cast<Fixture*>(raw);++f.calls;if(f.fail==f.calls)return false;
        const bool image=at>=f.Base() && at<=f.Base()+f.size && n<=f.Base()+f.size-at;
        const bool stack=at>=f.Low() && at<=f.Low()+f.stack.size() && n<=f.Low()+f.stack.size()-at;
        if(!image && !stack)return false;std::memcpy(out,reinterpret_cast<void*>(at),n);return true;
    }
    static bool Executable(void* raw,uint64_t at) noexcept {
        auto& f=*static_cast<Fixture*>(raw);return f.executable && at>=f.Base()+0x100 && at<f.Base()+0x400;
    }
    u::Backend Api() const {return {Image,Read,Executable};}
};
u::Handler Sentinel(){return {1,2,3,4,5,6,7};}
bool Same(const u::Context& a,const u::Context& b){return !std::memcmp(&a,&b,sizeof(a));}
bool Same(const u::Handler& a,const u::Handler& b){return !std::memcmp(&a,&b,sizeof(a));}

void Oracle(Fixture& f,const u::Context& initial,u::HandlerType type,const u::Context& actual,const u::Handler& handler){
#ifdef _WIN32
    CONTEXT native{};native.ContextFlags=CONTEXT_ALL;
    DWORD64* regs[]={&native.Rax,&native.Rcx,&native.Rdx,&native.Rbx,&native.Rsp,&native.Rbp,&native.Rsi,&native.Rdi,&native.R8,&native.R9,&native.R10,&native.R11,&native.R12,&native.R13,&native.R14,&native.R15};
    for(uint32_t i=0;i<16;++i)*regs[i]=initial.gpr[i];native.Rip=initial.rip;
    std::memcpy(native.FltSave.XmmRegisters,initial.xmm,256);
    RUNTIME_FUNCTION fn{};
    for(uint32_t i=0;i<f.count;++i){RUNTIME_FUNCTION entry{};std::memcpy(&entry,f.image.data()+0x400+i*12,12);
        if(initial.rip>=f.Base()+entry.BeginAddress && initial.rip<f.Base()+entry.EndAddress){fn=entry;break;}}
    Check(fn.EndAddress!=0,"Windows differential entry exists");PVOID data=nullptr;DWORD64 frame=0;
    const auto routine=RtlVirtualUnwind(static_cast<DWORD>(type),f.Base(),initial.rip,&fn,&native,&data,&frame,nullptr);
    Check(actual.rip==native.Rip,"Windows caller RIP");
    for(uint32_t i=0;i<16;++i)Check(actual.gpr[i]==*regs[i],"Windows caller GPR");
    Check(!std::memcmp(actual.xmm,native.FltSave.XmmRegisters,256),"Windows caller XMM");
    Check(handler.routine==reinterpret_cast<uint64_t>(routine),"Windows handler selection");
    if(handler.routine){Check(handler.data==reinterpret_cast<uint64_t>(data),"Windows handler data");Check(handler.establisher==frame,"Windows establisher frame");}
    ++comparisons;
#else
    (void)f;(void)initial;(void)type;(void)actual;(void)handler;
#endif
}

void Accepted(Fixture& f,const u::Context& initial,u::HandlerType type,bool match,uint64_t frame=0,bool oracle=true){
    ++scenarios;f.calls=0;auto c=initial;auto h=Sentinel();
    Check(u::InspectHandler(f.Api(),&f,f.Bounds(),type,c,h)==u::Status::Ok,"valid handler frame");
    Check(c.gpr[4]>initial.gpr[4],"virtual stack advances");
    Check(c.rip==0x1234,"virtual return address is restored");
    Check(!std::memcmp(c.xmm,initial.xmm,sizeof(c.xmm)),"unmodified SIMD registers survive inspection");
    if(match){Check(h.routine==f.Base()+0x300 && h.data==f.Base()+0x50C,"handler and aligned trailer");
        Check(h.establisher==(frame?frame:initial.gpr[4]),"establisher uses original fixed allocation");
        Check(h.imageBase==f.Base() && h.functionBegin==f.Base()+0x100 && h.functionEnd==f.Base()+0x200 && h.unwindInfo==f.Base()+0x500,"primary function identity");
    }else Check(Same(h,u::Handler{}),"ineligible frame has no handler metadata");
    const auto reads=f.calls;
    if(oracle)Oracle(f,initial,type,c,h);
    for(uint32_t point=1;point<=reads;++point){f.calls=0;f.fail=point;auto failed=initial;auto output=Sentinel();
        Check(u::InspectHandler(f.Api(),&f,f.Bounds(),type,failed,output)!=u::Status::Ok,"read fault is reported");
        Check(Same(initial,failed) && Same(output,Sentinel()),"read fault never publishes partial context or metadata");++injected;}
    f.fail=0;
}

void Selection(){
    for(uint8_t flags=1;flags<=3;++flags)for(auto type:{u::HandlerType::Exception,u::HandlerType::Termination}){
        // Include odd code count, padding and every prolog boundary.
        for(uint32_t pc=0;pc<=20;++pc){Fixture f;auto c=f.Context();f.Header(0x500,flags,16,1,0);c.rip=f.Base()+0x100+pc;
            if(pc<4)f.Saved(c.gpr[4],0x1234);
            Accepted(f,c,type,pc>=16 && (flags&static_cast<uint32_t>(type))!=0);
        }
    }
    // Zero-code header has no padding before its trailer.
    {Fixture f;auto c=f.Context();f.Header(0x500,1,0,0,0);f.Word(0x504,0x300);f.Saved(c.gpr[4],0x1234);
        u::Handler h{};Check(u::InspectHandler(f.Api(),&f,f.Bounds(),u::HandlerType::Exception,c,h)==u::Status::Ok && h.data==f.Base()+0x508,"zero-code handler trailer");}
    // Language data is optional: its pointer may be one past the image.
    {Fixture f;auto c=f.Context();f.Word(0x408,0xFF8);f.Header(0xFF8,1,0,0,0);f.Word(0xFFC,0x300);f.Saved(c.gpr[4],0x1234);
        u::Handler h{};Check(u::InspectHandler(f.Api(),&f,f.Bounds(),u::HandlerType::Exception,c,h)==u::Status::Ok && h.data==f.Base()+0x1000,"optional language data at image end");}
    // Even code count, restore a pushed nonvolatile after the allocation.
    {Fixture f;auto c=f.Context();f.Header(0x500,1,16,2,0);f.Node(0x504,8,2,3);f.Node(0x506,1,0,3);f.Saved(c.gpr[4]+32,0xBEEF);f.Saved(c.gpr[4]+40,0x1234);
        Accepted(f,c,u::HandlerType::Exception,true);}
    // Frame pointer is not the establisher until SET_FPREG has executed.
    for(uint32_t pc=0;pc<=20;++pc){Fixture f;auto c=f.Context();f.Header(0x500,1,16,2,0x25);f.Node(0x504,12,3,0);f.Node(0x506,4,2,3);
        c.rip=f.Base()+0x100+pc;c.gpr[5]=pc>=12?c.gpr[4]+0x120:0xBAD;
        const auto fixed=pc>=12?c.gpr[5]-32:c.gpr[4];f.Saved(fixed+(pc>=4?32:0),0x1234);
        Accepted(f,c,u::HandlerType::Exception,pc>=16,fixed);
    }
    // No matching flag, no table entry, and no language handler.
    {Fixture f;auto c=f.Context();f.Header(0x500,0,16,1,0);Accepted(f,c,u::HandlerType::Exception,false);}
    {Fixture f;auto c=f.Context();f.count=0;f.Saved(c.gpr[4],0x1234);Accepted(f,c,u::HandlerType::Exception,false,0,false);}
}

void EpilogsAndChains(){
    const uint8_t epilog[]={0x48,0x83,0xC4,0x20,0xC3};
    for(uint32_t pc:{0u,4u}){Fixture f;auto c=f.Context();std::memcpy(f.image.data()+0x180,epilog,sizeof(epilog));c.rip=f.Base()+0x180+pc;
        if(pc)c.gpr[4]+=32;Accepted(f,c,u::HandlerType::Exception,false);}
    for(uint32_t depth:{1u,2u}){Fixture f;auto c=f.Context();f.count=depth+1;
        for(uint32_t i=1;i<=depth;++i){const auto info=0x500+i*0x100;f.Table(i,0x220+(i-1)*0x60,0x260+(i-1)*0x60,info);
            f.Header(info,4,8,0,0);std::memcpy(f.image.data()+info+4,f.image.data()+0x400+(i-1)*12,12);}
        c.rip=f.Base()+0x230+(depth-1)*0x60;Accepted(f,c,u::HandlerType::Exception,true);
    }
    // A secondary prolog must not expose its parent's handler.
    {Fixture f;auto c=f.Context();f.count=2;f.Table(1,0x220,0x260,0x600);f.Header(0x600,4,8,0,0);
        std::memcpy(f.image.data()+0x604,f.image.data()+0x400,12);c.rip=f.Base()+0x224;
        Accepted(f,c,u::HandlerType::Exception,false,0,false);}
    // Exercise the exact traversal budget, not only short chains.
    for(uint32_t length:{u::MaxChain,u::MaxChain+1}){
        Fixture f;auto c=f.Context();f.count=length;
        f.Table(0,0x100,0x108,0x800);f.Header(0x800,1,4,1,0);f.Node(0x804,4,2,3);f.Word(0x808,0x300);
        for(uint32_t i=1;i<length;++i){const auto info=0x800+i*16;f.Table(i,0x100+i*8,0x108+i*8,info);
            f.Header(info,4,0,0,0);std::memcpy(f.image.data()+info+4,f.image.data()+0x400+(i-1)*12,12);}
        c.rip=f.Base()+0x104+(length-1)*8;const auto initial=c;auto h=Sentinel();
        const auto status=u::InspectHandler(f.Api(),&f,f.Bounds(),u::HandlerType::Exception,c,h);
        if(length==u::MaxChain)Check(status==u::Status::Ok && c.rip==0x1234 && h.routine==f.Base()+0x300 && h.unwindInfo==f.Base()+0x800,"last permitted chain entry yields primary handler");
        else Check(status==u::Status::Limit && Same(c,initial) && Same(h,Sentinel()),"excessive chain does not publish a prefix");
    }
}

void Rejected(){
    for(uint32_t kind=0;kind<16;++kind){Fixture f;auto c=f.Context();auto api=f.Api();auto h=Sentinel();auto type=u::HandlerType::Exception;
        switch(kind){
        case 0:f.Word(0x508,0);break;case 1:f.Word(0x508,0x1000);break;
        case 2:f.Word(0x508,0x500);break;case 3:f.executable=false;break;
        case 4:api.executable=nullptr;break;case 5:type=static_cast<u::HandlerType>(3);break;
        case 6:f.Header(0x500,5,16,1,0);break;case 7:f.Header(0x500,6,16,1,0);break;
        case 8:f.Header(0x500,7,16,1,0);break;case 9:f.Header(0x500,8,16,1,0);break;
        case 10:f.image[0x500]=10;break;case 11:f.size=0x50B;break;
        case 12:f.Node(0x504,4,0,4);break;case 13:c.gpr[4]=f.Bounds().high-8;break;
        case 14:api.read=nullptr;break;case 15:c.gpr[4]|=1;break;
        }
        const auto initial=c;Check(u::InspectHandler(api,&f,f.Bounds(),type,c,h)!=u::Status::Ok,"invalid candidate refused");
        Check(Same(c,initial) && Same(h,Sentinel()),"rejected candidate leaves both outputs intact");
    }
    // A successful metadata read must not leak if the final stack check fails.
    {Fixture f;auto c=f.Context();f.Header(0x500,1,16,2,0x25);f.Node(0x504,12,3,0);f.Node(0x506,4,2,3);
        c.gpr[5]=c.gpr[4]-0x20;f.Saved(c.gpr[5],0x1234);const auto initial=c;auto h=Sentinel();
        Check(u::InspectHandler(f.Api(),&f,f.Bounds(),u::HandlerType::Exception,c,h)==u::Status::Invalid && Same(c,initial) && Same(h,Sentinel()),"late failure is transactional");}
    // The production stack-capture entry still refuses language handlers.
    {Fixture f;auto c=f.Context();const auto initial=c;Check(u::Step(f.Api(),&f,f.Bounds(),c)==u::Status::Unsupported && Same(c,initial),"Step compatibility retained");}
    {Fixture f;auto c=f.Context();const auto initial=c;auto h=Sentinel();f.outside=true;
        Check(u::InspectHandler(f.Api(),&f,f.Bounds(),u::HandlerType::Exception,c,h)==u::Status::End && Same(c,initial) && Same(h,Sentinel()),"End leaves outputs intact");}
}
}
int main(){
    Selection();EpilogsAndChains();Rejected();
    std::printf("%u handler metadata checks PASS; %u valid scenarios; %u injected read fault points PASS\n",checks,scenarios,injected);
#ifdef _WIN32
    std::printf("%u Windows handler differential frames PASS\n",comparisons);
#else
    std::puts("Windows differential validation NOT RUN (requires native Windows x64)");
#endif
}
