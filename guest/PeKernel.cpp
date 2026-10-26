#include "PeKernel.h"
#ifdef GUARDIAN_INTERNET_NATIVE
#include "guardian/Internet.h"
namespace {void InternetExit(uint32_t) noexcept;}
#endif
#ifdef GUARDIAN_COMPONENT_NATIVE
#include "Component.h"
namespace {guardian::component::Core componentCore;}
#endif
#ifdef GUARDIAN_NATIVE_NT
#include "NativeServices.h"
#ifdef GUARDIAN_DESKTOP_NATIVE
#include "Desktop.h"
#ifdef GUARDIAN_NATIVE_GUI
#include "guardian/NativeGui.h"
namespace {bool NativeGuiFinish() noexcept;}
#endif
#ifdef GUARDIAN_NATIVE_USER32
#include "guardian/NativeUser32.h"
namespace {bool NativeUserFinish() noexcept;void NativeUserThreadGone(uint32_t,uint32_t) noexcept;}
#endif
#ifdef GUARDIAN_NATIVE_GDI
#include "guardian/NativeGdi.h"
namespace {bool NativeGdiFinish() noexcept;void NativeGdiThreadGone(uint32_t,uint32_t) noexcept;}
#endif
namespace {extern guardian::desktop::Core desktopCore;bool DesktopFinish() noexcept;}
#endif
#ifdef GUARDIAN_SECURITY_NATIVE
#include "Security.h"
namespace {guardian::security::Core securityCore;}
#endif
#ifdef GUARDIAN_WIN32_NATIVE
#include "guardian/Win32.h"
#endif
#endif
#include "TelemetryKernel.h"
#ifdef GUARDIAN_ANALYSIS_KERNEL
#include "NetworkDriver.h"
#endif
#include "guardian/Environment.h"
#include "PeFixtures.h"
#ifdef GUARDIAN_PE_GUI_KERNEL
#include "GraphicsDriver.h"
#include "PeGuiFixtures.h"
#include "guardian/ExternalGui.h"
#include "guardian/NtCompatibility.h"
#include "guardian/Validate.h"
#ifdef GUARDIAN_SCALABLE_PE
#include "PeNamespace.h"
extern "C" const volatile char GuardianExternalPeCapability[]="GuardianExternalPe/1/volume";
#else
extern "C" const volatile char GuardianExternalGuiCapability[]="GuardianExternalGui/1/bounded";
#endif
#endif
#ifdef GUARDIAN_RUNTIME_KERNEL
#include "PeRuntimeFixtures.h"
#include "RuntimeMemory.h"
#include "guardian/SystemClock.h"
#ifdef GUARDIAN_LIFETIME_KERNEL
#ifdef GUARDIAN_COMPLETE_KERNEL
#include "PeCompleteFixtures.h"
#ifdef GUARDIAN_CONFORMANCE_KERNEL
#include "PeConformanceFixtures.h"
#endif
#ifdef GUARDIAN_SESSION_KERNEL
#include "PeSessionFixtures.h"
#endif
#ifdef GUARDIAN_ANALYSIS_KERNEL
#include "PeAnalysisFixtures.h"
extern "C" const volatile char GuardianAnalysisCapability[]="GuardianAnalysis/1/owned";
#endif
#ifdef GUARDIAN_ENVIRONMENT_KERNEL
#include "PeEnvironmentFixtures.h"
extern "C" const volatile char GuardianEnvironmentCapability[]="GuardianEnvironment/1/synthetic";
#endif
#include "PeExceptions.h"
#else
#include "PeLifetimeFixtures.h"
#endif
#include "Unwind.h"
#include "ExecutionContext.h"
#endif
#include "PeServices.h"
#include "StorageKernel.h"
#include "IoKernel.h"
#endif
#include <intrin.h>
#include <string.h>
namespace {
using namespace guardian;
struct Context {processes::Manager* processes;memory::Manager* memory;uint32_t pid;};
#ifdef GUARDIAN_COMPLETE_KERNEL
#ifdef GUARDIAN_SCALABLE_PE
pe::Loader scalableMappingLoader;
pe::Loader& MappingLoader() noexcept {return scalableMappingLoader;}
#else
static_assert(sizeof(pe::Loader)<=4096);
pe::Loader& MappingLoader() noexcept {return *reinterpret_cast<pe::Loader*>(PeExceptionScratch+PeExceptionScratchBytes);}
#endif
#else
pe::Loader mappingLoader;
#endif
inline constexpr uint32_t InitCalls=
#ifdef GUARDIAN_SCALABLE_PE
peprofile::InitCalls;
#else
#ifdef GUARDIAN_COMPLETE_KERNEL
12;
#else
4;
#endif
#endif
struct DynamicPending {uint32_t tid=0,index=0,added=0,removing=0,kind=0;};
#if defined(GUARDIAN_NATIVE_NT) && !defined(GUARDIAN_WIN32_NATIVE)
// This phase executes one external native process per boot. Keep its mutable
// volume out of the three legacy loader slots and below the boot image cap.
native::Services nativeServicesInstance;
#endif
struct RuntimeSlot {Context context{};pe::Loader image;pe::Source sources[pe::MaxModules]{};uint32_t pid=0;uint64_t loaderLock=0;bool terminating=false;DynamicPending pending{};
#ifdef GUARDIAN_NATIVE_NT
    native::Services* nativeServices=nullptr;native::SyscallMap nativeMap;uint64_t nativePeb=0;uint32_t nativeTid=0;bool nativeReady=false;
#ifdef GUARDIAN_WIN32_NATIVE
    native::Services nativeOwned;
#endif
#endif
#ifdef GUARDIAN_SCALABLE_PE
    pe::Catalog catalog;uint64_t initialTeb=0,nextResource=1;
    struct Resource {uint64_t id=0,address=0,base=0;uint32_t bytes=0,generation=0;};Resource resources[32]{};
#endif
#ifdef GUARDIAN_RUNTIME_KERNEL
    runtime::VirtualMemory virtualMemory;
#endif
};
#ifdef GUARDIAN_WIN32_NATIVE
class RuntimeArray {
public:
    bool Initialize(memory::Manager& memory) noexcept {if(slots_)return true;pages_=uint32_t((sizeof(RuntimeSlot)*processes::MaxProcesses+4095)/4096);
        base_=memory.physical.Allocate(memory::Frame::Data,pages_);if(!base_)return false;memory_=&memory;slots_=static_cast<RuntimeSlot*>(memory.physical.At(base_));
        for(uint32_t i=0;i<processes::MaxProcesses;++i)::new(static_cast<void*>(slots_+i)) RuntimeSlot{};return true;}
    RuntimeSlot* begin() noexcept {return slots_;}RuntimeSlot* end() noexcept {return slots_?slots_+processes::MaxProcesses:nullptr;}
    bool Release() noexcept {if(!slots_)return true;for(auto& s:*this)if(s.pid)return false;
        if(!memory_->physical.Release(base_,memory::Frame::Data,pages_))return false;slots_=nullptr;return true;}
private:RuntimeSlot* slots_=nullptr;memory::Manager* memory_=nullptr;uint64_t base_=0;uint32_t pages_=0;
};
RuntimeArray runtimes;
#elif defined(GUARDIAN_NATIVE_NT)
RuntimeSlot runtimes[1]{};
#else
RuntimeSlot runtimes[processes::MaxProcesses]{};
#endif
uint32_t rootPid=0,runtimeCode=UINT32_MAX;bool runtimeClean=false,allClean=true;
#ifdef GUARDIAN_ANALYSIS_KERNEL
struct LabService {uint32_t owner=0,id=0,child=0;uint64_t process=0,thread=0;char name[32]{},image[64]{};};
LabService labServices[4]{};uint32_t nextService=1;
#endif
RuntimeSlot* Find(uint32_t pid) noexcept {for(auto& s:runtimes)if(pid && s.pid==pid)return &s;return nullptr;}
#ifdef GUARDIAN_RUNTIME_KERNEL
runtime::Services services;
extern "C" const volatile char GuardianSystemClockCapability[]="GuardianSystemClock/1/real";
#ifdef GUARDIAN_PE_GUI_KERNEL
uint32_t Store(void*,uint32_t,const processes::Token&,storage::Command&) noexcept {return storage::Unsupported;}
#ifdef GUARDIAN_SCALABLE_PE
uint32_t Console(void*,uint32_t,const uint8_t* bytes,uint32_t count,uint32_t& out) noexcept {
    out=0;if(!bytes||count>512)return storage::Invalid;
    for(uint32_t i=0;i<count;++i)__outbyte(0xE9,bytes[i]);out=count;return 0;
}
#else
uint32_t Console(void*,uint32_t,const uint8_t*,uint32_t,uint32_t& out) noexcept {out=0;return storage::Unsupported;}
#endif
#else
uint32_t Store(void*,uint32_t pid,const processes::Token&,storage::Command& c) noexcept {return StorageExecute(pid,c);}
uint32_t Console(void*,uint32_t pid,const uint8_t* b,uint32_t n,uint32_t& out) noexcept {return IoConsoleSync(pid,b,n,out);}
#endif
void RuntimeRejected(uint32_t pid,uint64_t operation,uint32_t status) noexcept {
    auto* slot=Find(pid);const auto* process=slot?slot->context.processes->Get(pid):nullptr;
    auto e=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,pid,process?process->token.user:0,"runtime call failed or unsupported","",operation);
    telemetry::Before(e);telemetry::End(e,status);
}

