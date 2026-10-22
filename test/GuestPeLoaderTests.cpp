#include "../guest/PeLoader.h"
#include "PeFixtures.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace guardian::pe;
namespace {
uint32_t checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
uint16_t U16(const std::vector<uint8_t>& b,uint32_t p){return uint16_t(b.at(p)|(uint16_t(b.at(p+1))<<8));}
uint32_t U32(const std::vector<uint8_t>& b,uint32_t p){return uint32_t(U16(b,p))|(uint32_t(U16(b,p+2))<<16);}
uint64_t U64(const std::vector<uint8_t>& b,uint32_t p){return uint64_t(U32(b,p))|(uint64_t(U32(b,p+4))<<32);}
void Put(std::vector<uint8_t>& b,uint32_t p,uint64_t v,uint32_t n=4){for(uint32_t i=0;i<n;++i)b.at(p+i)=uint8_t(v>>(i*8));}
uint32_t Opt(const std::vector<uint8_t>& b){return U32(b,60)+24;}
uint32_t Dir(const std::vector<uint8_t>& b,uint32_t i){return Opt(b)+112+i*8;}
uint32_t Rva(const std::vector<uint8_t>& b,uint32_t r){
    const auto o=Opt(b),table=o+U16(b,o-4);for(uint32_t i=0;i<U16(b,o-18);++i){const auto s=table+i*40,va=U32(b,s+12),n=U32(b,s+16);
        if(r>=va && r-va<n)return U32(b,s+20)+r-va;}if(r<U32(b,o+60))return r;throw std::runtime_error("Fixture RVA unavailable");
}
uint32_t Thunk(const std::vector<uint8_t>& b,bool ordinal){const auto d=Rva(b,U32(b,Dir(b,1))),t=Rva(b,U32(b,d));
    for(uint32_t i=0;i<2;++i)if(bool(U64(b,t+i*8)>>63)==ordinal)return t+i*8;throw std::runtime_error("Fixture import kind unavailable");}
struct Input {
    std::vector<uint8_t> app{std::begin(fixtures::App),std::end(fixtures::App)},dll{std::begin(fixtures::Dll),std::end(fixtures::Dll)};
    Source sources[2]{};
    void Refresh(){sources[0]={"guardian-pe-console.exe",app.data(),static_cast<uint32_t>(app.size())};sources[1]={"guardian-pe-runtime.dll",dll.data(),static_cast<uint32_t>(dll.size())};}
};
struct Memory {
    static constexpr uint64_t Base=0x40040000;
    uint8_t data[MaxModules][MaxImageBytes]{};Protection pages[MaxModules][MaxImageBytes/4096]{};
    uint32_t sizes[MaxModules]{};int64_t budget=-1;uint32_t steps=0,maps=0,unmaps=0,unmapOrder[32]{};bool failUnmap=false;
    bool Step() noexcept {++steps;if(budget==0){budget=-1;return false;}if(budget>0)--budget;return true;}
    uint32_t Slot(uint64_t at,uint32_t n) const noexcept {
        if(at<Base || at-Base>=uint64_t(MaxModules)*MaxImageBytes)return UINT32_MAX;
        const auto slot=static_cast<uint32_t>((at-Base)/MaxImageBytes),offset=static_cast<uint32_t>((at-Base)%MaxImageBytes);
        return sizes[slot] && offset<sizes[slot] && n && n<=sizes[slot]-offset?slot:UINT32_MAX;
    }
    bool Access(uint64_t at,uint32_t n,bool write) const noexcept {
        const auto slot=Slot(at,n);if(slot==UINT32_MAX)return false;const auto offset=static_cast<uint32_t>((at-Base)%MaxImageBytes);
        for(uint32_t page=offset/4096;page<=(offset+n-1)/4096;++page)if(pages[slot][page]==Protection::None || (write && pages[slot][page]!=Protection::ReadWrite))return false;
        return true;
    }
    static bool Map(void* p,uint64_t at,uint32_t n) noexcept {
        auto& m=*static_cast<Memory*>(p);if(!m.Step() || at<Base || at-Base>=uint64_t(MaxModules)*MaxImageBytes || ((at-Base)%MaxImageBytes) || !n || n>MaxImageBytes || (n&4095))return false;
        const auto i=static_cast<uint32_t>((at-Base)/MaxImageBytes);if(m.sizes[i])return false;m.sizes[i]=n;++m.maps;
        std::memset(m.data[i],0,n);for(uint32_t page=0;page<n/4096;++page)m.pages[i][page]=Protection::ReadWrite;return true;
    }
    static bool Read(void* p,uint64_t at,void* out,uint32_t n) noexcept {
        auto& m=*static_cast<Memory*>(p);if(!m.Step() || !m.Access(at,n,false))return false;
        std::memcpy(out,m.data[m.Slot(at,n)]+(at-Base)%MaxImageBytes,n);return true;
    }
    static bool Write(void* p,uint64_t at,const void* in,uint32_t n) noexcept {
        auto& m=*static_cast<Memory*>(p);if(!m.Step() || !m.Access(at,n,true))return false;
        std::memcpy(m.data[m.Slot(at,n)]+(at-Base)%MaxImageBytes,in,n);return true;
    }
    static bool Protect(void* p,uint64_t at,uint32_t n,Protection access) noexcept {
        auto& m=*static_cast<Memory*>(p);if(!m.Step() || (at&4095) || (n&4095))return false;const auto slot=m.Slot(at,n);if(slot==UINT32_MAX)return false;
        const auto offset=static_cast<uint32_t>((at-Base)%MaxImageBytes);for(uint32_t page=offset/4096;page<(offset+n)/4096;++page)m.pages[slot][page]=access;return true;
    }
    static bool Unmap(void* p,uint64_t at) noexcept {
        auto& m=*static_cast<Memory*>(p);if(m.failUnmap)return false;const auto slot=m.Slot(at,1);if(slot==UINT32_MAX || ((at-Base)%MaxImageBytes))return false;
        m.sizes[slot]=0;for(auto& page:m.pages[slot])page=Protection::None;if(m.unmaps<32)m.unmapOrder[m.unmaps]=slot;++m.unmaps;return true;
    }
    bool Empty() const {for(auto n:sizes)if(n)return false;return true;}
    static Backend Api(){return {Map,Read,Write,Protect,Unmap};}
};
bool Load(Input& input,Memory& mem,Loader& loader,uint32_t& index){input.Refresh();return loader.Configure(Memory::Api(),&mem,input.sources,2,Memory::Base) && loader.Load(input.sources[0].name,index);}
template<class F> void Reject(const char* name,F mutate){Input in;mutate(in);auto mem=std::make_unique<Memory>();Loader loader;uint32_t index=0;
    Check(!Load(in,*mem,loader,index),name);Check(index==UINT32_MAX,name);Check(mem->Empty() && mem->maps==mem->unmaps,name);Check(!loader.Get(0) && !loader.Get(1),name);}
void Successful(){
    Input in;auto mem=std::make_unique<Memory>();Loader loader;uint32_t index=0;Check(Load(in,*mem,loader,index),"Compiler PE pair rejected");
    const auto* a=loader.Get(0);const auto* d=loader.Get(1);Check(index==0 && a && d && a->imports==2 && !d->imports,"Dependency graph/import counts");
    Check(a->relocated && d->relocated && a->base!=a->preferred && d->base!=d->preferred,"Actual DIR64 rebase missing");
    Check(a->tlsBytes && d->tlsBytes && a->tlsCallbackCount==1 && d->tlsCallbackCount==1 && a->functions,"Compiler TLS/unwind metadata missing");
    for(uint32_t i=0;i<2;++i){const auto& m=*loader.Get(i);Check(loader.Executable(m.entry) && !loader.Executable(m.base) && !loader.Executable(m.tlsTemplate),"RX allowlist wrong");
        uint32_t tls=UINT32_MAX;Check(Memory::Read(mem.get(),m.tlsIndexAddress,&tls,4) && tls==i,"TLS index initialization");
        Check(!mem->Access(m.entry,1,true) && !mem->Access(m.base,1,true) && mem->Access(m.tlsIndexAddress,4,true),"W^X seal missing");
        const auto tlsRaw=Rva(i?in.dll:in.app,U32(i?in.dll:in.app,Dir(i?in.dll:in.app,9)));
        const auto expected=U64(i?in.dll:in.app,tlsRaw+24)-m.preferred+m.base;uint64_t callback=0;
        Check(Memory::Read(mem.get(),expected,&callback,8) && callback==m.tlsCallbacks[0] && loader.Executable(callback),"Relocated TLS callback wrong");
        for(uint32_t s=0;s<m.sectionCount;++s){const auto& section=m.sections[s];for(uint32_t b=section.rawBytes;b<section.bytes;++b){const auto at=m.base+section.rva+b;
            if(at<m.tlsIndexAddress || at>=m.tlsIndexAddress+4)Check(mem->data[i][section.rva+b]==0,"BSS not zeroed");}}}
    const auto desc=Rva(in.app,U32(in.app,Dir(in.app,1))),iat=U32(in.app,desc+16);uint64_t addresses[2]{};
    Check(Memory::Read(mem.get(),a->base+iat,addresses,16) && addresses[0]!=addresses[1],"Named/ordinal IAT not resolved");
    Check(addresses[0]>=d->base && addresses[0]<d->base+d->size && addresses[1]>=d->base && addresses[1]<d->base+d->size && loader.Executable(addresses[0]) && loader.Executable(addresses[1]),"IAT escaped guest DLL");
    const auto maps=mem->maps;Check(loader.Load("GUARDIAN-PE-CONSOLE.EXE",index) && index==0 && mem->maps==maps,"Case insensitive cache allocated duplicate");
    Check(!loader.Load("missing.dll",index) && loader.Failure()==Error::MissingModule && loader.Get(0) && loader.Get(1),"Missing library damaged loaded graph");
    Check(!loader.Configure(Memory::Api(),mem.get(),in.sources,2,Memory::Base) && loader.Failure()==Error::InvalidState,"Live reconfigure leaked mappings");
    mem->failUnmap=true;Check(!loader.UnloadAll() && loader.Failure()==Error::Access && !mem->Empty(),"Failed unmap was forgotten");mem->failUnmap=false;
    Check(loader.UnloadAll() && mem->Empty() && mem->maps==mem->unmaps,"Unload/rollback leak");Check(loader.UnloadAll(),"Empty unload failed");
}
void Malformed(){
    Reject("MZ",[](Input& i){i.app[0]=0;});
    Reject("NT offset overflow",[](Input& i){Put(i.app,60,0xFFFFFFF0);});
    Reject("NT inside DOS",[](Input& i){Put(i.app,60,0);});
    Reject("Wrong machine",[](Input& i){Put(i.app,Opt(i.app)-24+4,0x14C,2);});
    Reject("Optional header truncated",[](Input& i){Put(i.app,Opt(i.app)-4,239,2);});
    Reject("Wrong magic",[](Input& i){Put(i.app,Opt(i.app),0x10B,2);});
    Reject("No sections",[](Input& i){Put(i.app,Opt(i.app)-18,0,2);});
    Reject("Section quota",[](Input& i){Put(i.app,Opt(i.app)-18,17,2);});
    Reject("Stripped relocations",[](Input& i){Put(i.app,Opt(i.app)-2,U16(i.app,Opt(i.app)-2)|1,2);});
    Reject("Bad image size",[](Input& i){Put(i.app,Opt(i.app)+56,UINT32_MAX);});
    Reject("Bad headers size",[](Input& i){Put(i.app,Opt(i.app)+60,512);});
    Reject("Bad alignment",[](Input& i){Put(i.app,Opt(i.app)+32,512);});
    Reject("Noncanonical preferred base",[](Input& i){Put(i.app,Opt(i.app)+24,UINT64_MAX-0xFFFF,8);});
    Reject("No executable entry",[](Input& i){Put(i.app,Opt(i.app)+16,0);});
    Reject("Data entry",[](Input& i){Put(i.app,Opt(i.app)+16,U32(i.app,Dir(i.app,9)));});
    Reject("Directory quota",[](Input& i){Put(i.app,Opt(i.app)+108,17);});
    Reject("Directory outside image",[](Input& i){Put(i.app,Dir(i.app,1),UINT32_MAX);});
    Reject("Half directory",[](Input& i){Put(i.app,Dir(i.app,1)+4,0);});
    Reject("Delay imports unsupported",[](Input& i){Put(i.app,Dir(i.app,13),U32(i.app,Dir(i.app,1)));Put(i.app,Dir(i.app,13)+4,20);});
    Reject("Writable code",[](Input& i){const auto s=Opt(i.app)+U16(i.app,Opt(i.app)-4);Put(i.app,s+36,U32(i.app,s+36)|0x80000000);});
    Reject("Overlapping sections",[](Input& i){const auto s=Opt(i.app)+U16(i.app,Opt(i.app)-4);Put(i.app,s+40+12,U32(i.app,s+12));});
    Reject("Raw outside file",[](Input& i){const auto s=Opt(i.app)+U16(i.app,Opt(i.app)-4);Put(i.app,s+20,0xFFFFFE00);});
    Reject("Overlapping raw sections",[](Input& i){const auto s=Opt(i.app)+U16(i.app,Opt(i.app)-4);Put(i.app,s+40+20,U32(i.app,s+20));});
    Reject("Missing relocations",[](Input& i){Put(i.app,Dir(i.app,5),0,8);});
    Reject("Short relocation block",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,5)));Put(i.app,r+4,7);});
    Reject("Odd relocation block",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,5)));Put(i.app,r+4,9);});
    Reject("Overflow relocation block",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,5)));Put(i.app,r+4,0xFFFFFFFE);});
    Reject("Relocation outside image",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,5)));Put(i.app,r,0xFFFFF000);});
    Reject("Unsupported relocation",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,5)));Put(i.app,r+8,0x3000,2);});
    Reject("Duplicate relocation",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,5)));Put(i.app,r+10,U16(i.app,r+8),2);});
    Reject("Missing module",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,1)));i.app[Rva(i.app,U32(i.app,r+12))]='x';});
    Reject("Missing descriptor terminator",[](Input& i){Put(i.app,Dir(i.app,1)+4,20);});
    Reject("Import thunk outside image",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,1)));Put(i.app,r,0xFFFFFFF8);});
    Reject("IAT outside image",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,1)));Put(i.app,r+16,0xFFFFFFF8);});
    Reject("IAT inside headers",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,1)));Put(i.app,r+16,64);});
    Reject("IAT inside code",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,1))),s=Opt(i.app)+U16(i.app,Opt(i.app)-4);Put(i.app,r+16,U32(i.app,s+12));});
    Reject("Bound imports unsupported",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,1)));Put(i.app,r+4,1);});
    Reject("Symbol missing",[](Input& i){const auto th=Thunk(i.app,false);i.app[Rva(i.app,static_cast<uint32_t>(U64(i.app,th)))+2]='Z';});
    Reject("Empty symbol",[](Input& i){const auto th=Thunk(i.app,false);i.app[Rva(i.app,static_cast<uint32_t>(U64(i.app,th)))+2]=0;});
    Reject("Ordinal reserved bits",[](Input& i){Put(i.app,Thunk(i.app,true),0x8000000100000011ull,8);});
    Reject("Ordinal missing",[](Input& i){Put(i.app,Thunk(i.app,true),0x800000000000FFFFull,8);});
    Reject("Dependency is EXE",[](Input& i){Put(i.dll,Opt(i.dll)-2,U16(i.dll,Opt(i.dll)-2)&~0x2000u,2);});
    Reject("Dependency cycle",[](Input& i){i.dll=i.app;});
    Reject("Forwarder unsupported",[](Input& i){const auto rva=U32(i.dll,Dir(i.dll,0)),r=Rva(i.dll,rva);Put(i.dll,Rva(i.dll,U32(i.dll,r+28)),rva);});
    Reject("Export ordinal outside table",[](Input& i){const auto r=Rva(i.dll,U32(i.dll,Dir(i.dll,0)));Put(i.dll,Rva(i.dll,U32(i.dll,r+36)),0xFFFF,2);});
    Reject("TLS index read only",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,9)));Put(i.app,r+16,U64(i.app,Opt(i.app)+24)+U32(i.app,Opt(i.app)+16),8);});
    Reject("TLS template reversed",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,9)));Put(i.app,r+8,U64(i.app,r)-1,8);});
    Reject("TLS zero quota",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,9)));Put(i.app,r+32,257);});
    Reject("TLS callback not code",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,9))),cb=Rva(i.app,static_cast<uint32_t>(U64(i.app,r+24)-U64(i.app,Opt(i.app)+24)));Put(i.app,cb,U64(i.app,r),8);});
    Reject("TLS flags",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,9)));Put(i.app,r+36,0x80000000);});
    Reject("Exception table misaligned size",[](Input& i){Put(i.app,Dir(i.app,3)+4,13);});
    Reject("Exception range reversed",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,3)));Put(i.app,r+4,U32(i.app,r));});
    Reject("Unwind outside image",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,3)));Put(i.app,r+8,0xFFFFFFFC);});
    Reject("Chained unwind missing predecessor",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,3))),u=Rva(i.app,U32(i.app,r+8));i.app[u]=0x21;});
    Reject("Unwind operand truncated",[](Input& i){const auto r=Rva(i.app,U32(i.app,Dir(i.app,3))),u=Rva(i.app,U32(i.app,r+8));i.app[u+2]=1;i.app[u+5]=1;});
    Reject("Truncated DLL",[](Input& i){i.dll.pop_back();});
    Reject("Truncated EXE",[](Input& i){i.app.pop_back();});
    for(uint32_t n=0;n<64;++n){Input in;in.app.resize(n);auto mem=std::make_unique<Memory>();Loader loader;in.Refresh();Check(!loader.Configure(Memory::Api(),mem.get(),in.sources,2,Memory::Base),"Short DOS accepted");}
    for(uint32_t n=512;n<fixtures::Sources[0].bytes;n+=512)Reject("Truncated app section table/data",[&](Input& i){i.app.resize(n);});
    for(uint32_t n=512;n<fixtures::Sources[1].bytes;n+=512)Reject("Truncated DLL section table/data",[&](Input& i){i.dll.resize(n);});
}
void FailureInjection(){
    uint32_t failures=0;
    for(uint32_t point=0;point<10000;++point){Input in;auto mem=std::make_unique<Memory>();mem->budget=point;Loader loader;uint32_t index=0;
        const bool ok=Load(in,*mem,loader,index);if(ok){Check(loader.UnloadAll() && mem->Empty(),"Terminal injection cleanup");Check(point==mem->steps,"Injection skipped an operation");break;}
        ++failures;Check(mem->Empty() && mem->maps==mem->unmaps && !loader.Get(0) && !loader.Get(1),"Backend fault leaked mapping or published module");
        Check(index==UINT32_MAX && loader.Failure()!=Error::Ok,"Backend fault exposed success");
    }Check(failures>100 && failures<10000,"Incomplete backend fault sweep");
    std::cout<<failures<<" backend fault points checked\n";
    // Preserve a previously loaded DLL while a subsequent root load fails.
    Input in;in.Refresh();auto mem=std::make_unique<Memory>();Loader loader;uint32_t index=0;
    Check(loader.Configure(Memory::Api(),mem.get(),in.sources,2,Memory::Base) && loader.Load(in.sources[1].name,index) && index==1,"Preload DLL failed");
    mem->budget=3;Check(!loader.Load(in.sources[0].name,index) && loader.Get(1) && !loader.Get(0) && mem->sizes[1] && !mem->sizes[0],"Rollback damaged preloaded dependency");
    Check(loader.UnloadAll() && mem->Empty(),"Preloaded dependency cleanup");
    in.sources[1].name="GUARDIAN-PE-CONSOLE.EXE";Check(!loader.Configure(Memory::Api(),mem.get(),in.sources,2,Memory::Base),"Duplicate module alias accepted");
    in.Refresh();Check(!loader.Configure(Memory::Api(),mem.get(),in.sources,2,UINT64_MAX-0xFFFF),"Arena overflow accepted");
    in.sources[1].name="..\\escape.dll";Check(!loader.Configure(Memory::Api(),mem.get(),in.sources,2,Memory::Base),"Module path escape accepted");
    Input fallback;const auto desc=Rva(fallback.app,U32(fallback.app,Dir(fallback.app,1)));Put(fallback.app,desc,0);
    auto other=std::make_unique<Memory>();Loader withoutLookup;
    Check(Load(fallback,*other,withoutLookup,index) && withoutLookup.Get(0)->imports==2,"OriginalFirstThunk zero fallback broken");
    Check(withoutLookup.UnloadAll() && other->Empty(),"Fallback IAT leaked mappings");
}
// Build a bounded three-part chain in unused .reloc raw padding. No executable
// bytes change; these parser probes never execute the synthetic unwind records.
void Chained(Input& in,uint32_t mode){
    auto& b=in.app;const auto o=Opt(b),table=o+U16(b,o-4),last=table+(U16(b,o-18)-1)*40;
    const auto raw=U32(b,last+20),bytes=U32(b,last+16),base=U32(b,last+12),p=raw+bytes-112,rva=base+bytes-112;
    Check(bytes>=132,"Fixture relocation padding missing");const auto old=Rva(b,U32(b,Dir(b,3))),begin=U32(b,old),end=U32(b,old+4),u=U32(b,old+8),h=Rva(b,u);
    const auto split=begin+uint32_t(b[h+1])+1;Check(end>split+1,"Fixture function cannot split");
    Put(b,Dir(b,3),rva);Put(b,Dir(b,3)+4,36);
    Put(b,p,begin);Put(b,p+4,split);Put(b,p+8,u);
    Put(b,p+12,split);Put(b,p+16,split+1);Put(b,p+20,rva+40);
    Put(b,p+24,split+1);Put(b,p+28,end);Put(b,p+32,rva+56);
    for(uint32_t k=0;k<2;++k){const auto header=p+40+k*16;b[header]=0x21;b[header+1]=b[header+2]=0;b[header+3]=b[h+3];
        for(uint32_t n=0;n<12;++n)b[header+4+n]=b[p+k*12+n];}
    if(mode==1)for(uint32_t n=0;n<12;++n)b[p+44+n]=b[p+12+n]; // self-cycle
    if(mode==2)for(uint32_t n=0;n<12;++n)b[p+44+n]=b[p+24+n]; // forward edge
    if(mode==3)b[p+43]=b[h+3]?0:5; // incompatible frame register
    if(mode==4){Put(b,p+32,base+bytes-4);b[raw+bytes-4]=0x21;b[raw+bytes-3]=b[raw+bytes-2]=0;b[raw+bytes-1]=b[h+3];}
    if(mode==5)Put(b,p+52,0xFFFFFFFC); // parent unwind outside the image
    if(mode==6)b[p+40]=0x29; // chain plus language-handler flags
}
void ChainChecks(){
    Input in;Chained(in,0);auto mem=std::make_unique<Memory>();Loader loader;uint32_t index=0;
    Check(Load(in,*mem,loader,index) && loader.Get(0)->functions==3,"Valid multi-level unwind chain rejected");
    Check(loader.UnloadAll() && mem->Empty(),"Chained image cleanup failed");
    for(uint32_t mode=1;mode<=6;++mode)Reject("Malformed unwind chain",[&](Input& source){Chained(source,mode);});
    uint32_t failures=0;
    for(uint32_t point=0;point<10000;++point){Input source;Chained(source,0);auto backing=std::make_unique<Memory>();backing->budget=point;Loader candidate;
        if(Load(source,*backing,candidate,index)){Check(candidate.UnloadAll() && backing->Empty() && point==backing->steps,"Chained fault sweep cleanup");break;}
        ++failures;Check(backing->Empty() && backing->maps==backing->unmaps && !candidate.Get(0) && !candidate.Get(1) && index==UINT32_MAX,"Chained backend failure leaked mappings");}
    Check(failures>100 && failures<10000,"Chained backend sweep incomplete");std::cout<<failures<<" chained unwind fault points checked\n";
}
void DynamicChecks(){
    Input in;in.Refresh();const Source catalog[3]={in.sources[0],in.sources[1],{"guardian-extra.dll",in.dll.data(),static_cast<uint32_t>(in.dll.size())}};
    auto mem=std::make_unique<Memory>();Loader loader;uint32_t index=0,added=0,remove=0;
    Check(loader.Configure(Memory::Api(),mem.get(),catalog,3,Memory::Base) && loader.Load(catalog[0].name,index),"Dynamic baseline failed");
    Check(loader.Get(0)->dependencies==2 && loader.MappedMask()==3,"Import dependency graph missing");
    Check(loader.Acquire(catalog[2].name,index,added) && index==2 && added==4 && loader.References(2)==1,"Dynamic acquisition missing");
    const auto maps=mem->maps;Check(loader.Acquire("GUARDIAN-EXTRA.DLL",index,added) && !added && loader.References(2)==2 && maps==mem->maps,"Repeated load reinitialized or remapped");
    uint64_t address=0;Check(loader.Resolve(2,"GuardianAdd",0,address) && loader.Executable(address),"Dynamic named symbol missing");
    Check(loader.Resolve(2,nullptr,17,address) && loader.Executable(address),"Dynamic ordinal symbol missing");
    Check(!loader.Resolve(2,"missing",0,address) && !address,"Missing export succeeded");
    Check(!loader.Remove(4) && loader.Get(2),"Live DLL was removed");Check(!loader.Remove(2) && loader.Get(1),"Pinned dependency was removed");
    Check(loader.Release(2,remove) && !remove && loader.References(2)==1,"First release removed live DLL");
    Check(loader.Release(2,remove) && remove==4 && loader.References(2)==0,"Final release did not expose orphan");
    mem->failUnmap=true;Check(!loader.Remove(remove) && loader.Get(2),"Failed unmap lost ownership");mem->failUnmap=false;
    Check(loader.Remove(remove) && !loader.Get(2) && !loader.Resolve(2,"GuardianAdd",0,address),"Stale DLL exports survived unload");
    Check(!loader.Release(2,remove),"Stale release succeeded");
    Check(!loader.Acquire(catalog[0].name,index,added) && loader.Get(0) && loader.Get(1) && !loader.Get(2),"Executable acquired as a dynamic DLL");
    for(uint32_t i=0;i<64;++i)Check(loader.Acquire(catalog[2].name,index,added) && loader.References(2)==i+1,"Reference count did not advance");
    Check(!loader.Acquire(catalog[2].name,index,added) && loader.References(2)==64,"Reference limit exceeded");
    for(uint32_t i=0;i<64;++i)Check(loader.Release(2,remove),"Bounded reference release failed");
    Check(remove==4 && loader.Remove(remove) && loader.UnloadAll() && mem->Empty(),"Dynamic reference cleanup leaked");
    auto dependent=in.app;Put(dependent,Opt(dependent)-2,U16(dependent,Opt(dependent)-2)|0x2000,2);
    const Source graph[3]={in.sources[0],in.sources[1],{"guardian-dependent.dll",dependent.data(),static_cast<uint32_t>(dependent.size())}};
    auto graphMemory=std::make_unique<Memory>();Loader graphLoader;
    Check(graphLoader.Configure(Memory::Api(),graphMemory.get(),graph,3,Memory::Base) && graphLoader.Acquire(graph[2].name,index,added) && added==6,"Dynamic dependency graph did not load");
    Check(graphLoader.Get(2)->dependencies==2 && !graphLoader.Remove(2),"Dynamic dependency was not retained");
    Check(graphLoader.Release(2,remove) && remove==6 && graphLoader.Remove(remove) && graphMemory->Empty() && graphMemory->unmapOrder[0]==2 && graphMemory->unmapOrder[1]==1,"Dynamic teardown did not follow dependency order");
    uint32_t faults=0;
    for(uint32_t point=0;point<10000;++point){auto backing=std::make_unique<Memory>();Loader candidate;
        Check(candidate.Configure(Memory::Api(),backing.get(),catalog,3,Memory::Base) && candidate.Load(catalog[0].name,index),"Fault baseline failed");
        backing->steps=0;backing->budget=point;
        if(candidate.Acquire(catalog[2].name,index,added)){
            Check(backing->steps==point && candidate.Release(index,remove) && candidate.Remove(remove) && candidate.UnloadAll() && backing->Empty(),"Dynamic fault sweep incomplete");break;}
        ++faults;Check(index==UINT32_MAX && !added && candidate.Get(0) && candidate.Get(1) && !candidate.Get(2) && !candidate.References(2) && !backing->sizes[2],"Dynamic failure damaged a preloaded module");
        Check(candidate.UnloadAll() && backing->Empty(),"Failed dynamic load leaked mappings");}
    Check(faults>30 && faults<10000,"Dynamic sweep did not reach the full path");std::cout<<faults<<" dynamic mapping rollback fault points PASS\n";
}

}
int main(){try{Successful();Malformed();FailureInjection();ChainChecks();DynamicChecks();std::cout<<checks<<" freestanding PE loader checks PASS\n";return 0;}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
