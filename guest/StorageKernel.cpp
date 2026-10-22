#include "StorageKernel.h"
#include "guardian/SystemClock.h"
#include "guardian/Environment.h"
#include "IoKernel.h"
#include "guardian/Validate.h"
#include <intrin.h>
extern "C" const volatile char GuardianStorageQuotaCapability[]="GuardianStorageQuota/1/512/32";
namespace {
using namespace guardian;
storage::Volume volume,core,copy;
storage::Report report{};processes::Manager* processesManager=nullptr;
void (*verify)(bool,uint32_t) noexcept=nullptr;
void Check(bool ok,uint32_t n) noexcept {verify(ok,0x3000+n);}
void CoreCheck(bool ok,uint32_t n) noexcept {verify(ok,0x2000+n);}
void Copy(void* out,const void* in,uint32_t n) noexcept {auto* d=static_cast<volatile uint8_t*>(out);const auto* s=static_cast<const uint8_t*>(in);for(uint32_t i=0;i<n;++i)d[i]=s[i];}
bool Block(void*,io::Op op,uint64_t offset,uint8_t* data) noexcept {return IoBlockSync(op,offset,data);}
bool Backing(void*,guardian::storage::BackingSpace& out) noexcept {uint8_t data[512]{};if(!IoBlockSync(guardian::io::Op::QuerySpace,0,data))return false;Copy(&out,data,sizeof(out));return true;}
void Text(char* out,const char* text,uint32_t limit) noexcept {for(uint32_t i=0;i<limit-1 && text[i];++i)out[i]=text[i];}
void Make(storage::Command& c,storage::Op op,const char* path="") noexcept {c={};c.op=op;Text(c.path,path,storage::PathBytes);}
bool Range(uint32_t pid,uint64_t address,bool write) noexcept {if(address>UINT64_MAX-sizeof(storage::Command))return false;for(uint32_t i=0;i<sizeof(storage::Command);++i)if(!processesManager->Translate(pid,address+i,write))return false;return true;}
}
void StorageInitialize(guardian::processes::Manager& p,guardian::threads::Scheduler& s,void (*check)(bool,uint32_t) noexcept) noexcept {
    using namespace guardian;verify=check;Check(GuardianStorageQuotaCapability[0]=='G',17);processesManager=&p;
    report.magic=storage::Magic;report.version=storage::Version;report.bytes=sizeof(report);report.bootId=reinterpret_cast<const abi::BootInfo*>(0x100000)->bootId;
    const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);const auto* bus=reinterpret_cast<const io::Bus*>(io::BusGpa);
    Check(io::ValidBus(*bus,report.bootId) && bus->devices[2].features==7 && abi::Covered(reinterpret_cast<const abi::MemoryRegion*>(boot->memoryMapGpa),boot->memoryMapCount,storage::ReportGpa,abi::PageBytes,abi::MemoryKind::BootData),15);
    report.coreChecks=storage::StorageChecks(core,copy,s,CoreCheck);Check(report.coreChecks>=700,1);report.checks|=0x3FF;volume.Configure(nullptr,Block,s);
    volume.ConfigureClock([](void*) noexcept -> uint64_t {
        const auto& clock=*reinterpret_cast<const volatile guardian::cpu::Clock*>(guardian::cpu::ClockGpa);
        const auto* boot=reinterpret_cast<const guardian::abi::BootInfo*>(0x100000);guardian::time::Sample sample{};
        return guardian::time::ReadPublished(clock,boot->bootId,sample)?sample.utc100ns:0;
    });
    const auto* quota=reinterpret_cast<const storage::QuotaInfo*>(storage::QuotaGpa);
    if(quota->magic==storage::QuotaMagic){Check(storage::ValidQuota(*quota)&&volume.ConfigureQuota(quota->allocationLimit),16);volume.ConfigureBacking(Backing);}
}
void StorageBegin(uint32_t owner) noexcept {
    using namespace guardian::storage;const auto token=processesManager->Get(owner)->token;
    Check(volume.Mount()==Success,2);report.sequenceBefore=volume.Sequence();report.recovered=report.sequenceBefore?1:0;
    Command c{};Make(c,Op::Open,"C:\\persist.bin");c.access=All;c.disposition=2;Check(volume.Execute(owner,token,c)==Success,3);const auto h=c.handle;
    uint32_t counter=0;
    if(report.recovered){Make(c,Op::Read);c.handle=h;c.length=4;Check(volume.Execute(owner,token,c)==Success && c.transferred==4,4);Copy(&counter,c.data,4);Check(counter && counter<100000,5);
        Make(c,Op::RegGet,"HKLM\\Software\\Guardian");Text(c.name,"Marker",NameBytes);c.length=512;Check(volume.Execute(owner,token,c)==Success && c.type==4 && c.transferred==4,6);uint32_t registry=0;Copy(&registry,c.data,4);Check(registry==counter,7);}
    ++counter;Make(c,Op::Write);c.handle=h;c.length=4;Copy(c.data,&counter,4);Check(volume.Execute(owner,token,c)==Success,8);
    Make(c,Op::Close);c.handle=h;Check(volume.Execute(owner,token,c)==Success,9);
    Make(c,Op::RegCreate,"HKLM\\Software\\Guardian");Check(volume.Execute(owner,token,c)==Success,10);
    Make(c,Op::RegSet,"HKLM\\Software\\Guardian");Text(c.name,"Marker",NameBytes);c.type=4;c.length=4;Copy(c.data,&counter,4);Check(volume.Execute(owner,token,c)==Success,11);++report.registryChanges;
    Check(volume.Flush()==Success,12);report.checks|=0x400;
}
void StorageExit(uint32_t owner) noexcept {volume.Exit(owner);}
uint32_t StorageExecute(uint32_t pid,guardian::storage::Command& c) noexcept {
    using namespace guardian;auto* p=processesManager->Get(pid);if(!p || p->state!=processes::State::Live)return storage::Denied;
    ++report.userCalls;const auto status=volume.Execute(pid,p->token,c);
    if(status==storage::Denied)++report.denials;
    if(!status){if(c.op==storage::Op::Read)++report.reads;if(c.op==storage::Op::Write)++report.writes;if(c.op==storage::Op::RegSet)++report.registryChanges;}
    return status;
}
bool StorageDispatch(uint32_t pid,uint32_t tid,const TrapFrame& f,ExecutionContext&,uint32_t& status) noexcept {
    using namespace guardian;using namespace guardian::storage;
    if(f.rax!=0x80)return false;++report.userCalls;
    if(!Range(pid,f.rdx,false) || !Range(pid,f.rdx,true)){status=Denied;++report.denials;return true;}
    Command c{};auto* out=reinterpret_cast<uint8_t*>(&c);for(uint32_t i=0;i<sizeof(c);++i)out[i]=*reinterpret_cast<const uint8_t*>(processesManager->Translate(pid,f.rdx+i));
    if(c.op==Op::WaitWatch && c.version==Version && c.bytes==sizeof(c) && !c.status && !c.reserved && c.watch<=UINT32_MAX && processesManager->Owner(tid)==pid)status=volume.Wait(pid,static_cast<uint32_t>(c.watch),tid);
    else status=volume.Execute(pid,processesManager->Get(pid)->token,c);
    c.status=status;if(status==Denied)++report.denials;
    if(!status){if(c.op==Op::Read)++report.reads;if(c.op==Op::Write)++report.writes;if(c.op==Op::RegSet)++report.registryChanges;if(c.op==Op::RegPoll)++report.notifications;}
    const auto* bytes=reinterpret_cast<const uint8_t*>(&c);for(uint32_t i=0;i<sizeof(c);++i)*reinterpret_cast<uint8_t*>(processesManager->Translate(pid,f.rdx+i,true))=bytes[i];return true;
}
void StorageFinish() noexcept {Check(!volume.Handles() && !volume.Watches() && volume.Flush()==guardian::storage::Success,13);report.sequenceAfter=volume.Sequence();report.liveHandles=volume.Handles();report.liveWatches=volume.Watches();Check(report.sequenceAfter>report.sequenceBefore && report.userCalls>=10 && report.reads && report.writes && report.denials,14);report.checks|=0x800;}
void StoragePublish() noexcept {Copy(reinterpret_cast<void*>(guardian::storage::ReportGpa),&report,sizeof(report));_ReadWriteBarrier();__outbyte(guardian::storage::Port,1);}