#endif
bool Map(void* p,uint64_t at,uint32_t n) noexcept {
    auto& c=*static_cast<Context*>(p);auto base=at;
    if(c.processes->Reserve(c.pid,base,n,memory::ReadWrite))return false;
    if(base!=at || c.processes->Commit(c.pid,base,n,memory::ReadWrite)){c.processes->Release(c.pid,base);return false;}return true;
}
bool Read(void* p,uint64_t at,void* out,uint32_t n) noexcept {
    auto& c=*static_cast<Context*>(p);if(at>UINT64_MAX-n)return false;
#ifdef GUARDIAN_SCALABLE_PE
    for(uint32_t i=0;i<n;){if(!c.processes->Translate(c.pid,at+i))return false;const auto chunk=4096u-uint32_t((at+i)&4095);i+=chunk<n-i?chunk:n-i;}
    for(uint32_t i=0;i<n;){const auto physical=c.processes->Translate(c.pid,at+i);auto bytes=4096u-uint32_t((at+i)&4095);if(bytes>n-i)bytes=n-i;
        memcpy(static_cast<uint8_t*>(out)+i,c.memory->physical.At(physical),bytes);i+=bytes;}return true;
#else
    for(uint32_t i=0;i<n;++i)if(!c.processes->Translate(c.pid,at+i))return false;
    for(uint32_t i=0;i<n;++i)static_cast<uint8_t*>(out)[i]=*static_cast<const uint8_t*>(c.memory->physical.At(c.processes->Translate(c.pid,at+i)));return true;
#endif
}
bool Write(void* p,uint64_t at,const void* in,uint32_t n) noexcept {
    auto& c=*static_cast<Context*>(p);if(at>UINT64_MAX-n)return false;
#ifdef GUARDIAN_SCALABLE_PE
    for(uint32_t i=0;i<n;){if(!c.processes->Translate(c.pid,at+i,true))return false;const auto chunk=4096u-uint32_t((at+i)&4095);i+=chunk<n-i?chunk:n-i;}
    for(uint32_t i=0;i<n;){const auto physical=c.processes->Translate(c.pid,at+i,true);auto bytes=4096u-uint32_t((at+i)&4095);if(bytes>n-i)bytes=n-i;
        memcpy(c.memory->physical.At(physical),static_cast<const uint8_t*>(in)+i,bytes);i+=bytes;}return true;
#else
    for(uint32_t i=0;i<n;++i)if(!c.processes->Translate(c.pid,at+i,true))return false;
    for(uint32_t i=0;i<n;++i)*static_cast<uint8_t*>(c.memory->physical.At(c.processes->Translate(c.pid,at+i,true)))=static_cast<const uint8_t*>(in)[i];return true;
#endif
}
#ifdef GUARDIAN_LIFETIME_KERNEL
bool StackImage(void* raw,uint64_t pc,unwind::Image& image) noexcept {
    auto& slot=*static_cast<RuntimeSlot*>(raw);if(!slot.image.Executable(pc))return false;
    for(uint32_t i=0;i<pe::MaxModules;++i)if(const auto* m=slot.image.Get(i);m && pc>=m->base && pc-m->base<m->size){
        image={m->base,m->size,m->directories[3].rva,m->functions};return true;}return false;
}
bool StackExecutable(void* raw,uint64_t pc) noexcept {return static_cast<RuntimeSlot*>(raw)->image.Executable(pc);}
bool StackRead(void* raw,uint64_t at,void* out,uint32_t bytes) noexcept {return Read(&static_cast<RuntimeSlot*>(raw)->context,at,out,bytes);}
uint32_t StackCapture(RuntimeSlot& slot,uint32_t tid,const TrapFrame& f,const ExecutionContext& saved,runtime::Command& reply) noexcept {
    unwind::Stack stack{};if(!ProcessPeStack(slot.pid,tid,stack.low,stack.high))return storage::Denied;
    unwind::Context c{};const uint64_t registers[16]={f.rax,f.rcx,f.rdx,f.rbx,f.rsp,f.rbp,f.rsi,f.rdi,f.r8,f.r9,f.r10,f.r11,f.r12,f.r13,f.r14,f.r15};
    for(uint32_t i=0;i<16;++i){c.gpr[i]=registers[i];for(uint32_t j=0;j<16;++j)c.xmm[i][j]=saved.fx[160+i*16+j];}c.rip=f.rip;
    const unwind::Backend backend{StackImage,StackRead,StackExecutable};uint64_t frames[unwind::MaxFrames]{};uint32_t count=0,hash=0;
    // Raw syscall leaf -> API wrapper -> requested caller. Hide the wrapper.
    #ifdef GUARDIAN_COMPLETE_KERNEL
    constexpr bool handlers=true;
#else
    constexpr bool handlers=false;
#endif
    const auto status=unwind::Capture(backend,&slot,stack,c,reply.flags+1,reply.length,frames,count,hash,handlers);
    if(status!=unwind::Status::Ok)return status==unwind::Status::Invalid?storage::Invalid:storage::Denied;
    for(uint32_t i=0;i<count*8;++i)reply.data[i]=reinterpret_cast<const uint8_t*>(frames)[i];reply.transferred=count;reply.argument=hash;return 0;
}
#endif
bool Protect(void* p,uint64_t at,uint32_t n,pe::Protection protection) noexcept {
    auto& c=*static_cast<Context*>(p);uint32_t old=0;
    const auto access=protection==pe::Protection::Read?memory::ReadOnly:(protection==pe::Protection::ReadWrite?memory::ReadWrite:(protection==pe::Protection::ExecuteRead?memory::ExecuteRead:memory::NoAccess));
    return c.memory->ProtectRange(c.processes->Get(c.pid)->space,at,n,access,old)==memory::Status::Success;
}
#ifdef GUARDIAN_COMPLETE_KERNEL
uint32_t DynamicExecute(RuntimeSlot&,uint32_t,runtime::Command&) noexcept;
uint32_t RefreshThread(RuntimeSlot&,uint32_t,uint64_t) noexcept;
#ifdef GUARDIAN_SCALABLE_PE
uint32_t ScalableMetadata(RuntimeSlot&,uint32_t,runtime::Command&,bool) noexcept;
#endif
#endif
bool Unmap(void* p,uint64_t at) noexcept {auto& c=*static_cast<Context*>(p);return c.processes->Release(c.pid,at)==0;}
#ifdef GUARDIAN_SCALABLE_PE
bool ScalableStart(RuntimeSlot& slot,uint64_t entry,uint64_t argument,uint64_t& teb,uint64_t& startup) noexcept {
    auto& c=slot.context;uint64_t base=0;
    if(c.processes->Reserve(c.pid,base,peprofile::TebBytes,memory::ReadWrite))return false;
    if(c.processes->Commit(c.pid,base,peprofile::TebBytes,memory::ReadWrite)){c.processes->Release(c.pid,base);return false;}
    struct Call {uint64_t function,module,check;};
    struct Start {uint64_t entry,count;Call calls[InitCalls];uint64_t argument;};Start start{};start.entry=entry;start.argument=argument;
    uint64_t vector[pe::MaxModules]{};uint32_t order[pe::MaxModules]{},count=0;
    bool ok=slot.image.InitializationOrder(slot.image.MappedMask(),order,count);
    for(uint32_t p=0;ok&&p<count;++p){const auto index=order[p];const auto* module=slot.image.Get(index);if(!module){ok=false;break;}
        vector[index]=base+peprofile::TlsOffset+uint64_t(index)*peprofile::TlsBytes;
        uint8_t bytes[peprofile::TlsBytes]{};if(module->tlsBytes&&!Read(&c,module->tlsTemplate,bytes,module->tlsBytes)){ok=false;break;}
        if(!Write(&c,vector[index],bytes,sizeof(bytes))){ok=false;break;}
        for(uint32_t i=0;i<module->tlsCallbackCount;++i){if(start.count==InitCalls){ok=false;break;}start.calls[start.count++]={module->tlsCallbacks[i],module->base,0};}
        if(module->dll&&module->entry){if(start.count==InitCalls){ok=false;break;}start.calls[start.count++]={module->entry,module->base,1};}
    }
    uint64_t value=base;
    ok=ok&&Write(&c,base+0x30,&value,8);value=base+0x100;
    ok=ok&&Write(&c,base+0x58,&value,8)&&Write(&c,value,vector,sizeof(vector));value=cpu::UserData+0x400;
#ifdef GUARDIAN_NATIVE_NT
    if(slot.nativePeb)value=slot.nativePeb;
#endif
    ok=ok&&Write(&c,base+0x60,&value,8)&&Write(&c,base+0x500,&start,sizeof(start));
    if(!ok){c.processes->Release(c.pid,base);return false;}teb=base;startup=base+0x500;return true;
}
#endif
}
uint32_t PeMappingChecks(guardian::processes::Manager& processes,guardian::memory::Manager& memory,void (*check)(bool,uint32_t) noexcept) noexcept {
    using namespace guardian;uint32_t checks=0;
#ifdef GUARDIAN_COMPLETE_KERNEL
    auto& mappingLoader=MappingLoader();::new(static_cast<void*>(&mappingLoader)) pe::Loader{};
#endif
    const auto verify=[&](bool ok) noexcept {check(ok,0xA00+(++checks));};
    const auto free=memory.physical.Statistics().free;processes::Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;uint32_t pid=0;
    verify(processes.CreateRoot(token,pid)==0);Context context{&processes,&memory,pid};
    const pe::Backend backend{Map,Read,Write,Protect,Unmap};
    verify(mappingLoader.Configure(backend,&context,pe::fixtures::Sources,2,memory::Arena+0x40000));uint32_t index=UINT32_MAX;
    verify(mappingLoader.Load("GUARDIAN-PE-CONSOLE.EXE",index));verify(index==0);
    const auto* app=mappingLoader.Get(0);const auto* dll=mappingLoader.Get(1);verify(app && dll);
    verify(app->imports==2 && dll->imports==0);verify(app->relocated>0 && dll->relocated>0);
    verify(app->base!=app->preferred && dll->base!=dll->preferred);
    verify(app->tlsCallbackCount==1 && dll->tlsCallbackCount==1);verify(app->tlsBytes>0 && dll->tlsBytes>0);
    verify(app->functions>0); // Optimized leaf-only DLLs legitimately have no .pdata.
    verify(mappingLoader.Executable(app->entry) && mappingLoader.Executable(dll->entry));
    verify(mappingLoader.Executable(app->tlsCallbacks[0]) && mappingLoader.Executable(dll->tlsCallbacks[0]));
    verify(!mappingLoader.Executable(app->base) && !mappingLoader.Executable(app->tlsTemplate));
    verify(!processes.Translate(pid,app->entry,true) && processes.Translate(pid,app->entry)!=0);
    verify(!processes.Translate(pid,app->base,true) && processes.Translate(pid,app->base)!=0);
    verify((memory.Entry(processes.Get(pid)->space,app->entry)&(1ull<<63))==0);
    verify((memory.Entry(processes.Get(pid)->space,app->tlsIndexAddress)&(1ull<<63))!=0);
    uint32_t tlsIndex=UINT32_MAX;verify(Read(&context,app->tlsIndexAddress,&tlsIndex,4) && tlsIndex==0);
    verify(Read(&context,dll->tlsIndexAddress,&tlsIndex,4) && tlsIndex==1);
    const auto allocated=memory.physical.Statistics().free;
    verify(mappingLoader.Load("guardian-pe-console.exe",index) && index==0 && memory.physical.Statistics().free==allocated);
    verify(!mappingLoader.Load("missing.dll",index) && mappingLoader.Failure()==pe::Error::MissingModule && mappingLoader.Get(0) && mappingLoader.Get(1));
    verify(mappingLoader.UnloadAll() && !mappingLoader.Get(0) && !mappingLoader.Get(1));
    verify(processes.Get(pid)->committed==0);verify(processes.Kill(pid,0)==0 && processes.Reap(pid));
    verify(memory.physical.Statistics().free==free && processes.Counts().consistent);
    const char message[]="PE loader mapping checks PASS\n";for(char ch:message)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
    return checks;
}
bool PePrepareProcess(guardian::processes::Manager& p,guardian::memory::Manager& m,uint32_t pid,uint64_t& startup,const char* childCommand,const char* application) noexcept {
    using namespace guardian;
#ifdef GUARDIAN_WIN32_NATIVE
    if(!runtimes.Initialize(m))return false;
#endif
#if !defined(GUARDIAN_ENVIRONMENT_KERNEL) && !defined(GUARDIAN_ANALYSIS_KERNEL)
    (void)application;
#endif
    if(Find(pid))return false;RuntimeSlot* slot=nullptr;for(auto& s:runtimes)if(!s.pid){slot=&s;break;}if(!slot)return false;
    slot->pid=pid;slot->context={&p,&m,pid};slot->terminating=false;slot->loaderLock=0;slot->pending={};auto& runtimeContext=slot->context;auto& loader=slot->image;
#ifdef GUARDIAN_NATIVE_NT
    slot->nativePeb=0;slot->nativeReady=false;slot->nativeTid=0;
#endif
#ifdef GUARDIAN_SCALABLE_PE
    slot->nextResource=1;for(auto& r:slot->resources)r={};
#endif
#ifdef GUARDIAN_RUNTIME_KERNEL
    slot->virtualMemory.Reset();
#endif
    const pe::Backend backend{Map,Read,Write,Protect,Unmap};uint32_t index=0;
#ifdef GUARDIAN_RUNTIME_KERNEL
#ifdef GUARDIAN_LIFETIME_KERNEL
    #ifdef GUARDIAN_COMPLETE_KERNEL
    slot->sources[0]=pe::fixtures::CompleteSources[childCommand?4:0];slot->sources[1]=pe::fixtures::CompleteSources[1];
#ifdef GUARDIAN_CONFORMANCE_KERNEL
    if(!childCommand)slot->sources[0]=pe::fixtures::ConformanceSource;
#endif
#ifdef GUARDIAN_SESSION_KERNEL
    if(!childCommand)slot->sources[0]=pe::fixtures::SessionSource;
#endif
#ifdef GUARDIAN_ENVIRONMENT_KERNEL
    if(!childCommand)slot->sources[0]=pe::fixtures::EnvironmentSource;
    else if(application && guardian::environment::Equal(application,"guardian-docs.exe"))slot->sources[0]=pe::fixtures::DocsSource;
#endif
#ifdef GUARDIAN_ANALYSIS_KERNEL
    if(!childCommand)slot->sources[0]=pe::fixtures::AnalysisSource;
    else if(application&&guardian::environment::Equal(application,"guardian-analysis-child.exe"))slot->sources[0]=pe::fixtures::AnalysisChildSource;
#endif
    slot->sources[2]=pe::fixtures::CompleteSources[2];slot->sources[3]=pe::fixtures::CompleteSources[3];
#else
    slot->sources[0]=pe::fixtures::LifetimeSources[childCommand?2:0];slot->sources[1]=pe::fixtures::LifetimeSources[1];
#endif
#else
    slot->sources[0]=pe::fixtures::RuntimeSources[0];slot->sources[1]=pe::fixtures::RuntimeSources[1];
#if defined(GUARDIAN_PE_GUI_KERNEL) && !defined(GUARDIAN_SCALABLE_PE)
    const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);
    const auto* descriptor=reinterpret_cast<const externalgui::Info*>(externalgui::InfoGpa);
    if(!externalgui::Valid(*descriptor,boot->bootId)||!abi::Covered(reinterpret_cast<const abi::MemoryRegion*>(boot->memoryMapGpa),boot->memoryMapCount,externalgui::SourceGpa,externalgui::MaxFile,abi::MemoryKind::BootData))return false;
    const auto* payload=reinterpret_cast<const uint8_t*>(externalgui::SourceGpa);
    if(externalgui::Hash(payload,descriptor->fileBytes)!=descriptor->hash)return false;
    const auto* ntBinding=reinterpret_cast<const nt::Info*>(nt::InfoGpa);
    if(ntBinding->magic&&(!nt::Valid(*ntBinding,boot->bootId)||!abi::Covered(reinterpret_cast<const abi::MemoryRegion*>(boot->memoryMapGpa),boot->memoryMapCount,nt::InfoGpa,sizeof(nt::Info),abi::MemoryKind::BootData)))return false;
    slot->sources[0]={"application.exe",payload,descriptor->fileBytes};
    slot->sources[1]=pe::fixtures::GuiSource;
