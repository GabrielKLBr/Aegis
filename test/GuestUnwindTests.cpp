#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <Windows.h>
#endif
#include "../guest/Unwind.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace u=guardian::unwind;
namespace {
uint32_t checks=0,comparisons=0,injected=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"check %u: %s\n",checks,what);std::exit(1);}}
struct Fixture {
    alignas(16) std::array<uint8_t,4096> image{};
    alignas(16) std::array<uint8_t,16384> stack{};
    uint32_t calls=0,fail=0,count=1;bool outside=false;
    Fixture(){image.fill(0x90);Table(0,0x100,0x200,0x500);Header(0x500,1,16,0,0);}
    uint64_t Base() const {return reinterpret_cast<uint64_t>(image.data());}
    uint64_t Low() const {return reinterpret_cast<uint64_t>(stack.data());}
    u::Stack Bounds() const {return {Low(),Low()+stack.size()};}
    void Put(uint32_t at,uint16_t value){std::memcpy(image.data()+at,&value,2);}
    void Word(uint32_t at,uint32_t value){std::memcpy(image.data()+at,&value,4);}
    void Table(uint32_t at,uint32_t begin,uint32_t end,uint32_t info){Word(0x400+at*12,begin);Word(0x404+at*12,end);Word(0x408+at*12,info);}
    void Header(uint32_t at,uint8_t v,uint8_t prolog,uint8_t n,uint8_t frame){image[at]=v;image[at+1]=prolog;image[at+2]=n;image[at+3]=frame;}
    void Node(uint32_t at,uint8_t offset,uint8_t op,uint8_t info){Put(0x504+at*2,static_cast<uint16_t>(offset|(op<<8)|(info<<12)));}
    void Saved(uint64_t at,uint64_t value){Check(at>=Low() && at+8<=Low()+stack.size(),"test stack bounds");std::memcpy(reinterpret_cast<void*>(at),&value,8);}
    u::Context Context(){u::Context c{};for(uint32_t i=0;i<16;++i){c.gpr[i]=0xA000+i;for(uint32_t j=0;j<16;++j)c.xmm[i][j]=static_cast<uint8_t>(i*16+j);}
        c.gpr[4]=Low()+0x100;c.rip=Base()+0x140;return c;}
    static bool Image(void* raw,uint64_t pc,u::Image& out) noexcept {auto& f=*static_cast<Fixture*>(raw);
        if(f.outside || pc<f.Base() || pc>=f.Base()+f.image.size())return false;out={f.Base(),static_cast<uint32_t>(f.image.size()),0x400,f.count};return true;}
    static bool Read(void* raw,uint64_t at,void* out,uint32_t n) noexcept {auto& f=*static_cast<Fixture*>(raw);++f.calls;if(f.fail && f.calls==f.fail)return false;
        const bool image=at>=f.Base() && at<=f.Base()+f.image.size() && n<=f.Base()+f.image.size()-at;
        const bool stack=at>=f.Low() && at<=f.Low()+f.stack.size() && n<=f.Low()+f.stack.size()-at;
        if(!image && !stack)return false;std::memcpy(out,reinterpret_cast<void*>(at),n);return true;}
    u::Backend Api() const {return {Image,Read};}
};
void Oracle(Fixture& f,const u::Context& initial){
    f.calls=0;auto actual=initial;Check(u::Step(f.Api(),&f,f.Bounds(),actual)==u::Status::Ok,"virtual unwind accepted valid frame");
    const auto reads=f.calls;for(uint32_t fault=1;fault<=reads;++fault){
        f.calls=0;f.fail=fault;auto failed=initial;Check(u::Step(f.Api(),&f,f.Bounds(),failed)!=u::Status::Ok,"differential read failure rejected");
        Check(!std::memcmp(&failed,&initial,sizeof(failed)),"differential failure context unchanged");++injected;}f.fail=0;
    #ifdef _WIN32
    CONTEXT native{};native.ContextFlags=CONTEXT_ALL;
    DWORD64* regs[]={&native.Rax,&native.Rcx,&native.Rdx,&native.Rbx,&native.Rsp,&native.Rbp,&native.Rsi,&native.Rdi,&native.R8,&native.R9,&native.R10,&native.R11,&native.R12,&native.R13,&native.R14,&native.R15};
    for(uint32_t i=0;i<16;++i)*regs[i]=initial.gpr[i];native.Rip=initial.rip;std::memcpy(native.FltSave.XmmRegisters,initial.xmm,256);
    RUNTIME_FUNCTION fn{};for(uint32_t i=0;i<f.count;++i){RUNTIME_FUNCTION candidate{};std::memcpy(&candidate,f.image.data()+0x400+i*12,12);
        if(initial.rip>=f.Base()+candidate.BeginAddress && initial.rip<f.Base()+candidate.EndAddress){fn=candidate;break;}}
    Check(fn.EndAddress!=0,"oracle function exists");PVOID handler=nullptr;DWORD64 establisher=0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,f.Base(),initial.rip,&fn,&native,&handler,&establisher,nullptr);
    Check(actual.rip==native.Rip,"Windows RIP agrees");for(uint32_t i=0;i<16;++i){if(actual.gpr[i]!=*regs[i])std::fprintf(stderr,"frame %u register %u: got %llx expected %llx\n",comparisons,i,actual.gpr[i],*regs[i]);Check(actual.gpr[i]==*regs[i],"Windows GPR agrees");}
    Check(!std::memcmp(actual.xmm,native.FltSave.XmmRegisters,256),"Windows XMM agrees");
    #endif
    ++comparisons;
}
void Body(){
    for(uint8_t n=0;n<16;++n){Fixture f;auto c=f.Context();f.Header(0x500,1,16,1,0);f.Node(0,4,2,n);f.Saved(c.gpr[4]+n*8+8,0x1234);Oracle(f,c);}
    for(uint8_t reg:std::array<uint8_t,8>{3,5,6,7,12,13,14,15}){Fixture f;auto c=f.Context();f.Header(0x500,1,16,2,0);f.Node(0,8,2,3);f.Node(1,1,0,reg);
        f.Saved(c.gpr[4]+32,0xABCDEF);f.Saved(c.gpr[4]+40,0x1234);Oracle(f,c);}
    for(uint8_t form=0;form<2;++form){Fixture f;auto c=f.Context();f.Header(0x500,1,16,form?3:2,0);f.Node(0,8,1,form);
        f.Put(0x506,form?0x800:0x100);if(form)f.Put(0x508,0);f.Saved(c.gpr[4]+0x800,0x1234);Oracle(f,c);}
    // Saves on either side of the fixed allocation verify offsets remain relative
    // to the fixed allocation even after undoing a stack allocation.
    for(bool early:{false,true})for(uint8_t op:std::array<uint8_t,4>{4,5,8,9}){
        Fixture f;auto c=f.Context();const bool isFar=op==5 || op==9,simd=op==8 || op==9;const uint8_t extra=isFar?2:1;
        f.Header(0x500,1,16,static_cast<uint8_t>(2+extra),0);
        if(early){f.Node(0,8,2,3);f.Node(1,4,op,simd?6:3);f.Put(0x508,isFar?16:(simd?1:2));if(isFar)f.Put(0x50A,0);}
        else {f.Node(0,8,op,simd?6:3);f.Put(0x506,isFar?16:(simd?1:2));if(isFar)f.Put(0x508,0);f.Node(1+extra,4,2,3);}
        const auto at=c.gpr[4]+16;f.Saved(at,0xFEDCBA);if(simd)f.Saved(at+8,0x10203040);f.Saved(c.gpr[4]+32,0x1234);Oracle(f,c);
    }
    // Frame pointer, dynamic stack below the fixed allocation, saved GPR/XMM.
    {Fixture f;auto c=f.Context();f.Header(0x500,1,16,6,0x25);f.Node(0,14,8,9);f.Put(0x506,1);f.Node(2,12,4,12);f.Put(0x50A,0);
        f.Node(4,8,3,0);f.Node(5,4,2,7);c.gpr[5]=c.gpr[4]+0x120;const auto fixed=c.gpr[5]-32;
        f.Saved(fixed,0x4567);f.Saved(fixed+16,0x12345678);f.Saved(fixed+24,0xABCDEF01);f.Saved(fixed+64,0x1234);Oracle(f,c);}
    // The prolog is inspected at every instruction boundary, including before
    // the first stack change; later unwind nodes must not run too soon.
    for(uint32_t pc=0;pc<12;++pc){Fixture f;auto c=f.Context();f.Header(0x500,1,12,2,0);f.Node(0,8,2,3);f.Node(1,1,0,3);c.rip=f.Base()+0x100+pc;
        const uint64_t alloc=pc>=8?32:0,push=pc>=1?8:0;if(push)f.Saved(c.gpr[4]+alloc,0xDEAD);f.Saved(c.gpr[4]+alloc+push,0x1234);Oracle(f,c);}
    for(uint8_t error=0;error<2;++error){Fixture f;auto c=f.Context();f.Header(0x500,1,1,1,0);f.Node(0,1,10,error);
        f.Saved(c.gpr[4]+error*8,0x1234);f.Saved(c.gpr[4]+error*8+24,c.gpr[4]+0x100);Oracle(f,c);}
    {Fixture f;f.count=3;f.Table(1,0x220,0x260,0x600);f.Table(2,0x280,0x300,0x700);f.Header(0x500,1,8,1,0);f.Node(0,4,2,3);
        f.Header(0x600,33,8,0,0);std::memcpy(f.image.data()+0x604,f.image.data()+0x400,12);
        f.Header(0x700,33,8,0,0);std::memcpy(f.image.data()+0x704,f.image.data()+0x40C,12);auto c=f.Context();c.rip=f.Base()+0x290;f.Saved(c.gpr[4]+32,0x1234);Oracle(f,c);}
}
void Epilogs(){
    const uint8_t epilog[]={0x48,0x83,0xC4,0x20,0x5B,0x41,0x5C,0xC3};
    for(uint32_t at:std::array<uint32_t,4>{0,4,5,7}){Fixture f;f.Header(0x500,1,16,3,0);f.Node(0,8,2,3);f.Node(1,2,0,12);f.Node(2,1,0,3);
        std::memcpy(f.image.data()+0x180,epilog,sizeof(epilog));auto c=f.Context();c.rip=f.Base()+0x180+at;
        const auto base=c.gpr[4];f.Saved(base+32,0xBEEF);f.Saved(base+40,0xCAFE);f.Saved(base+48,0x1234);
        if(at>=4)c.gpr[4]+=32;if(at>=5){c.gpr[3]=0xBEEF;c.gpr[4]+=8;}if(at>=7){c.gpr[12]=0xCAFE;c.gpr[4]+=8;}Oracle(f,c);}
    const std::array<std::array<uint8_t,8>,5> endings={{{0x48,0x81,0xC4,0x20,0,0,0,0xC3},{0x48,0x8D,0x65,0xE0,0xC3},{0xC2,0x10,0},{0xF3,0xC3},{0xE9,0x7B,0,0,0}}};
    for(uint32_t i=0;i<endings.size();++i){Fixture f;auto c=f.Context();f.Header(0x500,1,8,1,i==1?0x25:0);f.Node(0,4,2,3);
        std::memcpy(f.image.data()+0x180,endings[i].data(),8);c.rip=f.Base()+0x180;const auto adjust=i<2?32:0;c.gpr[5]=c.gpr[4]+64;f.Saved(c.gpr[4]+adjust,0x1234);Oracle(f,c);}
}
void Failures(){
    Fixture f;auto c=f.Context();f.Header(0x500,1,16,2,0);f.Node(0,8,2,3);f.Node(1,1,0,3);f.Saved(c.gpr[4]+32,0xABC);f.Saved(c.gpr[4]+40,0x1234);
    auto work=c;Check(u::Step(f.Api(),&f,f.Bounds(),work)==u::Status::Ok,"fault sweep control");const auto reads=f.calls;
    for(uint32_t at=1;at<=reads;++at){f.calls=0;f.fail=at;work=c;Check(u::Step(f.Api(),&f,f.Bounds(),work)!=u::Status::Ok,"injected memory failure rejected");Check(!std::memcmp(&work,&c,sizeof(c)),"context atomic on memory failure");}f.fail=0;
    std::printf("%u virtual unwind read fault points checked\n",reads);
    for(uint32_t kind=0;kind<18;++kind){Fixture x;auto original=x.Context();x.Header(0x500,1,16,1,0);x.Node(0,4,2,3);x.Saved(original.gpr[4]+32,0x1234);
        switch(kind){case 0:x.image[0x500]=2;break;case 1:x.image[0x500]=9;break;case 2:x.image[0x502]=65;break;case 3:x.image[0x503]=0x10;break;
        case 4:x.Node(0,17,2,3);break;case 5:x.Node(0,4,0,4);break;case 6:x.Node(0,4,8,5);break;case 7:x.Node(0,4,1,2);break;
        case 8:x.Node(0,4,5,3);break;case 9:x.Node(0,4,3,0);break;case 10:x.Node(0,4,6,0);break;case 11:x.Word(0x408,0xFFE);break;
        case 12:original.gpr[4]=x.Bounds().high-8;break;case 13:original.gpr[4]=x.Bounds().low-8;break;case 14:original.gpr[4]|=1;break;
        case 15:x.image[0x500]=33;std::memcpy(x.image.data()+0x508,x.image.data()+0x400,12);break;case 16:x.Word(0x404,0x100);break;
        case 17:x.Node(0,4,10,1);x.Saved(original.gpr[4]+8,0x1234);x.Saved(original.gpr[4]+32,original.gpr[4]-8);break;}
        work=original;Check(u::Step(x.Api(),&x,x.Bounds(),work)!=u::Status::Ok,"malformed frame rejected");Check(!std::memcmp(&work,&original,sizeof(work)),"malformed context unchanged");}
    // Trace stops at an external bootstrap, skips bounded callers, hashes
    // returned addresses and never exposes partial results on a later fault.
    {Fixture x;x.count=0;auto original=x.Context();for(uint32_t i=0;i<5;++i)x.Saved(original.gpr[4]+i*8,i==4?0:x.Base()+0x150+i);
        uint64_t frames[16]{};uint32_t n=99,hash=99;Check(u::Capture(x.Api(),&x,x.Bounds(),original,1,16,frames,n,hash)==u::Status::Ok && n==3,"capture skip and normal end");
        Check(frames[0]==x.Base()+0x151 && hash==static_cast<uint32_t>(3*x.Base()+0x151+0x152+0x153),"capture addresses and additive hash");
        const auto calls=x.calls;x.calls=0;x.fail=calls;for(auto& frame:frames)frame=0xDEAD;
        Check(u::Capture(x.Api(),&x,x.Bounds(),original,1,16,frames,n,hash)!=u::Status::Ok && !n && !hash,"trace failure clears outputs");
        for(auto frame:frames)Check(frame==0xDEAD,"failed trace never publishes prefix");x.fail=0;
        Check(u::Capture(x.Api(),&x,x.Bounds(),original,33,1,frames,n,hash)==u::Status::Invalid,"skip bound");
        Check(u::Capture(x.Api(),&x,x.Bounds(),original,0,17,frames,n,hash)==u::Status::Invalid,"capture bound");
        Check(u::Capture(x.Api(),&x,x.Bounds(),original,0,0,frames,n,hash)==u::Status::Invalid,"zero bound");
        work=original;x.outside=true;Check(u::Step(x.Api(),&x,x.Bounds(),work)==u::Status::End && !std::memcmp(&work,&original,sizeof(work)),"outside image is terminal");}
}
}
namespace {
void ExtendedFunctionTable(){
    struct Large {
        alignas(16) std::array<uint8_t,8192> image{};
        alignas(16) std::array<uint64_t,64> stack{};
        uint32_t count=guardian::pe::MaxRuntimeFunctions;
        uint64_t Base() const {return reinterpret_cast<uint64_t>(image.data());}
        uint64_t Low() const {return reinterpret_cast<uint64_t>(stack.data());}
    } fixture;
    for(uint32_t i=0;i<fixture.count;++i){const uint32_t entry[]={0x100+i*2,0x102+i*2,0x1100};
        std::memcpy(fixture.image.data()+0x400+i*12,entry,sizeof(entry));}
    fixture.image[0x1100]=1;fixture.stack[0]=0x1234;
    const u::Backend backend{
        [](void* raw,uint64_t pc,u::Image& out) noexcept {
            auto& f=*static_cast<Large*>(raw);if(pc<f.Base() || pc>=f.Base()+f.image.size())return false;
            out={f.Base(),static_cast<uint32_t>(f.image.size()),0x400,f.count};return true;
        },
        [](void* raw,uint64_t at,void* out,uint32_t bytes) noexcept {
            auto& f=*static_cast<Large*>(raw);
            const bool image=at>=f.Base() && at<=f.Base()+f.image.size() && bytes<=f.Base()+f.image.size()-at;
            const bool stack=at>=f.Low() && at<=f.Low()+sizeof(f.stack) && bytes<=f.Low()+sizeof(f.stack)-at;
            if(!image&&!stack)return false;std::memcpy(out,reinterpret_cast<const void*>(at),bytes);return true;
        }};
    const u::Stack bounds{fixture.Low(),fixture.Low()+sizeof(fixture.stack)};
    u::Context original{};original.rip=fixture.Base()+0x100+200*2;original.gpr[4]=fixture.Low();
    auto actual=original;Check(u::Step(backend,&fixture,bounds,actual)==u::Status::Ok && actual.rip==0x1234 &&
                              actual.gpr[4]==fixture.Low()+8,"256-function table was truncated or rejected");
    ++fixture.count;actual=original;
    Check(u::Step(backend,&fixture,bounds,actual)==u::Status::Invalid && !std::memcmp(&actual,&original,sizeof(actual)),
          "Oversize function table accepted or changed context");
}
}
int main(){Body();Epilogs();Failures();ExtendedFunctionTable();
#ifdef _WIN32
    std::printf("%u freestanding virtual unwind checks PASS; %u Windows differential frames PASS\n",checks,comparisons);
#else
    std::printf("%u portable virtual unwind checks PASS; %u synthetic scenarios\n",checks,comparisons);
    std::puts("Windows differential validation NOT RUN (requires native Windows x64)");
#endif
    std::printf("%u differential virtual unwind read fault points checked\n",injected);
}
