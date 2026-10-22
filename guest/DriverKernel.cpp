#include "DriverKernel.h"
#include "TelemetryKernel.h"
#include "PeLoader.h"
#include "fixtures/DriverAbi.h"
#include "guardian/Driver.h"
#include <intrin.h>
extern "C" const volatile char GuardianDriverTelemetryCapability[]="GuardianTelemetry/1/bounded";
namespace {
using namespace guardian;
alignas(4096) uint64_t pml4[512],pdpt[512],pd[512],low[512];
uint64_t sequence=0;uint8_t mapped=0;
bool Span(uint64_t at,uint64_t bytes) noexcept {return at>=driver::Arena&&at<=driver::ArenaEnd&&bytes<=driver::ArenaEnd-at;}
void Copy(void* to,const void* from,uint32_t bytes) noexcept {auto* d=static_cast<uint8_t*>(to);auto* s=static_cast<const uint8_t*>(from);for(uint32_t n=0;n<bytes;++n)d[n]=s[n];}
bool Protect(uint64_t at,uint32_t bytes,uint32_t access) noexcept {
    if((at&4095)||(bytes&4095)||!Span(at,bytes)||(access!=0&&access!=1&&access!=3&&access!=5))return false;
    driver::Request request{driver::Magic,driver::Version,sizeof(driver::Request),++sequence,driver::Op::Protect,access,at,bytes,0,0};
    Copy(reinterpret_cast<void*>(driver::RequestGpa),&request,sizeof(request));__outbyte(driver::Port,1);
    const auto* response=reinterpret_cast<volatile driver::Request*>(driver::RequestGpa);if(response->status)return false;
    for(uint64_t page=at;page<at+bytes;page+=4096){low[page/4096]=access?(page|1ull|(access==3?2ull:0ull)|(access==5?0ull:(1ull<<63))):0;__invlpg(reinterpret_cast<void*>(page));}
    return true;
}
bool Map(void*,uint64_t at,uint32_t bytes) noexcept {if(!Protect(at,bytes,3))return false;auto* p=reinterpret_cast<uint8_t*>(at);for(uint32_t n=0;n<bytes;++n)p[n]=0;++mapped;return true;}
bool Read(void*,uint64_t at,void* out,uint32_t bytes) noexcept {if(!Span(at,bytes))return false;Copy(out,reinterpret_cast<void*>(at),bytes);return true;}
bool Write(void*,uint64_t at,const void* in,uint32_t bytes) noexcept {if(!Span(at,bytes))return false;Copy(reinterpret_cast<void*>(at),in,bytes);return true;}
bool Seal(void*,uint64_t at,uint32_t bytes,pe::Protection p) noexcept {return Protect(at,bytes,p==pe::Protection::None?0u:p==pe::Protection::Read?1u:p==pe::Protection::ReadWrite?3u:5u);}
bool Unmap(void*,uint64_t at) noexcept {if(!Protect(at,driver::MaxFile,0))return false;if(mapped)--mapped;return true;}
void Tables(uint64_t ram) noexcept {
    pml4[0]=reinterpret_cast<uint64_t>(pdpt)|3;pdpt[0]=reinterpret_cast<uint64_t>(pd)|3;
    for(uint64_t n=0;n<(ram+0x1FFFFF)/0x200000;++n)pd[n]=n*0x200000|0x83ull;
    pd[0]=reinterpret_cast<uint64_t>(low)|3;
    for(uint64_t n=0;n<512;++n)low[n]=n*4096|3|(1ull<<63);
    __writemsr(0xC0000080,__readmsr(0xC0000080)|(1ull<<11));__writecr0(__readcr0()|(1ull<<16));__writecr3(reinterpret_cast<uint64_t>(pml4));
}
}
uint32_t HardwareDriverRun(const guardian::abi::BootInfo& boot) noexcept {
    using namespace guardian;const auto& input=*reinterpret_cast<const driver::Boot*>(driver::BootGpa);
    if(input.magic!=driver::Magic||input.version!=driver::Version||input.bytes!=sizeof(input)||input.bootId!=boot.bootId||
       !input.imageBytes||input.imageBytes>driver::MaxFile||!input.apiBytes||input.apiBytes>driver::MaxFile||input.reserved[0]||input.reserved[1]||input.reserved[2])return 1;
    Tables(boot.ramBytes);if(GuardianDriverTelemetryCapability[0]=='G')TelemetryInitialize(boot.bootId);
    pe::Source sources[]{{"driver.sys",reinterpret_cast<const uint8_t*>(driver::SourceGpa),input.imageBytes},
        {"ntoskrnl.exe",reinterpret_cast<const uint8_t*>(driver::ApiGpa),input.apiBytes}};
    pe::Loader loader;const pe::Backend backend{Map,Read,Write,Seal,Unmap};uint32_t index=UINT32_MAX,apiIndex=UINT32_MAX;
    driver::Report report{driver::Magic,driver::Version,sizeof(driver::Report),boot.bootId,input.packageHash,0,0,0,0,0,0,0,0};
    wdm::DriverObject object{};object.type=4;object.size=sizeof(object);
    // The kernel API is part of the execution environment even when an
    // optimized driver has no imports (for example, an immediate entry error).
    if(!loader.Configure(backend,nullptr,sources,2,driver::Arena)||!loader.Load("ntoskrnl.exe",apiIndex)||!loader.Load("driver.sys",index)){report.error=0x100+static_cast<uint32_t>(loader.Failure());}
    else {
        report.installed=1;const auto* module=loader.Get(index);object.start=reinterpret_cast<void*>(module->base);object.imageBytes=module->size;object.initialize=reinterpret_cast<void*>(module->entry);
        uint64_t probe=0,stats=0;
        if(!loader.Resolve(index,"GuardianDriverProbe",0,probe)||!loader.Resolve(apiIndex,"GuardianApiState",0,stats)||!loader.Executable(probe)||!loader.Executable(stats))report.error=2;
        else {
            using Entry=uint32_t (*)(wdm::DriverObject*,wdm::UnicodeString*);using Probe=uint32_t (*)(uint32_t);using Stats=uint64_t (*)();
            uint16_t path[128]{};const char prefix[]="\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";uint32_t length=0;
            for(;prefix[length];++length)path[length]=static_cast<uint16_t>(prefix[length]);
            for(uint32_t n=0;n<64&&input.service[n]&&length<127;++n)path[length++]=static_cast<uint16_t>(input.service[n]);
            wdm::UnicodeString registry{static_cast<uint16_t>(length*2),static_cast<uint16_t>((length+1)*2),0,path};
            auto startAudit=telemetry::Begin(telemetry::Domain::Driver,telemetry::Operation::Load,0,0,input.service);
            report.error=telemetry::Before(startAudit)?reinterpret_cast<Entry>(module->entry)(&object,&registry):0xC0000022;
            telemetry::End(startAudit,report.error);
            if(!report.error){report.initialized=1;report.apiBefore=reinterpret_cast<Stats>(stats)();
                const uint32_t values[]{0,UINT32_MAX,0x13579BDF,0x80000001};
                bool ok=report.apiBefore==0x0000000100010101ull&&object.device&&object.unload&&loader.Executable(reinterpret_cast<uint64_t>(object.unload));
                for(uint32_t value:values){if(!ok)break;ok=reinterpret_cast<Probe>(probe)(value)==driver::Exchange(value);if(ok)++report.operations;}
                report.functional=ok?1u:0u;if(!ok)report.error=3;
                if(object.unload&&loader.Executable(reinterpret_cast<uint64_t>(object.unload))){
                    auto stopAudit=telemetry::Begin(telemetry::Domain::Driver,telemetry::Operation::Unload,0,0,input.service);telemetry::Before(stopAudit);
                    object.unload(&object);report.apiAfter=reinterpret_cast<Stats>(stats)();
                    report.unloaded=report.apiAfter==0x0000010101010000ull&&!object.device?1u:0u;if(!report.unloaded)report.error=4;telemetry::End(stopAudit,report.unloaded?0:4);}
            }
        }
    }
    const bool removed=loader.UnloadAll()&&mapped==0;if(!removed)report.error=5;
    Copy(reinterpret_cast<void*>(driver::ReportGpa),&report,sizeof(report));
    driver::Request request{driver::Magic,driver::Version,sizeof(driver::Request),++sequence,driver::Op::Report,0,0,0,0,0};
    Copy(reinterpret_cast<void*>(driver::RequestGpa),&request,sizeof(request));__outbyte(driver::Port,1);
    return report.error||!report.functional||!report.unloaded?1u:0u;
}