#endif
#endif
    services.Configure(nullptr,Store,Console);
    const auto* hardware=reinterpret_cast<const guardian::hardware::Info*>(guardian::hardware::InfoGpa);
    services.ConfigureHardware(hardware->magic==guardian::hardware::Magic?hardware:nullptr);
#else
    slot->sources[0]=pe::fixtures::Sources[0];slot->sources[1]=pe::fixtures::Sources[1];
#endif
#ifdef GUARDIAN_SCALABLE_PE
    const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);
    const auto* descriptor=reinterpret_cast<const pevolume::Info*>(pevolume::InfoGpa);
    const auto* binding=reinterpret_cast<const nt::Info*>(nt::InfoGpa);
    if(descriptor->magic!=pevolume::InfoMagic||descriptor->version!=pevolume::Version||descriptor->bytes!=sizeof(*descriptor)||
        descriptor->reserved||descriptor->boot!=boot->bootId||!nt::Valid(*binding,boot->bootId)||descriptor->volumeBytes>pevolume::MaxBytes||
        !abi::Covered(reinterpret_cast<const abi::MemoryRegion*>(boot->memoryMapGpa),boot->memoryMapCount,pevolume::SourceGpa,descriptor->volumeBytes,abi::MemoryKind::BootData))return false;
    const auto* volume=reinterpret_cast<const uint8_t*>(pevolume::SourceGpa);
    if(pevolume::Hash(volume,descriptor->volumeBytes,true)!=descriptor->hash)return false;
    uint32_t sourceCount=0;const char* selectedRoot=nullptr;
