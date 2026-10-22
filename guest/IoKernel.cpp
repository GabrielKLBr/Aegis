#include "IoKernel.h"
#include "IoManager.h"
#include "guardian/Validate.h"
#include <intrin.h>
#ifdef GUARDIAN_STORAGE_KERNEL
#include "StorageKernel.h"
#endif
namespace {
using namespace guardian;
io::Manager manager,checksManager;
processes::Manager* processesManager=nullptr;bool enabled[io::DeviceCount]{};
io::Report report{};io::Bus bus{};uint64_t sequence=0;
processes::Object* objects[io::DeviceCount]{};uint32_t pins[io::DeviceCount]{},drivers[io::DeviceCount]{};
void (*verify)(bool,uint32_t) noexcept=nullptr;
void Check(bool ok,uint32_t n) noexcept {verify(ok,0xD00+n);}
void CoreCheck(bool ok,uint32_t n) noexcept {verify(ok,0x1000+n);}
void Copy(void* out,const void* in,uint32_t n) noexcept {auto* d=static_cast<volatile uint8_t*>(out);auto* s=static_cast<const uint8_t*>(in);for(uint32_t i=0;i<n;++i)d[i]=s[i];}
void Release(void*,uint32_t pin) noexcept {Check(processesManager->Dereference(pin),1);}
bool Busy(void*,uint32_t device) noexcept {const auto* o=objects[device-1];return o && (o->handles || o->refs>1);}
bool Start(void*,uint32_t device) noexcept {if(!io::ValidBus(bus,report.bootId) || !device || device>io::DeviceCount || enabled[device-1])return false;enabled[device-1]=true;return true;}
void Stop(void*,uint32_t device) noexcept {Check(device && device<=io::DeviceCount && enabled[device-1],11);enabled[device-1]=false;}
uint32_t Transfer(void*,uint32_t device,io::Op op,uint64_t offset,const uint8_t* in,uint32_t bytes,uint8_t* out,uint32_t& transferred) noexcept {
    if(!device || device>io::DeviceCount || !enabled[device-1])return io::Removed;
    io::Packet request{io::Magic,io::Version,sizeof(request),report.bootId,++sequence,sequence,device,op,offset,bytes,0,0,0,{}};
    if(op==io::Op::Write)for(uint32_t i=0;i<bytes;++i)request.data[i]=in[i];
    Copy(reinterpret_cast<void*>(io::PacketGpa),&request,sizeof(request));_ReadWriteBarrier();__outbyte(io::Port,1);
    io::Packet response{};Copy(&response,reinterpret_cast<const void*>(io::PacketGpa),sizeof(response));
    if(!io::ValidResponse(request,response))return io::DeviceError;
    transferred=response.transferred;if(response.status==io::Success && (op==io::Op::Read || op==io::Op::QuerySpace))for(uint32_t i=0;i<transferred;++i)out[i]=response.data[i];
    if(device==1)report.consoleBytes+=transferred;else if(device==2)report.channelBytes+=transferred;else if(op!=io::Op::QuerySpace)report.blockBytes+=transferred;
    return response.status;
}
const char* Name(uint32_t id) noexcept {return id==1?"\\Global\\IoConsole":(id==2?"\\Global\\IoChannel":"\\Global\\IoBlock");}
bool UserRange(uint32_t pid,uint64_t address,uint32_t bytes,bool write) noexcept {
    if(bytes>io::MaxTransfer || address>UINT64_MAX-bytes)return false;for(uint32_t i=0;i<bytes;++i)if(!processesManager->Translate(pid,address+i,write))return false;return true;
}
uint8_t* Physical(uint32_t pid,uint64_t address,bool write) noexcept {return reinterpret_cast<uint8_t*>(processesManager->Translate(pid,address,write));}
}
void IoInitialize(guardian::processes::Manager& p,guardian::threads::Scheduler& s,void (*check)(bool,uint32_t) noexcept) noexcept {
    using namespace guardian;verify=check;processesManager=&p;Copy(&bus,reinterpret_cast<const void*>(io::BusGpa),sizeof(bus));
    const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);Check(io::ValidBus(bus,boot->bootId) && abi::Covered(reinterpret_cast<const abi::MemoryRegion*>(boot->memoryMapGpa),boot->memoryMapCount,io::PacketGpa,3*abi::PageBytes,abi::MemoryKind::BootData),2);
    report.magic=io::Magic;report.version=io::Version;report.bytes=sizeof(report);report.bootId=boot->bootId;
    report.coreChecks=io::IoChecks(checksManager,s,CoreCheck);Check(report.coreChecks>500,3);report.checks|=0x3FF;
    manager.Configure(s,nullptr,Release,Busy);
}
void IoBegin(uint32_t owner) noexcept {
    using namespace guardian;
    for(uint32_t i=0;i<io::DeviceCount;++i){io::Driver driver{};driver.device=i+1;driver.start=Start;driver.stop=Stop;driver.dispatch=Transfer;Check(manager.Load(driver,drivers[i])==io::Success,4);
        uint64_t handle=0;Check(processesManager->CreateObject(owner,processes::Kind::Device,Name(i+1),nullptr,processes::GenericAll,0,handle,i+1)==processes::Success,5);
        Check(processesManager->Reference(owner,handle,processes::Query,pins[i])==processes::Success,6);
        Check(processesManager->Resolve(owner,handle,processes::Kind::Device,processes::Query,objects[i])==processes::Success && processesManager->Close(owner,handle)==processes::Success,7);
    }
}
void IoTick() noexcept {++report.ticks;manager.Pump();}
void IoExit(uint32_t owner) noexcept {
#ifdef GUARDIAN_STORAGE_KERNEL
    StorageExit(owner);
#endif
    manager.Exit(owner);
}
bool IoDispatch(uint32_t pid,uint32_t tid,const TrapFrame& f,ExecutionContext& c,uint32_t& status) noexcept {
    using namespace guardian;
    if(f.rax<0x70 || f.rax>0x77)return false;
    if(f.rax==0x70){uint64_t handle=0;status=f.rdx<1 || f.rdx>io::DeviceCount || f.r8>UINT32_MAX?io::Invalid:processesManager->Open(pid,processes::Kind::Device,Name(static_cast<uint32_t>(f.rdx)),static_cast<uint32_t>(f.r8),handle);c.frame.rdx=handle;return true;}
    if(f.rax==0x71){
        processes::Object* object=nullptr;
        if(f.r8<1 || f.r8>3 || f.r10>io::MaxTransfer){status=io::Invalid;return true;}const auto op=static_cast<io::Op>(f.r8);
        const auto rights=(op==io::Op::Read || op==io::Op::QuerySpace)?processes::ReadMemory:processes::WriteMemory;status=processesManager->Resolve(pid,f.rdx,processes::Kind::Device,rights,object);if(status)return true;
        uint8_t data[io::MaxTransfer]{};const auto bytes=static_cast<uint32_t>(f.r10);
        if(op==io::Op::Write){if(!UserRange(pid,f.r9,bytes,false)){status=io::Denied;return true;}for(uint32_t i=0;i<bytes;++i)data[i]=*Physical(pid,f.r9+i,false);}
        uint32_t pin=0,id=0;status=processesManager->Reference(pid,f.rdx,rights,pin);if(status)return true;
        status=manager.Submit(pid,object->target,op,data,bytes,f.rdi,pin,id);if(status!=io::Pending)Release(nullptr,pin);c.frame.rdx=id;return true;
    }
    if(f.rax==0x72){
        if(f.rdx>UINT32_MAX || f.r9>io::MaxTransfer){status=io::Invalid;return true;}const auto id=static_cast<uint32_t>(f.rdx);const auto* r=manager.Get(pid,id);
        if(!r){status=io::NotFound;return true;}
        if(r->state==io::State::Complete && r->status==io::Success && (r->op==io::Op::Read || r->op==io::Op::QuerySpace) && (!UserRange(pid,f.r8,r->transferred,true) || f.r9<r->transferred)){status=io::Denied;return true;}
        const auto op=r->op;uint8_t data[io::MaxTransfer]{};uint32_t bytes=0;status=manager.Collect(pid,id,data,static_cast<uint32_t>(f.r9),bytes);
        if(status==io::Success && (op==io::Op::Read || op==io::Op::QuerySpace))for(uint32_t i=0;i<bytes;++i)*Physical(pid,f.r8+i,true)=data[i];c.frame.rdx=bytes;return true;
    }
    if(f.rax==0x73){status=f.rdx>UINT32_MAX?io::Invalid:manager.Cancel(pid,static_cast<uint32_t>(f.rdx));return true;}
    if(f.rax==0x74){processes::Object* object=nullptr;status=processesManager->Resolve(pid,f.rdx,processes::Kind::Device,processes::Modify,object);if(!status)status=object->target<1 || object->target>io::DeviceCount?io::Invalid:manager.Unload(drivers[object->target-1]);return true;}
    if(f.rax==0x75){status=processesManager->Close(pid,f.rdx);return true;}
    if(f.rax==0x76){c.frame.rdx=io::DeviceCount;status=io::Success;return true;}
    status=f.rdx>UINT32_MAX || processesManager->Owner(tid)!=pid?io::Invalid:manager.Wait(pid,static_cast<uint32_t>(f.rdx),tid);return true;
}
void IoFinish() noexcept {
    using namespace guardian;
    for(uint32_t i=0;i<io::DeviceCount;++i){Check(manager.Unload(drivers[i])==io::Success,8);Release(nullptr,pins[i]);pins[i]=0;objects[i]=nullptr;}
    const auto c=manager.Statistics();report.submitted=c.submitted;report.completed=c.completed;report.cancelled=c.cancelled;report.failed=c.failed;report.loads=c.loads;report.unloads=c.unloads;report.liveDrivers=c.drivers;report.liveRequests=c.requests;report.livePins=c.pins;
    Check(c.consistent && !c.drivers && !c.requests && !c.pins && c.submitted==c.completed,9);report.checks|=0xC00;
#if defined(GUARDIAN_ANALYSIS_KERNEL)
    constexpr uint64_t expectedConsole=16;
#elif defined(GUARDIAN_RUNTIME_KERNEL)
    constexpr uint64_t expectedConsole=20;
#else
    constexpr uint64_t expectedConsole=6;
#endif
#if defined(GUARDIAN_CONFORMANCE_KERNEL) || defined(GUARDIAN_SESSION_KERNEL) || defined(GUARDIAN_ENVIRONMENT_KERNEL) || defined(GUARDIAN_ANALYSIS_KERNEL)
    const bool consoleValid=report.consoleBytes>expectedConsole && report.consoleBytes<=io::ConsoleBytes;
#else
    const bool consoleValid=report.consoleBytes==expectedConsole;
#endif
#ifdef GUARDIAN_ANALYSIS_KERNEL
    const bool channelValid=report.channelBytes==0;
#else
    const bool channelValid=report.channelBytes==16;
#endif
    Check(consoleValid && channelValid && report.blockBytes>=1024 && report.ticks>=1,10);
}
void IoPublish() noexcept {Copy(reinterpret_cast<void*>(guardian::io::ReportGpa),&report,sizeof(report));_ReadWriteBarrier();__outbyte(guardian::io::Port,2);}
bool IoBlockSync(guardian::io::Op op,uint64_t offset,uint8_t* data) noexcept {uint32_t bytes=0;const auto n=op==guardian::io::Op::Flush?0u:512u;const auto status=Transfer(nullptr,3,op,offset,data,n,data,bytes);return status==guardian::io::Success && bytes==n;}
uint32_t IoConsoleSync(uint32_t owner,const uint8_t* data,uint32_t bytes,uint32_t& transferred) noexcept {
    using namespace guardian;uint64_t handle=0;auto status=processesManager->Open(owner,processes::Kind::Device,Name(1),processes::WriteMemory,handle);if(status)return status;
    uint32_t pin=0,request=0;status=processesManager->Reference(owner,handle,processes::WriteMemory,pin);processesManager->Close(owner,handle);if(status)return status;
    status=manager.Submit(owner,1,io::Op::Write,data,bytes,0,pin,request);
    if(status!=io::Pending){Release(nullptr,pin);return status;}
    for(uint32_t tries=0;tries<io::MaxRequests;++tries){manager.Pump();const auto* r=manager.Get(owner,request);if(r && r->state==io::State::Complete)return manager.Collect(owner,request,nullptr,0,transferred);}
    manager.Cancel(owner,request);manager.Collect(owner,request,nullptr,0,transferred);return io::DeviceError;
}