#ifdef GUARDIAN_WIN32_NATIVE
    char childPath[128]{};if(application){uint32_t n=0;bool path=false;for(uint32_t i=0;application[i];++i)if(application[i]==':'||application[i]=='\\')path=true;
        if(!path){const char prefix[]="c:\\app\\";for(char ch:prefix)if(ch)childPath[n++]=ch;}for(uint32_t i=0;application[i];++i){if(n==127)return false;childPath[n++]=application[i];}selectedRoot=childPath;}
#endif
    if(!slot->catalog.Open(volume,descriptor->volumeBytes,binding->profile,slot->sources,sourceCount,selectedRoot))return false;
    constexpr bool allowHandlers=true;
#else
#ifdef GUARDIAN_COMPLETE_KERNEL
    constexpr uint32_t sourceCount=4;constexpr bool allowHandlers=true;
#else
    constexpr uint32_t sourceCount=2;constexpr bool allowHandlers=false;
#endif
#endif
    loader.ConfigureAudit(pid,p.Get(pid)->token.user);
    if(!loader.Configure(backend,&runtimeContext,slot->sources,sourceCount,memory::Arena+0x40000,allowHandlers))return false;
#ifdef GUARDIAN_SCALABLE_PE
    loader.ConfigureNamespace(slot->catalog.Resolver());
#endif
    if(!loader.Load(slot->sources[0].name,index)){
#ifdef GUARDIAN_PE_GUI_KERNEL
        const char text[]="External GUI PE loader rejected image; error=";
        for(char ch:text)if(ch)__outbyte(0xE9,uint8_t(ch));
        __outbyte(0xE9,uint8_t('0'+uint32_t(loader.Failure())));__outbyte(0xE9,'\n');
#ifdef GUARDIAN_NATIVE_NT
        const char debug[]="Native loader stage/module/detail ";for(char ch:debug)if(ch)__outbyte(0xE9,uint8_t(ch));
        const uint32_t fields[]{loader.FailureStage(),loader.FailureModule(),loader.FailureDetail()};for(auto field:fields){for(uint32_t b=8;b;--b)__outbyte(0xE9,"0123456789ABCDEF"[(field>>((b-1)*4))&15]);__outbyte(0xE9,' ');}__outbyte(0xE9,'\n');
#endif
#endif
        return false;
    }
    if(!rootPid){rootPid=pid;runtimeCode=UINT32_MAX;runtimeClean=false;}
#ifdef GUARDIAN_RUNTIME_KERNEL
    if(p.CreateObject(pid,processes::Kind::Mutex,nullptr,nullptr,processes::GenericAll,0,slot->loaderLock))return false;
#endif
    // The initial thread uses the private bootstrap data page. Workers get
    // separately allocated TEB/TLS pages; only the PEB is shared.
    uint8_t zero[256]{};for(uint32_t at=0;at<4096;at+=sizeof(zero))if(!Write(&runtimeContext,cpu::UserData+at,zero,sizeof(zero)))return false;
#ifdef GUARDIAN_SCALABLE_PE
    if(!ScalableStart(*slot,loader.Get(0)->entry,0,slot->initialTeb,startup))return false;
    uint64_t value=loader.Get(0)->base;if(!Write(&runtimeContext,cpu::UserData+0x410,&value,8))return false;
    char command[128]{};const auto* input=childCommand?childCommand:slot->sources[0].name;uint32_t n=0;
    for(;n<sizeof(command)&&input[n];++n)command[n]=input[n];if(n==sizeof(command))return false;
    value=cpu::UserData+0x800;return Write(&runtimeContext,value,command,sizeof(command))&&Write(&runtimeContext,cpu::UserData+0x420,&value,8);
#else
    uint64_t tls[2]={cpu::UserData+0x200,cpu::UserData+0x300};
    if(!Write(&runtimeContext,cpu::UserData+0x100,tls,sizeof(tls)))return false;
    struct Call {uint64_t function,module,checkReturn;};struct Start {uint64_t entry,count;Call calls[InitCalls];};Start start{};start.entry=loader.Get(0)->entry;
    for(uint32_t i=2;i;--i){const auto& module=*loader.Get(i-1);uint8_t data[256]{};
        if(module.tlsBytes && !Read(&runtimeContext,module.tlsTemplate,data,module.tlsBytes))return false;
        if(!Write(&runtimeContext,tls[module.tlsIndex],data,sizeof(data)))return false;
        for(uint32_t cb=0;cb<module.tlsCallbackCount;++cb){if(start.count==InitCalls)return false;start.calls[start.count++]={module.tlsCallbacks[cb],module.base,0};}
        if(module.dll && module.entry){if(start.count==InitCalls)return false;start.calls[start.count++]={module.entry,module.base,1};}}
    startup=cpu::UserData+0x500;uint64_t value=cpu::UserData;
    if(!Write(&runtimeContext,value+0x30,&value,8))return false;value=cpu::UserData+0x100;
    if(!Write(&runtimeContext,cpu::UserData+0x58,&value,8))return false;value=cpu::UserData+0x400;
    if(!Write(&runtimeContext,cpu::UserData+0x60,&value,8))return false;value=loader.Get(0)->base;
    if(!Write(&runtimeContext,cpu::UserData+0x410,&value,8) || !Write(&runtimeContext,startup,&start,sizeof(start)))return false;
    char command[128]{};const auto* input=childCommand?childCommand:slot->sources[0].name;uint32_t n=0;
    for(;n<sizeof(command) && input[n];++n){if(input[n]<32 || input[n]>126)return false;command[n]=input[n];}if(n==sizeof(command))return false;
    value=cpu::UserData+0x800;return Write(&runtimeContext,value,command,sizeof(command)) && Write(&runtimeContext,cpu::UserData+0x420,&value,8);
#endif

}
#ifdef GUARDIAN_RUNTIME_KERNEL
bool PePrepareThread(uint32_t pid,uint32_t tid,uint64_t stack,uint64_t entry,uint64_t argument,uint64_t& teb,uint64_t& startup) noexcept {
    using namespace guardian;auto* slot=Find(pid);if(!slot || slot->terminating)return false;auto& c=slot->context;auto& loader=slot->image;teb=0;startup=0;uint64_t base=0;
#ifdef GUARDIAN_SCALABLE_PE
    (void)loader;(void)base;if(!PeExecutable(pid,entry)||!ScalableStart(*slot,entry,argument,teb,startup))return false;
    bool ok=Write(&c,teb+0x40,&pid,4)&&Write(&c,teb+0x48,&tid,4);
    const uint64_t high=stack+0x10000,low=stack+4096;ok=ok&&Write(&c,teb+8,&high,8)&&Write(&c,teb+16,&low,8);
    if(!ok){c.processes->Release(pid,teb);teb=startup=0;}return ok;
#else
    if(!PeExecutable(pid,entry) || c.processes->Reserve(c.pid,base,4096,memory::ReadWrite))return false;
    bool ok=c.processes->Commit(c.pid,base,4096,memory::ReadWrite)==0;
    struct Call {uint64_t function,module,checkReturn;};
    struct Start {uint64_t entry,count;Call calls[InitCalls];uint64_t argument;};Start start{};start.entry=entry;start.argument=argument;
    static_assert(sizeof(Start)==16+InitCalls*24+8);
    const uint64_t tls[4]={base+0x200,base+0x300,base+0xA00,base+0xB00};
    ok=ok && Write(&c,base+0x100,tls,sizeof(tls));
    for(uint32_t i=pe::MaxModules;ok && i;--i){const auto* module=loader.Get(i-1);if(!module)continue;if(module->tlsIndex>=pe::MaxModules){ok=false;break;}uint8_t data[256]{};
        if(module->tlsBytes>sizeof(data) || (module->tlsBytes && !Read(&c,module->tlsTemplate,data,module->tlsBytes)) || !Write(&c,tls[module->tlsIndex],data,sizeof(data))){ok=false;break;}
        for(uint32_t cb=0;cb<module->tlsCallbackCount;++cb){if(start.count==InitCalls){ok=false;break;}start.calls[start.count++]={module->tlsCallbacks[cb],module->base,0};}
        if(ok && module->dll && module->entry){if(start.count==InitCalls){ok=false;break;}start.calls[start.count++]={module->entry,module->base,0};}}
    uint64_t value=base;ok=ok && Write(&c,base+0x30,&value,8);value=base+0x100;ok=ok && Write(&c,base+0x58,&value,8);
    value=cpu::UserData+0x400;ok=ok && Write(&c,base+0x60,&value,8);value=c.pid;ok=ok && Write(&c,base+0x40,&value,8);
    value=tid;ok=ok && Write(&c,base+0x48,&value,8);value=stack+0x10000;ok=ok && Write(&c,base+8,&value,8);
    value=stack+4096;ok=ok && Write(&c,base+16,&value,8);ok=ok && Write(&c,base+0x500,&start,sizeof(start));
    if(!ok){c.processes->Release(c.pid,base);return false;}teb=base;startup=base+0x500;return true;
#endif
}
#endif
bool PeOwnsProcess(uint32_t pid) noexcept {return Find(pid)!=nullptr;}
uint64_t PeInitialTeb(uint32_t pid) noexcept {
#ifdef GUARDIAN_SCALABLE_PE
    const auto* slot=Find(pid);return slot?slot->initialTeb:0;
#else
    (void)pid;return guardian::cpu::UserData;
#endif
}
void PeThreadGone(uint32_t pid,uint32_t tid) noexcept {
#ifdef GUARDIAN_NATIVE_GDI
    NativeGdiThreadGone(pid,tid);
#endif
#ifdef GUARDIAN_NATIVE_USER32
    NativeUserThreadGone(pid,tid);
#endif
#ifdef GUARDIAN_COMPONENT_NATIVE
    componentCore.Exit(pid,tid);
#endif
#ifdef GUARDIAN_DESKTOP_NATIVE
    desktopCore.Exit(pid,tid);
#endif
#ifdef GUARDIAN_SCALABLE_PE
    if(auto* slot=Find(pid))slot->catalog.ThreadExit(tid);
#else
    (void)pid;(void)tid;
#endif
}
bool PeExecutable(uint32_t pid,uint64_t rip) noexcept {const auto* slot=Find(pid);return slot && slot->image.Executable(rip);}
bool PeIsRoot(uint32_t pid) noexcept {return pid && pid==rootPid;}
bool PeProcessTerminating(uint32_t pid) noexcept {const auto* slot=Find(pid);return !slot || slot->terminating;}
bool PeMarkTerminating(uint32_t pid) noexcept {auto* slot=Find(pid);if(!slot || slot->terminating)return false;slot->terminating=true;
#ifdef GUARDIAN_COMPLETE_KERNEL
    PeExceptionExit(pid);
#endif
    return true;}
bool PeExitProcess(uint32_t pid,uint32_t code,bool fault) noexcept {
#ifdef GUARDIAN_COMPONENT_NATIVE
    componentCore.Exit(pid);
#endif
#ifdef GUARDIAN_ANALYSIS_KERNEL
    for(auto& service:labServices)if(service.owner==pid){
        if(service.child)ProcessStopPeChild(pid,service.child);
        if(auto* slot=Find(pid)){if(service.thread)slot->context.processes->Close(pid,service.thread);if(service.process)slot->context.processes->Close(pid,service.process);}
        service={};
    }
#endif
    (void)fault;
    auto* slot=Find(pid);if(!slot)return true;
#ifdef GUARDIAN_INTERNET_NATIVE
    InternetExit(pid);
#endif
#ifdef GUARDIAN_SECURITY_NATIVE
    securityCore.Exit(pid);
#endif
#ifdef GUARDIAN_DESKTOP_NATIVE
    desktopCore.Exit(pid);
#endif
#ifdef GUARDIAN_NATIVE_NT
    if(slot->nativeReady&&!slot->nativeServices->Exit()){allClean=false;return false;}
    slot->nativeReady=false;
#endif
#ifdef GUARDIAN_RUNTIME_KERNEL
    if(!services.Exit(pid,slot->context.processes->Get(pid)->token) || services.Count(pid)){allClean=false;return false;}
#endif
#ifdef GUARDIAN_COMPLETE_KERNEL
    PeExceptionExit(pid);
#endif
#ifdef GUARDIAN_COMPLETE_KERNEL
    if(slot->terminating && !fault){if(slot->pending.kind)allClean=false;for(uint32_t i=0;i<pe::MaxModules;++i)if(slot->image.References(i))allClean=false;}
#endif
    const bool clean=slot->image.UnloadAll();allClean=allClean && clean;if(PeIsRoot(pid)){runtimeCode=code;runtimeClean=clean;}if(clean)slot->pid=0;return clean;
}
bool PeProcessPassed() noexcept {for(const auto& s:runtimes)if(s.pid)return false;
#ifdef GUARDIAN_COMPONENT_NATIVE
    if(!componentCore.Clean())return false;
#endif
#ifdef GUARDIAN_SECURITY_NATIVE
    if(!securityCore.Finish())return false;
#endif
#ifdef GUARDIAN_DESKTOP_NATIVE
    if(!DesktopFinish())return false;
#ifdef GUARDIAN_NATIVE_GUI
    if(!NativeGuiFinish())return false;
#endif
#ifdef GUARDIAN_NATIVE_USER32
    if(runtimeCode||!runtimeClean||!allClean||!NativeUserFinish())return false;
#endif
#ifdef GUARDIAN_NATIVE_GDI
    if(runtimeCode||!runtimeClean||!allClean||!NativeGdiFinish())return false;
#endif
#endif
#ifdef GUARDIAN_WIN32_NATIVE
    if(!Win32StorageFinish()||!runtimes.Release())return false;
#endif
    return !runtimeCode && runtimeClean && allClean;}
#ifdef GUARDIAN_RUNTIME_KERNEL
bool PeRuntimeDispatch(uint32_t pid,uint32_t tid,uint64_t now,const TrapFrame& f,ExecutionContext& saved,uint32_t& status) noexcept {
    using namespace guardian;
#ifndef GUARDIAN_LIFETIME_KERNEL
    (void)saved;
#endif
    if(f.rax!=0x81 && f.rax!=0x83 && f.rax!=0x84
#ifdef GUARDIAN_PE_GUI_KERNEL
        && f.rax!=0xA0 && f.rax!=0xA1
#endif
#ifdef GUARDIAN_COMPLETE_KERNEL
        && f.rax!=0x89
#endif
#ifdef GUARDIAN_LIFETIME_KERNEL
        && f.rax!=0x85
#endif
    )return false;status=storage::Denied;
    auto* slot=Find(pid);if(!slot || slot->context.processes->Owner(tid)!=pid)return true;auto& runtimeContext=slot->context;auto& p=*runtimeContext.processes;const auto loaderLock=slot->loaderLock;
#ifdef GUARDIAN_SCALABLE_PE
    slot->catalog.CurrentThread(tid);
#endif
#ifdef GUARDIAN_PE_GUI_KERNEL
    if(f.rax==0xA1){
        const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);
        const auto* binding=reinterpret_cast<const nt::Info*>(nt::InfoGpa);
        if(!binding->magic){status=storage::Unsupported;return true;}
        if(!nt::Valid(*binding,boot->bootId)||f.rdx>UINT64_MAX-sizeof(nt::Query))return true;
        for(uint32_t i=0;i<sizeof(nt::Query);++i)if(!p.Translate(pid,f.rdx+i)||!p.Translate(pid,f.rdx+i,true))return true;
        nt::Query query{};if(!Read(&runtimeContext,f.rdx,&query,sizeof(query)))return true;
        if(query.version!=nt::Version||query.bytes!=sizeof(query)||(query.manifest>2&&!(peprofile::Extended&&query.manifest==UINT32_MAX))||query.reserved){status=storage::Invalid;return true;}
#ifdef GUARDIAN_SCALABLE_PE
        if(query.manifest==UINT32_MAX)query.manifest=slot->catalog.VersionMode();
#endif
        query.target=nt::Find(binding->profile);query.reported=nt::Reported(query.target,static_cast<nt::Manifest>(query.manifest));
        status=Write(&runtimeContext,f.rdx,&query,sizeof(query))?0:storage::Denied;
        return true;
    }
    if(f.rax==0xA0){
        struct Packet {graphics::Command command;graphics::Event event;};
        if(!PeIsRoot(pid)||p.Get(pid)->threadCount!=1||f.rdx>UINT64_MAX-sizeof(Packet))return true;
        for(uint32_t i=0;i<sizeof(Packet);++i)if(!p.Translate(pid,f.rdx+i)||!p.Translate(pid,f.rdx+i,true))return true;
        Packet packet{};if(!Read(&runtimeContext,f.rdx,&packet,sizeof(packet)))return true;
        packet.event={};status=GraphicsExecute(packet.command,packet.event);
        if(!Write(&runtimeContext,f.rdx,&packet,sizeof(packet)))status=storage::Denied;
        return true;
    }
#endif
#ifdef GUARDIAN_LIFETIME_KERNEL
    if(f.rax==0x85){status=f.rdx>UINT32_MAX?storage::Invalid:ProcessBeginPeExit(pid,tid,static_cast<uint32_t>(f.rdx));return true;}
#endif
#ifdef GUARDIAN_COMPLETE_KERNEL
    if(f.rax==0x89){status=RefreshThread(*slot,tid,f.rdx);return true;}
#endif
    // Loader callbacks are serialized and may yield. Wait completion is returned in RAX.
    if(f.rax==0x83){status=p.WaitHandles(pid,tid,&loaderLock,1,false,threads::Never);return true;}
    if(f.rax==0x84){status=p.Unlock(pid,loaderLock,tid);return true;}
    if(f.rdx>UINT64_MAX-sizeof(runtime::Command)){RuntimeRejected(pid,f.rax,status);return true;}
    for(uint32_t i=0;i<sizeof(runtime::Command);++i)if(!p.Translate(pid,f.rdx+i) || !p.Translate(pid,f.rdx+i,true)){RuntimeRejected(pid,f.rax,status);return true;}
    runtime::Command c{};if(!Read(&runtimeContext,f.rdx,&c,sizeof(c)))return true;
    bool valid=c.version==runtime::Version && c.bytes==sizeof(c) && !c.reserved && !c.status && !c.transferred;
    if(c.op>=runtime::Op::LibraryLoad && c.op<=runtime::Op::LibraryFinishGetProc){
#ifdef GUARDIAN_COMPLETE_KERNEL
        status=valid?DynamicExecute(*slot,tid,c):storage::Invalid;
#else
        status=storage::Unsupported;
#endif
        c.status=status;if(!Write(&runtimeContext,f.rdx,&c,sizeof(c)))status=storage::Denied;return true;}

#ifdef GUARDIAN_SCALABLE_PE
    if(c.op==runtime::Op::ResourceFind||c.op==runtime::Op::ResourceInfo||c.op==runtime::Op::ModuleHandle||
       (c.op>=runtime::Op::ContextCreate&&c.op<=runtime::Op::ContextRelease)){
        status=ScalableMetadata(*slot,tid,c,valid);c.status=status;
        if(!Write(&runtimeContext,f.rdx,&c,sizeof(c)))status=storage::Denied;return true;
    }
#endif
    if(c.op>=runtime::Op::MutexCreate&&c.op<=runtime::Op::ProcessExit){
        valid=valid && !c.length && !c.type && !c.access && !c.share && !c.disposition;
        if(c.op==runtime::Op::ThreadCreate || c.op==runtime::Op::ProcessCreate)valid=valid && !(c.flags&~4u) && !c.handle;
        else valid=valid && (c.op==runtime::Op::MutexCreate?c.flags<=1:!c.flags) && (c.op==runtime::Op::Wait || c.op==runtime::Op::ThreadExit || c.op==runtime::Op::ProcessExit || !c.argument);
        if(c.op==runtime::Op::MutexCreate || c.op==runtime::Op::Yield)valid=valid && !c.handle;
    }
    if(c.op==runtime::Op::ProcessCreate && c.argument)valid=false;
    if(c.op==runtime::Op::StackCapture)valid=valid && c.flags<=16 && c.length && c.length<=16 && !c.handle && !c.argument && !c.type && !c.access && !c.share && !c.disposition;
    const bool auditRuntime=c.op==runtime::Op::ThreadCreate||c.op==runtime::Op::MemoryAllocate||c.op==runtime::Op::MemoryFree||c.op==runtime::Op::MemoryProtect||(c.op>=runtime::Op::ServiceCreate&&c.op<=runtime::Op::ServiceQuery);
    auto domain=c.op>=runtime::Op::ServiceCreate?telemetry::Domain::Service:(c.op==runtime::Op::ThreadCreate?telemetry::Domain::Thread:telemetry::Domain::Memory);
    auto action=c.op==runtime::Op::ServiceStart?telemetry::Operation::Start:c.op==runtime::Op::ServiceStop?telemetry::Operation::Stop:c.op==runtime::Op::ServiceQuery?telemetry::Operation::Query:c.op==runtime::Op::MemoryProtect?telemetry::Operation::Protect:c.op==runtime::Op::MemoryFree?telemetry::Operation::Delete:telemetry::Operation::Create;
    const char* resource=domain==telemetry::Domain::Service?c.path:(domain==telemetry::Domain::Thread?"owned thread":"guest virtual memory");
#ifdef GUARDIAN_ANALYSIS_KERNEL
    if(domain==telemetry::Domain::Service&&c.op!=runtime::Op::ServiceCreate)for(const auto& svc:labServices)if(svc.owner==pid&&svc.id==c.handle)resource=svc.name;
#endif
    auto audit=telemetry::Begin(domain,action,pid,p.Get(pid)->token.user,resource,"",c.op==runtime::Op::MemoryProtect?c.access:c.handle);
    const bool permitted=!valid||!auditRuntime||telemetry::Before(audit);
    if(!valid)status=storage::Invalid;
    else if(!permitted)status=storage::Denied;
    else if(c.op==runtime::Op::NetworkEcho){
#ifdef GUARDIAN_ANALYSIS_KERNEL
        if(c.flags||c.handle||c.argument||c.length||c.type||c.access||c.share||c.disposition)status=storage::Invalid;
        else status=NetworkRuntimeEcho(pid,p.Get(pid)->token.user);
#else
        status=storage::Unsupported;
#endif
    }
    else if(c.op==runtime::Op::AnalysisInfo){
#ifdef GUARDIAN_ANALYSIS_KERNEL
        const auto& info=*reinterpret_cast<const telemetry::Info*>(telemetry::InfoGpa);
        if(c.flags||c.handle||c.argument||c.length||c.type||c.access||c.share||c.disposition||info.magic!=telemetry::Magic||info.scenario>1||GuardianAnalysisCapability[0]!='G')status=storage::Invalid;
        else {c.transferred=info.scenario;status=0;}
#else
        status=storage::Unsupported;
#endif
    }
    else if(c.op>=runtime::Op::ServiceCreate&&c.op<=runtime::Op::ServiceQuery){
#ifdef GUARDIAN_ANALYSIS_KERNEL
        status=storage::Invalid;
        if(!c.flags&&!c.argument&&!c.length&&!c.type&&!c.access&&!c.share&&!c.disposition){
            if(c.op==runtime::Op::ServiceCreate){
                char name[128]{},image[128]{};
                if(c.handle||!telemetry::Text(name,c.path,32)||!name[0]||!telemetry::Text(image,reinterpret_cast<const char*>(c.data),64)||!environment::Equal(image,"guardian-analysis-child.exe"))status=storage::Unsupported;
                else {LabService* free=nullptr;bool duplicate=false;for(auto& svc:labServices){if(svc.owner==pid&&environment::Equal(svc.name,name))duplicate=true;if(!svc.owner)free=&svc;}
                    if(duplicate)status=storage::Collision;else if(!free||nextService==UINT32_MAX)status=storage::Quota;else {*free={};free->owner=pid;free->id=nextService++;telemetry::Text(free->name,name,32);telemetry::Text(free->image,image,64);c.transferred=free->id;audit.related=free->id;status=0;}}
            }else {LabService* service=nullptr;for(auto& svc:labServices)if(svc.owner==pid&&svc.id==c.handle)service=&svc;
                if(!service)status=storage::NotFound;
                else if(c.op==runtime::Op::ServiceStart){
                    if(service->child)status=storage::Collision;
                    else {uint32_t childTid=0;status=ProcessCreatePeChild(pid,service->image,"service",false,service->child,childTid,service->process,service->thread);if(!status)audit.related=service->child;}
                }else if(c.op==runtime::Op::ServiceQuery){processes::Object* child=nullptr;const auto found=service->process?p.Resolve(pid,service->process,processes::Kind::Process,processes::Query,child):storage::NotFound;c.transferred=!found&&!child->exited?1u:0u;status=0;}
                else if(!service->child)status=storage::NotFound;
                else {status=ProcessStopPeChild(pid,service->child);if(!status){p.Close(pid,service->thread);p.Close(pid,service->process);service->thread=service->process=0;service->child=0;}}
            }
        }
#else
        status=storage::Unsupported;
#endif
    }
    else if(c.op==runtime::Op::ClockRead){
        if(c.flags||c.handle||c.argument||c.access||c.share||c.disposition||c.length||c.type)status=storage::Invalid;
        else {
            __outbyte(cpu::TimerPort,5);
            _ReadWriteBarrier();
            const auto& clock=*reinterpret_cast<const volatile cpu::Clock*>(cpu::ClockGpa);time::Sample sample{};
            const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);
            if(GuardianSystemClockCapability[0]!='G' || !time::ReadPublished(clock,boot->bootId,sample))status=storage::Invalid;
            else {const auto* bytes=reinterpret_cast<const uint8_t*>(&sample);for(uint32_t i=0;i<sizeof(sample);++i)c.data[i]=bytes[i];c.transferred=sizeof(sample);status=0;}
        }
    }
    else if(c.op==runtime::Op::EnvironmentRead){
#ifdef GUARDIAN_ENVIRONMENT_KERNEL
        const auto& info=*reinterpret_cast<const environment::Info*>(environment::InfoGpa);
        const auto* process=p.Get(pid);
        if(c.flags||c.handle||c.argument||c.length||c.type||c.access||c.share||c.disposition||
           GuardianEnvironmentCapability[0]!='G'||!environment::Valid(info)||info.catalogHash||!process||
           process->token.user!=info.user||process->token.primaryGroup!=info.group)status=storage::Invalid;
        else {
            auto result=info;result.catalogHash=environment::Hash(pe::fixtures::DocsApp,sizeof(pe::fixtures::DocsApp));
            const auto* bytes=reinterpret_cast<const uint8_t*>(&result);for(uint32_t i=0;i<sizeof(result);++i)c.data[i]=bytes[i];c.transferred=sizeof(result);status=0;
        }
#else
        status=storage::Unsupported;
#endif
    }
    else if(c.op>=runtime::Op::MemoryAllocate && c.op<=runtime::Op::MemoryQuery)
        status=slot->virtualMemory.Execute(p,*runtimeContext.memory,pid,c);
    else if(c.op==runtime::Op::Delay){
        __outbyte(cpu::TimerPort,5);
        _ReadWriteBarrier();
        const auto& clock=*reinterpret_cast<const volatile cpu::Clock*>(cpu::ClockGpa);time::Sample sample{};
        const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);
        uint64_t deadline=0;
        if(c.flags||c.handle||c.access||c.share||c.disposition||c.length||c.type||c.argument>=UINT32_MAX||
           !time::ReadPublished(clock,boot->bootId,sample) ||
           !time::MillisecondDeadline(sample.monotonicNs,static_cast<uint32_t>(c.argument),deadline))status=storage::Invalid;
        else if(!c.argument){ProcessYieldPeThread();status=0;}
        else status=ProcessDelayPeThread(tid,deadline);
    }
#ifdef GUARDIAN_LIFETIME_KERNEL
    else if(c.op==runtime::Op::StackCapture)status=StackCapture(*slot,tid,f,saved,c);
    else if(c.op==runtime::Op::ThreadExit || c.op==runtime::Op::ProcessExit)status=c.handle || c.argument>UINT32_MAX?storage::Invalid:ProcessRedirectPeExit(pid,tid,static_cast<uint32_t>(c.argument),c.op==runtime::Op::ProcessExit);
    else if(c.op==runtime::Op::ProcessCreate){uint32_t child=0,thread=0;uint64_t process=0,threadHandle=0;
#if defined(GUARDIAN_SCALABLE_PE) && !defined(GUARDIAN_WIN32_NATIVE)
        status=storage::Unsupported;
#else
        status=ProcessCreatePeChild(pid,c.path,reinterpret_cast<const char*>(c.data),(c.flags&4)!=0,child,thread,process,threadHandle);
#endif
        if(!status){const uint64_t info[3]={process,threadHandle,uint64_t(child)|(uint64_t(thread)<<32)};for(uint32_t i=0;i<sizeof(info);++i)c.data[i]=reinterpret_cast<const uint8_t*>(info)[i];}}
    else if(c.op==runtime::Op::ProcessQuery){processes::Object* object=nullptr;status=p.Resolve(pid,c.handle,processes::Kind::Process,processes::Query,object);if(!status)c.transferred=object->exited?object->exitCode:259;}
#endif
    else if(c.op==runtime::Op::ThreadCreate){uint64_t entry=0;for(uint32_t i=0;i<8;++i)entry|=uint64_t(c.data[i])<<(8*i);uint32_t created=0;
        status=ProcessCreatePeThread(pid,entry,c.argument,(c.flags&4)!=0,created,c.handle);if(!status)c.transferred=created;}
    else if(c.op==runtime::Op::ThreadResume || c.op==runtime::Op::ThreadQuery){
        processes::Object* object=nullptr;status=p.Resolve(pid,c.handle,processes::Kind::Thread,c.op==runtime::Op::ThreadQuery?processes::Query:processes::Modify,object);
        if(!status){if(c.op==runtime::Op::ThreadQuery)c.transferred=object->exited?object->exitCode:259;
            else if(object->exited)status=storage::Invalid;else status=ProcessResumePeThread(object->target,c.transferred);}}
    else if(c.op==runtime::Op::Yield){c.transferred=ProcessYieldPeThread()?1u:0u;status=0;}
    else if(c.op==runtime::Op::MutexCreate){
        status=p.CreateObject(pid,processes::Kind::Mutex,nullptr,nullptr,processes::GenericAll,0,c.handle);
        if(!status && c.flags){status=p.WaitHandles(pid,tid,&c.handle,1,false,now);if(status){p.Close(pid,c.handle);c.handle=0;}}
    }
    else if(c.op==runtime::Op::Wait || c.op==runtime::Op::Unlock || c.op==runtime::Op::CloseSync){
        processes::Object* object=nullptr;status=p.Resolve(pid,c.handle,c.op==runtime::Op::Unlock?processes::Kind::Mutex:processes::Kind::Empty,c.op==runtime::Op::Wait?uint32_t(processes::Wait):(c.op==runtime::Op::CloseSync?0u:uint32_t(processes::Modify)),object);
        if(!status && object->kind!=processes::Kind::Mutex && object->kind!=processes::Kind::Thread && object->kind!=processes::Kind::Process)status=processes::TypeMismatch;
        if(!status){if(c.op==runtime::Op::Unlock)status=p.Unlock(pid,c.handle,tid);
            else if(c.op==runtime::Op::CloseSync)status=p.Close(pid,c.handle);
            else if(c.argument>UINT32_MAX || (c.argument!=UINT32_MAX && c.argument>=(threads::Never-now)/1000000))status=storage::Invalid;
            else status=p.WaitHandles(pid,tid,&c.handle,1,false,c.argument==UINT32_MAX?threads::Never:now+c.argument*1000000);}}
    else status=services.Execute(pid,p.Get(pid)->token,c);
    if(valid&&auditRuntime){if(!status&&c.op==runtime::Op::ThreadCreate)audit.related=c.transferred;telemetry::End(audit,status);}
    if(status&&status!=threads::Pending)RuntimeRejected(pid,uint32_t(c.op),status);
    c.status=status;if(!Write(&runtimeContext,f.rdx,&c,sizeof(c)))status=storage::Denied;return true;
}
#endif

#ifdef GUARDIAN_COMPLETE_KERNEL
namespace {
bool ExceptionExecutable(void* raw,uint64_t pc) noexcept {return static_cast<RuntimeSlot*>(raw)->image.Executable(pc);}
bool ExceptionWrite(void* raw,uint64_t at,const void* bytes,uint32_t count) noexcept {return Write(&static_cast<RuntimeSlot*>(raw)->context,at,bytes,count);}
bool ExceptionEnvironment(uint32_t pid,uint32_t tid,PeExceptionEnvironment& env) noexcept {
    auto* slot=Find(pid);if(!slot || slot->terminating || !ProcessPeStack(pid,tid,env.stack.low,env.stack.high))return false;
    env.api={StackImage,StackRead,ExceptionExecutable};env.context=slot;env.write=ExceptionWrite;
#ifdef GUARDIAN_SCALABLE_PE
    const auto runtime=slot->catalog.Find("guardian-runtime-api.dll");
#else
    constexpr uint32_t runtime=1;
#endif
#ifdef GUARDIAN_CRT_NATIVE
    if(!slot->image.Resolve(runtime,"GuardianCrtConsolidate",0,env.consolidate))return false;
#endif
    return slot->image.Resolve(runtime,"GuardianExceptionDispatch",0,env.entry);
}
}
bool PeHandleException(uint32_t pid,uint32_t tid,ExecutionContext& saved) noexcept {
    PeExceptionEnvironment env{};return ExceptionEnvironment(pid,tid,env) && PeExceptionFault(pid,tid,saved,env);
}
bool PeHandleContinuation(uint32_t pid,uint32_t tid,const TrapFrame& f,ExecutionContext& saved) noexcept {
    PeExceptionEnvironment env{};return ExceptionEnvironment(pid,tid,env) && PeExceptionCall(pid,tid,f,saved,env);
}
#endif
#ifdef GUARDIAN_COMPLETE_KERNEL
namespace {
struct ModuleCall {uint64_t function,module,check;};
uint32_t ModuleIndex(const RuntimeSlot& s,uint64_t base) noexcept {for(uint32_t i=0;i<pe::MaxModules;++i)if(const auto* m=s.image.Get(i);m && m->base==base)return i;return UINT32_MAX;}
#ifdef GUARDIAN_SCALABLE_PE
uint32_t ScalableMetadata(RuntimeSlot& slot,uint32_t tid,runtime::Command& c,bool valid) noexcept {
    if(!valid)return storage::Invalid;auto& loader=slot.image;
    if(c.op==runtime::Op::ModuleHandle){
        if(c.flags||c.argument||c.handle||c.access||c.share||c.disposition||c.length||c.type)return storage::Invalid;
        const auto i=c.path[0]?slot.catalog.Find(c.path):0;const auto* m=loader.Get(i);
        if(!m)return storage::NotFound;c.handle=m->base;return 0;
    }
    if(c.op==runtime::Op::ResourceFind){
        if(c.argument||c.type||c.share||c.disposition)return storage::Invalid;
        const auto index=c.handle?ModuleIndex(slot,c.handle):0;const auto* m=loader.Get(index);if(!m)return storage::InvalidHandle;
        uint64_t address=0;uint32_t bytes=0;if(!loader.Resource(index,c.access,c.length,c.flags,address,bytes))return storage::NotFound;
        RuntimeSlot::Resource* record=nullptr;
        for(auto& r:slot.resources){const auto old=ModuleIndex(slot,r.base);const auto* live=loader.Get(old);if(r.id&&(!live||live->generation!=r.generation))r={};
            if(r.id&&r.base==m->base&&r.address==address){c.argument=r.id;c.transferred=r.bytes;return 0;}if(!r.id&&!record)record=&r;}
        if(!record||slot.nextResource==UINT64_MAX)return storage::Quota;
        *record={slot.nextResource++,address,m->base,bytes,m->generation};c.argument=record->id;c.transferred=bytes;return 0;
    }
    if(c.op==runtime::Op::ResourceInfo){
        if(c.flags||c.type||c.share||c.disposition||c.length||c.access)return storage::Invalid;
        const auto index=c.handle?ModuleIndex(slot,c.handle):0;const auto* m=loader.Get(index);if(!m)return storage::InvalidHandle;
        for(const auto& r:slot.resources)if(r.id==c.argument&&r.base==m->base&&r.generation==m->generation){c.argument=r.address;c.transferred=r.bytes;return 0;}
        return storage::InvalidHandle;
    }
    if(c.flags||c.type||c.access||c.share||c.disposition||c.length)return storage::Invalid;
    if(c.op==runtime::Op::ContextCreate){if(c.handle||c.argument)return storage::Invalid;c.handle=slot.catalog.CreateContext(c.path);return c.handle?0:storage::Invalid;}
    if(c.op==runtime::Op::ContextActivate){if(c.argument)return storage::Invalid;return slot.catalog.Activate(tid,c.handle,c.argument)?0:storage::InvalidHandle;}
    if(c.op==runtime::Op::ContextDeactivate){if(c.handle)return storage::Invalid;return slot.catalog.Deactivate(tid,c.argument)?0:storage::Invalid;}
    if(c.op==runtime::Op::ContextRelease){if(c.argument)return storage::Invalid;return slot.catalog.ReleaseContext(c.handle)?0:storage::InvalidHandle;}
    return storage::Unsupported;
}
#endif
bool ModuleTls(RuntimeSlot& slot,uint32_t mask,bool clear) noexcept {
    uint32_t tids[processes::MaxProcessThreads]{};uint64_t tebs[processes::MaxProcessThreads]{};
    const auto count=ProcessPeTebs(slot.pid,tids,tebs,processes::MaxProcessThreads);if(!count)return false;
    for(uint32_t i=0;i<pe::MaxModules;++i)if(mask&(1u<<i)){const auto* module=slot.image.Get(i);if(!module)return false;uint8_t data[peprofile::TlsBytes]{};
        if(!clear && module->tlsBytes && !Read(&slot.context,module->tlsTemplate,data,module->tlsBytes))return false;
        for(uint32_t t=0;t<count;++t){const uint64_t address=tebs[t]+(peprofile::Extended?peprofile::TlsOffset+uint64_t(i)*peprofile::TlsBytes:(i<2?0x200+i*256:0xA00+(i-2)*256));const uint64_t vector=clear?0:address;
            if(!Write(&slot.context,address,data,sizeof(data)) || !Write(&slot.context,tebs[t]+0x100+i*8,&vector,8))return false;}}
    return true;
}
bool ModuleCalls(RuntimeSlot& slot,uint32_t mask,bool attach,runtime::Command& c) noexcept {
    constexpr uint32_t capacity=peprofile::Extended?peprofile::InitCalls:16;
    ModuleCall calls[capacity]{};uint32_t used=0,done=0;
    for(uint32_t pass=0;mask!=done && pass<pe::MaxModules;++pass){bool progress=false;
        for(uint32_t i=0;i<pe::MaxModules;++i)if((mask&~done)&(1u<<i)){const auto* m=slot.image.Get(i);if(!m)return false;
            if(m->dependencies&mask&~done)continue;
            for(uint32_t j=0;j<m->tlsCallbackCount;++j){if(used==capacity)return false;calls[used++]={m->tlsCallbacks[j],m->base,0};}
            if(m->dll && m->entry){if(used==capacity)return false;calls[used++]={m->entry,m->base,1};}
            done|=1u<<i;progress=true;}
        if(!progress)return false;
    }
    if(done!=mask)return false;c.transferred=used;
    for(uint32_t i=0;i<used;++i){const auto& call=calls[attach?i:used-1-i];for(uint32_t j=0;j<sizeof(call);++j)c.data[i*sizeof(call)+j]=reinterpret_cast<const uint8_t*>(&call)[j];}return true;
}
uint32_t DynamicExecute(RuntimeSlot& slot,uint32_t tid,runtime::Command& c) noexcept {
    auto& loader=slot.image;auto& pending=slot.pending;
    if(slot.terminating && c.op!=runtime::Op::LibraryNext && c.op!=runtime::Op::LibraryFree && c.op!=runtime::Op::LibraryFinishFree)return storage::Denied;
    if(!slot.context.processes->OwnsMutex(slot.pid,slot.loaderLock,tid) || c.length || c.type || c.access || c.share || c.disposition)return storage::Denied;
    const auto index=ModuleIndex(slot,c.handle);
    if(c.op==runtime::Op::LibraryGetProc){
        if(c.flags || c.argument>65535 || index==UINT32_MAX || (!peprofile::Extended&&index>1 && !loader.References(index)))return storage::Invalid;
        const auto keep=loader.MappedMask();uint64_t address=0;
        if(!loader.Resolve(index,c.argument?nullptr:c.path,static_cast<uint32_t>(c.argument),address))return storage::NotFound;
        c.argument=address;
#ifdef GUARDIAN_SCALABLE_PE
        const auto added=loader.MappedMask()&~keep;
        if(added){if(pending.kind||!ModuleTls(slot,added,false)||!ModuleCalls(slot,added,true,c)){ModuleTls(slot,added,true);loader.CancelResolution(added);return storage::Invalid;}
            pending={tid,index,added,0,3};c.flags=1;}
#else
        (void)keep;
#endif
        return 0;}
#ifdef GUARDIAN_SCALABLE_PE
    if(c.op==runtime::Op::LibraryFinishGetProc){
        if(pending.kind!=3||pending.tid!=tid||index!=pending.index||c.flags>1||c.argument)return storage::Invalid;
        if(!c.flags&&(!ModuleTls(slot,pending.added,true)||!loader.CancelResolution(pending.added)))return storage::IoError;
        pending={};return 0;
    }
#endif
    if(c.op==runtime::Op::LibraryNext){if(c.flags || c.argument || c.handle || pending.kind)return storage::Invalid;
        for(uint32_t i=0;i<pe::MaxModules;++i)if(loader.References(i)){c.handle=loader.Get(i)->base;return 0;}return 0;}
    if(c.op==runtime::Op::LibraryLoad){
        if(c.flags || c.argument || c.handle || pending.kind)return storage::Invalid;
        uint32_t found=0,added=0;if(!loader.Acquire(c.path,found,added))return loader.Failure()==pe::Error::MissingModule?storage::NotFound:storage::Invalid;
        c.handle=loader.Get(found)->base;
        if(!ModuleTls(slot,added,false) || !ModuleCalls(slot,added,true,c)){uint32_t removed=0;loader.Release(found,removed);ModuleTls(slot,removed,true);loader.Remove(removed);return storage::NoSpace;}
        pending={tid,found,added,0,1};return 0;}
    if(c.op==runtime::Op::LibraryFinish){
        if(pending.kind!=1 || pending.tid!=tid || index!=pending.index || c.flags>1 || c.argument)return storage::Invalid;
        if(!c.flags){uint32_t remove=0;if(!loader.Release(index,remove) || !ModuleTls(slot,remove,true) || !loader.Remove(remove))return storage::IoError;}
        pending={};return 0;}
    if(c.op==runtime::Op::LibraryFree){
        if(c.flags || c.argument || pending.kind || index==UINT32_MAX || PeExceptionBusy(slot.pid))return storage::Invalid;
        uint32_t remove=0;if(!loader.Release(index,remove))return storage::Invalid;
        if(!ModuleCalls(slot,remove,false,c))return storage::Invalid;pending={tid,index,0,remove,2};return 0;}
    if(c.op==runtime::Op::LibraryFinishFree){
        if(c.flags || c.argument || pending.kind!=2 || pending.tid!=tid || index!=pending.index)return storage::Invalid;
        if(!ModuleTls(slot,pending.removing,true) || !loader.Remove(pending.removing))return storage::IoError;pending={};return 0;}
    return storage::Invalid;
}
uint32_t RefreshThread(RuntimeSlot& slot,uint32_t tid,uint64_t startup) noexcept {
    if(!slot.context.processes->OwnsMutex(slot.pid,slot.loaderLock,tid))return storage::Denied;
    uint32_t tids[processes::MaxProcessThreads]{};uint64_t tebs[processes::MaxProcessThreads]{};
    const auto count=ProcessPeTebs(slot.pid,tids,tebs,processes::MaxProcessThreads);bool found=false;
    for(uint32_t i=0;i<count;++i)if(tids[i]==tid && startup==tebs[i]+0x500)found=true;if(!found)return storage::Invalid;
    struct Start {uint64_t entry,count;ModuleCall calls[InitCalls];uint64_t argument;};Start start{};
    if(!Read(&slot.context,startup,&start,sizeof(start)) || !slot.image.Executable(start.entry))return storage::Denied;
    runtime::Command command{};if(!ModuleCalls(slot,slot.image.MappedMask(),true,command) || command.transferred>InitCalls)return storage::Invalid;
    start.count=command.transferred;for(uint32_t i=0;i<start.count*sizeof(ModuleCall);++i)reinterpret_cast<uint8_t*>(start.calls)[i]=command.data[i];
    return Write(&slot.context,startup,&start,sizeof(start))?0:storage::Denied;
}
}
#endif
#ifdef GUARDIAN_NATIVE_NT
#ifdef GUARDIAN_WIN32_NATIVE
#ifdef GUARDIAN_DESKTOP_NATIVE
#include "DesktopKernel.inl"
#ifdef GUARDIAN_NATIVE_GUI
#include "NativeGuiKernel.inl"
#endif
#ifdef GUARDIAN_NATIVE_USER32
#include "NativeUserKernel.inl"
#endif
#ifdef GUARDIAN_NATIVE_GDI
#include "NativeGdiKernel.inl"
#endif
#endif
#include "Win32Kernel.inl"
#endif
#include "NativeKernel.inl"
#endif
