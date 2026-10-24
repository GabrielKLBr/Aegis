#include "ProcessKernel.h"
#include "TelemetryKernel.h"
#ifdef GUARDIAN_ANALYSIS_KERNEL
#include "NetworkDriver.h"
#endif
#include "ExecutionContext.h"
#include "ProcessManager.h"
#ifdef GUARDIAN_PE_KERNEL
extern "C" const volatile char GuardianProcessTelemetryCapability[]="GuardianTelemetry/1/bounded";
#include "PeKernel.h"
#ifdef GUARDIAN_PE_GUI_KERNEL
#include "GraphicsDriver.h"
#endif
#include "guardian/Environment.h"
#ifdef GUARDIAN_COMPLETE_KERNEL
#include "PeExceptions.h"
#endif
#endif
#ifdef GUARDIAN_IO_KERNEL
#include "IoKernel.h"
#ifdef GUARDIAN_STORAGE_KERNEL
#include "StorageKernel.h"
#endif
#endif
#include "guardian/Validate.h"
#include <intrin.h>
using namespace guardian;
namespace {
memory::Manager memoryManager;
memory::Space kernelSpace;
threads::Scheduler scheduler;
processes::Manager manager;
processes::Report result;
ExecutionContext contexts[threads::MaxThreads],idle;
#ifdef GUARDIAN_NATIVE_NT
ExecutionContext nativeApcContexts[threads::MaxThreads];bool nativeInApc[threads::MaxThreads]{};
extern "C" uint8_t ProcessNativeApc;
#endif
bool waiting[threads::MaxThreads]{},running=false,failed=false;
uint64_t stacks[threads::MaxThreads]{},tebs[threads::MaxThreads]{},startups[threads::MaxThreads]{},sequence=0,session=0;
uint32_t owners[threads::MaxThreads]{},parentPid=0,badPid=0;
void (*verify)(bool,uint32_t) noexcept=nullptr;
void Check(bool b,uint32_t n) noexcept {verify(b,0x600+n);}
void CoreCheck(bool b,uint32_t n) noexcept {verify(b,0x800+n);}
void Copy(void* out,const void* in,uint64_t n) noexcept {auto* d=static_cast<volatile uint8_t*>(out);auto* s=static_cast<const uint8_t*>(in);for(uint64_t i=0;i<n;++i)d[i]=s[i];}
bool Permission(void*,uint64_t gpa,uint32_t access) noexcept {
    const memory::Request request{memory::Magic,memory::Version,sizeof(request),session,++sequence,gpa,4096,access,0};
    Copy(reinterpret_cast<void*>(memory::RequestGpa),&request,sizeof(request));_ReadWriteBarrier();__outbyte(memory::Port,1);return true;
}
void Change(void*,uint64_t root) noexcept {__writecr3(root);}
uint64_t UserAddress(const uint8_t* label) noexcept {return cpu::UserCode+reinterpret_cast<uint64_t>(label)-reinterpret_cast<uint64_t>(&ProcessUserStart);}
uint64_t* Data(uint32_t pid) noexcept {return static_cast<uint64_t*>(memoryManager.physical.At(manager.Get(pid)->space.privatePages[15]));}
uint32_t AddThread(uint32_t pid,uint64_t mode,uint32_t* created=nullptr,uint64_t* output=nullptr,bool suspended=false) noexcept {
    uint64_t stack=0;auto status=manager.Reserve(pid,stack,0x10000,memory::ReadWrite);if(status)return status;
    status=manager.Commit(pid,stack+4096,0xF000,memory::ReadWrite);if(status){manager.Release(pid,stack);return status;}
    const auto tid=scheduler.CreateThread(3,suspended);if(!tid){manager.Release(pid,stack);return processes::NoMemory;}
    uint64_t handle=0;status=manager.Attach(pid,tid,handle);
    if(status){scheduler.Terminate(tid,status);scheduler.Reap(tid);manager.Release(pid,stack);return status;}
    const auto slot=(tid&255u)-1;owners[slot]=pid;stacks[slot]=stack;waiting[slot]=false;contexts[slot]={};auto& c=contexts[slot];SeedContext(c);
#ifdef GUARDIAN_NATIVE_NT
    nativeInApc[slot]=false;nativeApcContexts[slot]={};
#endif
    c.frame.rip=cpu::UserCode;c.frame.rsp=stack+0x10000-0x28;c.frame.cs=0x2B;c.frame.ss=0x23;c.frame.rflags=0x202;c.frame.rdi=pid;c.frame.rsi=mode;
    c.gs=cpu::UserData+0x300;c.fs=cpu::UserData+0x380;
#ifdef GUARDIAN_RUNTIME_KERNEL
    // Legacy FS/GS sentinels overlap the initial PE thread's DLL TLS block.
    if(!PeOwnsProcess(pid))
#endif
    {Data(pid)[96]=pid;Data(pid)[112]=pid^0xABCDEF;}
    if(created)*created=tid;if(output)*output=handle;return processes::Success;
}
bool InitializeData(uint32_t pid,uint64_t secret) noexcept {
    uint64_t address=memory::Arena+0x10000;
    if(manager.Reserve(pid,address,4096,memory::ReadWrite) || manager.Commit(pid,address,4096,memory::ReadWrite))return false;
    auto* data=Data(pid);data[0]=address;data[1]=secret;data[4]=parentPid;data[5]=badPid;data[6]=manager.Get(pid)->self;
    *static_cast<uint64_t*>(memoryManager.physical.At(manager.Translate(pid,address,true)))=secret;
    const char name[]="\\Global\\ProcessReady";for(uint32_t i=0;i<sizeof(name);++i)reinterpret_cast<char*>(data)[0x100+i]=name[i];return true;
}
void KillProcess(uint32_t pid,uint32_t status,bool fault=false) noexcept {
    (void)fault;
#ifdef GUARDIAN_PE_KERNEL
    Check(PeExitProcess(pid,status,fault),21);
#endif
#ifdef GUARDIAN_IO_KERNEL
    IoExit(pid);
#endif
    auto event=telemetry::Begin(telemetry::Domain::Process,telemetry::Operation::Exit,pid,manager.Get(pid)?manager.Get(pid)->token.user:0,"process exit", "",status);telemetry::Before(event);
    const auto code=manager.Kill(pid,status);telemetry::End(event,code);
}
void Garbage() noexcept {
    memoryManager.Activate(&kernelSpace);manager.Poll();
#ifdef GUARDIAN_RUNTIME_KERNEL
    for(uint32_t slot=0;slot<threads::MaxThreads;++slot)if(owners[slot] && tebs[slot]){
        const auto* p=manager.Get(owners[slot]);if(!p || p->state!=processes::State::Live)continue;
        uint32_t tid=0;for(auto id:p->threads)if(id && (id&255u)==slot+1)tid=id;
        const auto* t=scheduler.Get(tid);if(!t || t->state!=threads::State::Dead)continue;
        PeThreadGone(p->id,tid);
        Check((tebs[slot]==cpu::UserData || manager.Release(p->id,tebs[slot])==0) && manager.Release(p->id,stacks[slot])==0,26);
        Check(manager.ReapThread(tid),27);owners[slot]=0;tebs[slot]=stacks[slot]=startups[slot]=0;waiting[slot]=false;
    }
#endif
    for(uint32_t slot=0;slot<processes::MaxProcesses;++slot)if(auto* p=manager.At(slot);p && p->state==processes::State::Dying){const auto pid=p->id;Check(manager.Reap(pid),1);for(uint32_t i=0;i<threads::MaxThreads;++i)if(owners[i]==pid){owners[i]=0;tebs[i]=0;stacks[i]=0;startups[i]=0;}}
}
bool Read(uint32_t pid,uint64_t address,void* out,uint32_t n) noexcept {
    if(!n || n>4096 || address>UINT64_MAX-n)return false;
    for(uint32_t i=0;i<n;++i)if(!manager.Translate(pid,address+i))return false;
    auto* bytes=static_cast<uint8_t*>(out);for(uint32_t i=0;i<n;++i)bytes[i]=*static_cast<const uint8_t*>(memoryManager.physical.At(manager.Translate(pid,address+i)));return true;
}
bool ReadName(uint32_t pid,uint64_t address,char* name) noexcept {
    for(uint32_t i=0;i<74;++i){if(!Read(pid,address+i,name+i,1))return false;if(!name[i])return true;}return false;
}
void Load(uint32_t tid,TrapFrame& f,void* fx) noexcept {
    if(!tid){memoryManager.Activate(&kernelSpace);RestoreContext(idle,f,fx);return;}
    const auto slot=(tid&255u)-1;const auto pid=manager.Owner(tid);auto* p=manager.Get(pid);Check(p && p->state==processes::State::Live,2);
    if(waiting[slot]){contexts[slot].frame.rax=scheduler.Get(tid)->result;waiting[slot]=false;}
#ifdef GUARDIAN_NATIVE_NT
    auto* thread=scheduler.Get(tid);if(thread->delivery&&!nativeInApc[slot]){
        auto& c=contexts[slot];Check(c.frame.rsp>=stacks[slot]+4096+128,0xA0);nativeApcContexts[slot]=c;nativeInApc[slot]=true;thread->delivery=false;
        c.frame.rip=UserAddress(&ProcessNativeApc);c.frame.rsp=((c.frame.rsp-64)&~15ull)-8;
        c.frame.rcx=thread->dispatched.argument;c.frame.rdx=thread->dispatched.argument2;c.frame.r8=thread->dispatched.argument3;c.frame.r9=thread->dispatched.routine;
    }
#endif
    memoryManager.Activate(&p->space);RestoreContext(contexts[slot],f,fx);++result.switches;
}
uint32_t Spawn(uint32_t parent,uint64_t inherited,uint32_t& child,uint64_t& handle) noexcept {
    auto token=manager.Get(parent)->token;token.integrity=1;token.privileges=0;
    auto status=manager.CreateChildProcess(parent,token,inherited?&inherited:nullptr,inherited?1u:0u,child);if(status)return status;
    if(!InitializeData(child,0x3333)){KillProcess(child,processes::NoMemory);manager.Reap(child);return processes::NoMemory;}
    Data(child)[2]=inherited;Data(child)[3]=Data(parent)[3];
    status=AddThread(child,2);if(!status)status=manager.OpenProcess(parent,child,processes::GenericAll,handle);
    if(status){KillProcess(child,status);Garbage();return status;}++result.inheritance;return status;
}
}
#ifdef GUARDIAN_RUNTIME_KERNEL
bool ProcessYieldPeThread() noexcept {
    bool ready=false;for(uint32_t i=0;i<processes::MaxProcesses;++i)if(const auto* p=manager.At(i))for(auto tid:p->threads){const auto* t=scheduler.Get(tid);if(t && t->state==threads::State::Ready && !t->suspended)ready=true;}
    scheduler.Yield();return ready;
}
uint32_t ProcessDelayPeThread(uint32_t tid,uint64_t deadline) noexcept {return scheduler.Delay(tid,deadline);}
uint32_t ProcessResumePeThread(uint32_t tid,uint32_t& previous) noexcept {return scheduler.Resume(tid,previous);}
uint32_t ProcessCreatePeThread(uint32_t pid,uint64_t entry,uint64_t argument,bool suspended,uint32_t& tid,uint64_t& handle) noexcept {
    if(!PeOwnsProcess(pid) || !PeExecutable(pid,entry))return processes::Invalid;
    if(PeProcessTerminating(pid))return processes::Terminated;
    if(manager.Get(pid)->threadCount==processes::MaxProcessThreads)return processes::Quota;
    auto status=AddThread(pid,0,&tid,&handle,suspended);if(status)return status;
    const auto slot=(tid&255u)-1;uint64_t teb=0,startup=0;
    if(!PePrepareThread(pid,tid,stacks[slot],entry,argument,teb,startup)){
        manager.ThreadExit(tid,processes::NoMemory);Check(manager.ReapThread(tid),28);
        manager.Close(pid,handle);manager.Release(pid,stacks[slot]);owners[slot]=0;stacks[slot]=0;tid=0;handle=0;return processes::NoMemory;
    }
    tebs[slot]=teb;startups[slot]=startup;auto& c=contexts[slot];c.frame.rip=UserAddress(&PeThreadBootstrap);c.frame.rcx=startup;c.gs=teb;c.fs=0;return 0;
}
#ifdef GUARDIAN_LIFETIME_KERNEL
bool ProcessPeStack(uint32_t pid,uint32_t tid,uint64_t& low,uint64_t& high) noexcept {
    if(manager.Owner(tid)!=pid || !tid)return false;const auto slot=(tid&255u)-1;
    if(slot>=threads::MaxThreads || owners[slot]!=pid || !stacks[slot])return false;
    low=stacks[slot]+4096;high=stacks[slot]+0x10000;return true;
}
uint32_t ProcessRedirectPeExit(uint32_t pid,uint32_t tid,uint32_t code,bool whole) noexcept {
    if(!PeOwnsProcess(pid) || manager.Owner(tid)!=pid || PeProcessTerminating(pid))return processes::Invalid;
    const auto slot=(tid&255u)-1;if(!tebs[slot] || !startups[slot])return processes::Invalid;
    auto& c=contexts[slot];c.frame.rip=UserAddress(whole?&PeProcessExitEntry:&PeThreadExitEntry);c.frame.rsp=stacks[slot]+0x10000-0x28;
    c.frame.rcx=startups[slot];c.frame.rdx=code;return 0;
}
uint32_t ProcessBeginPeExit(uint32_t pid,uint32_t tid,uint32_t code) noexcept {
    auto* p=manager.Get(pid);if(!p || manager.Owner(tid)!=pid || !PeMarkTerminating(pid))return processes::Invalid;
    // The calling bootstrap owns the loader lock; other threads cannot enter callbacks.
    for(auto other:p->threads)if(other && other!=tid){const auto* t=scheduler.Get(other);if(t && t->state!=threads::State::Dead)manager.ThreadExit(other,code);}
    return 0;
}
uint32_t ProcessCreatePeChildRaw(uint32_t parent,const char* name,const char* command,bool suspended,uint32_t& pid,uint32_t& tid,uint64_t& processHandle,uint64_t& threadHandle) noexcept {
    pid=tid=0;processHandle=threadHandle=0;if(!PeOwnsProcess(parent) || PeProcessTerminating(parent))return processes::Denied;
    const char expected[]="guardian-lifetime-child.exe";bool match=true;uint32_t i=0;
    for(;i<sizeof(expected);++i){auto ch=name[i];if(ch>='A' && ch<='Z')ch=static_cast<char>(ch+32);if(ch!=expected[i]){match=false;break;}}
#ifdef GUARDIAN_ANALYSIS_KERNEL
    if(!match)match=guardian::environment::Equal(name,"guardian-analysis-child.exe");
#endif
#ifdef GUARDIAN_ENVIRONMENT_KERNEL
    if(!match)match=guardian::environment::Equal(name,"guardian-docs.exe");
#endif
    if(!match)return processes::NotFound;
    for(i=0;i<128 && command[i];++i)if(command[i]<32 || command[i]>126)return processes::Invalid;if(i==128)return processes::Invalid;
    auto token=manager.Get(parent)->token;
#ifdef GUARDIAN_ENVIRONMENT_KERNEL
    // Installed owned applications inherit the caller's integrity. Fixture
    // children retain the explicitly restricted token from phase 8.
    if(!guardian::environment::Equal(name,"guardian-docs.exe"))token.integrity=1;
#else
    token.integrity=1;
#endif
#ifdef GUARDIAN_ANALYSIS_KERNEL
    if(guardian::environment::Equal(name,"guardian-analysis-child.exe"))token.integrity=manager.Get(parent)->token.integrity;
#endif
    token.privileges=0;
    auto status=manager.CreateChildProcess(parent,token,nullptr,0,pid);if(status)return status;
    uint64_t internal=0,startup=0;status=AddThread(pid,0,&tid,&internal,suspended);
    if(!status && !PePrepareProcess(manager,memoryManager,pid,startup,command,name))status=processes::NoMemory;
    if(!status)status=manager.OpenProcess(parent,pid,processes::GenericAll,processHandle);
    if(!status)status=manager.OpenThread(parent,tid,processes::GenericAll,threadHandle);
    if(!status)status=manager.Close(pid,internal);
    if(status){if(threadHandle)manager.Close(parent,threadHandle);if(processHandle)manager.Close(parent,processHandle);KillProcess(pid,status);Garbage();pid=tid=0;processHandle=threadHandle=0;return status;}
    const auto slot=(tid&255u)-1;tebs[slot]=cpu::UserData;startups[slot]=startup;auto& c=contexts[slot];
    c.frame.rip=UserAddress(&PeProcessBootstrap);c.frame.rcx=startup;c.gs=cpu::UserData;c.fs=0;
    auto* teb=Data(pid);teb[1]=stacks[slot]+0x10000;teb[2]=stacks[slot]+4096;teb[8]=pid;teb[9]=tid;return 0;
}
uint32_t ProcessCreatePeChild(uint32_t parent,const char* name,const char* command,bool suspended,uint32_t& pid,uint32_t& tid,uint64_t& processHandle,uint64_t& threadHandle) noexcept {
    auto e=telemetry::Begin(telemetry::Domain::Process,telemetry::Operation::Create,parent,manager.Get(parent)?manager.Get(parent)->token.user:0,name,command);
    if(!telemetry::Before(e)){pid=tid=0;processHandle=threadHandle=0;return telemetry::End(e,processes::Denied);}
    const auto status=ProcessCreatePeChildRaw(parent,name,command,suspended,pid,tid,processHandle,threadHandle);if(!status)e.related=pid;return telemetry::End(e,status);
}
void PeChildRollbackChecks(uint32_t parent) noexcept {
    const auto baseline=manager.Counts();const auto free=memoryManager.physical.Statistics().free;uint32_t checks=0;
    const auto clean=[&]() noexcept {const auto c=manager.Counts();return c.consistent && c.processes==baseline.processes && c.objects==baseline.objects && c.handles==baseline.handles && c.references==baseline.references && c.committed==baseline.committed && c.threads==baseline.threads && memoryManager.physical.Statistics().free==free;};
    for(uint32_t kind=0;kind<2;++kind)for(uint32_t budget=0;budget<(kind?4u:6u);++budget){uint32_t pid=0,tid=0;uint64_t process=0,thread=0;
        manager.Budgets(kind?static_cast<int64_t>(budget):-1,kind?-1:static_cast<int64_t>(budget));const auto status=ProcessCreatePeChild(parent,"guardian-lifetime-child.exe","return",true,pid,tid,process,thread);manager.Budgets();
        Check(status!=0 && !pid && !tid && !process && !thread && clean(),0x30+(++checks));}
    uint32_t pid=0,tid=0;uint64_t process=0,thread=0;Check(ProcessCreatePeChild(parent,"guardian-lifetime-child.exe","return",true,pid,tid,process,thread)==0,0x40);
    KillProcess(pid,0);Garbage();Check(manager.Close(parent,thread)==0 && manager.Close(parent,process)==0 && clean(),0x41);
    const char message[]="PE child creation rollback 10 fault budgets PASS\n";for(char ch:message)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
}

#endif
#endif
void ProcessInitialize(const abi::BootInfo& boot,void (*check)(bool,uint32_t) noexcept) noexcept {
    verify=check;session=boot.bootId;result.magic=processes::Magic;result.version=processes::Version;result.bytes=sizeof(result);result.bootId=session;
    const auto* map=reinterpret_cast<const abi::MemoryRegion*>(boot.memoryMapGpa);
    Check(abi::Covered(map,boot.memoryMapCount,memory::RequestGpa,4*4096,abi::MemoryKind::BootData),3);
#ifdef GUARDIAN_COMPLETE_KERNEL
    Check(abi::Covered(map,boot.memoryMapCount,PeExceptionScratch,PeExceptionScratchBytes+4096,abi::MemoryKind::BootData),0x61);PeExceptionsInitialize();
#endif
    Check(memoryManager.physical.Initialize(0,boot.ramBytes,map,boot.memoryMapCount),4);result.freeBefore=memoryManager.physical.Statistics().free;
    memoryManager.Configure(nullptr,Permission,Change);Check(memoryManager.Create(kernelSpace),5);memoryManager.Activate(&kernelSpace);manager.Configure(memoryManager,scheduler);
#ifdef GUARDIAN_IO_KERNEL
    IoInitialize(manager,scheduler,verify);
#ifdef GUARDIAN_STORAGE_KERNEL
    StorageInitialize(manager,scheduler,verify);
#endif
#endif
    idle.frame.rip=UserAddress(&ProcessIdle);idle.frame.rsp=cpu::UserStack+cpu::UserStackBytes-0x28;idle.frame.cs=0x2B;idle.frame.ss=0x23;idle.frame.rflags=0x202;SeedContext(idle);
}
bool ProcessActive() noexcept {return running;}
void ProcessPublish() noexcept {Copy(reinterpret_cast<void*>(processes::ReportGpa),&result,sizeof(result));_ReadWriteBarrier();__outbyte(processes::Port,1);}
bool ProcessTrap(TrapFrame& frame,void* fx,uint64_t now,bool timer) noexcept {
    using namespace processes;
    const auto tid=scheduler.Current();telemetry::currentThread=tid;const auto pid=tid?manager.Owner(tid):0;telemetry::currentProcess=pid;
    if(tid)SaveContext(contexts[(tid&255u)-1],frame,fx);
    memoryManager.Activate(&kernelSpace);Check(scheduler.Advance(now),6);manager.Poll();
#ifdef GUARDIAN_NATIVE_NT
    PeNativeTick(now);
#endif
    if(timer){++result.ticks;
#ifdef GUARDIAN_IO_KERNEL
        IoTick();
#endif
        scheduler.Yield();}
    else if(frame.vector!=0x80){Check(pid!=0,7);
#ifdef GUARDIAN_COMPLETE_KERNEL
        const bool pe=PeOwnsProcess(pid);
        if(!pe || !PeHandleException(pid,tid,contexts[(tid&255u)-1])){KillProcess(pid,pe?PeExceptionCode(frame.vector):(0xE000u|static_cast<uint32_t>(frame.vector)),true);if(!pe)++result.faulted;}
#else
        KillProcess(pid,0xE000u|static_cast<uint32_t>(frame.vector));++result.faulted;
#endif
    }
    else if(!tid && frame.rax==0x60){Load(scheduler.Select(),frame,fx);return false;}
    else {
        Check(pid!=0,8);const auto slot=(tid&255u)-1;auto& c=contexts[slot];uint32_t status=Success;
#ifdef GUARDIAN_COMPLETE_KERNEL
        const bool continuation=frame.rax>=0x86 && frame.rax<=0x88 && PeOwnsProcess(pid)
#ifdef GUARDIAN_NATIVE_NT
            &&PeNativeLegacy(pid,frame.rip)
#endif
            ;
#endif
        bool validCode=frame.rip>=cpu::UserCode && frame.rip<cpu::UserCode+4096;
#ifdef GUARDIAN_PE_KERNEL
        validCode=validCode || PeExecutable(pid,frame.rip);
#endif
        if(!validCode || frame.rsp<stacks[slot]+4096+32 || frame.rsp>=stacks[slot]+0x10000){KillProcess(pid,Invalid);++result.faulted;}
        else
#ifdef GUARDIAN_COMPLETE_KERNEL
        if(continuation){if(!PeHandleContinuation(pid,tid,frame,c))KillProcess(pid,PeExceptionFailure(pid,tid),true);}else
#endif
#ifdef GUARDIAN_RUNTIME_KERNEL
 #ifdef GUARDIAN_NATIVE_NT
        if(frame.rax==0xA3&&frame.rip>=cpu::UserCode&&frame.rip<cpu::UserCode+4096&&nativeInApc[slot]){c=nativeApcContexts[slot];nativeInApc[slot]=false;status=uint32_t(c.frame.rax);}else
        if(PeNativeDispatch(pid,tid,now,frame,tebs[slot],status,nativeInApc[slot])){}else
 #endif
        if(PeRuntimeDispatch(pid,tid,now,frame,c,status)){}else
#endif
#ifdef GUARDIAN_IO_KERNEL
        if(IoDispatch(pid,tid,frame,c,status)){}else
#ifdef GUARDIAN_STORAGE_KERNEL
        if(StorageDispatch(pid,tid,frame,c,status)){}else
#endif
#endif
        switch(frame.rax){
        case 0x40:scheduler.Yield();break;
        case 0x41:
#ifdef GUARDIAN_PE_KERNEL
            if(PeOwnsProcess(pid)){
#ifdef GUARDIAN_LIFETIME_KERNEL
                if(PeIsRoot(pid))Check(frame.rdx==0,0x100+static_cast<uint32_t>(frame.rdx&255));
                KillProcess(pid,static_cast<uint32_t>(frame.rdx));
#else
                Check(frame.rdx==0,0x100+static_cast<uint32_t>(frame.rdx&255));KillProcess(pid,0);
#endif
                break;}
#endif
            if(frame.rdx){failed=true;for(uint32_t i=0;i<MaxProcesses;++i)if(auto* p=manager.At(i);p && p->state==State::Live)KillProcess(p->id,static_cast<uint32_t>(frame.rdx));}
            else KillProcess(pid,0);++result.completed;break;
#ifdef GUARDIAN_RUNTIME_KERNEL
        case 0x82:
            if(!PeOwnsProcess(pid) || !tebs[slot] || frame.rdx>UINT32_MAX)status=Invalid;
            else {
#ifdef GUARDIAN_LIFETIME_KERNEL
                if(manager.Get(pid)->threadCount==1)status=ProcessRedirectPeExit(pid,tid,static_cast<uint32_t>(frame.rdx),true);
                else
#endif
                status=manager.ThreadExit(tid,static_cast<uint32_t>(frame.rdx));}break;
#endif
        case 0x42:{uint64_t handle=0;status=frame.rdx>UINT32_MAX || frame.r8>UINT32_MAX?Invalid:manager.OpenProcess(pid,static_cast<uint32_t>(frame.rdx),static_cast<uint32_t>(frame.r8),handle);c.frame.rdx=handle;break;}
        case 0x43:{char name[74]{};uint64_t handle=0;status=frame.r8>UINT32_MAX || frame.r9>static_cast<uint32_t>(Kind::Token) || !ReadName(pid,frame.rdx,name)?Invalid:manager.Open(pid,static_cast<Kind>(frame.r9),name,static_cast<uint32_t>(frame.r8),handle);c.frame.rdx=handle;break;}
        case 0x44:status=manager.Signal(pid,frame.rdx);break;
        case 0x45:{if(frame.r8!=threads::Never && frame.r8>=threads::Never-now)status=Invalid;else {const auto deadline=frame.r8==threads::Never?threads::Never:now+frame.r8;status=manager.WaitHandles(pid,tid,&frame.rdx,1,false,deadline);}break;}
        case 0x46:case 0x47:status=frame.r10>4096?Invalid:manager.CopyProcess(pid,frame.rdx,frame.r8,frame.r9,static_cast<uint32_t>(frame.r10),frame.rax==0x47);if(!status)++result.remoteCopies;break;
        case 0x48:status=manager.Close(pid,frame.rdx);break;
        case 0x49:{Information info{};status=manager.QueryHandle(pid,frame.rdx,info);if(!status){c.frame.rdx=info.access;c.frame.r8=info.kind;}break;}
        case 0x4A:{uint64_t handle=0;status=frame.r9>UINT32_MAX || frame.r10>3?Invalid:manager.DuplicateHandle(pid,frame.rdx,frame.r8,static_cast<uint32_t>(frame.r9),static_cast<uint32_t>(frame.r10),handle);c.frame.rdx=handle;break;}
        case 0x4B:{uint32_t child=0;uint64_t handle=0;status=Spawn(pid,frame.rdx,child,handle);c.frame.rdx=child;c.frame.r8=handle;break;}
        case 0x4C:{Token token{};status=manager.QueryToken(pid,manager.Get(pid)->tokenHandle,token);c.frame.rdx=token.user;c.frame.r8=token.integrity;c.frame.r9=token.privileges;break;}
        case 0x4D:{Object* object=nullptr;status=manager.Resolve(pid,frame.rdx,Kind::Process,Query,object);if(!status)c.frame.rdx=object->exitCode;break;}
        case 0x50:if(frame.rdx>=threads::Never-now)status=Invalid;else status=scheduler.Delay(tid,now+frame.rdx);break;
        case 0x51:c.frame.rdx=result.ticks;break;
        case 0x52:{char name[74]{};uint64_t handle=0;
            if(frame.r8>UINT32_MAX || frame.r9>3 || (frame.rdx && !ReadName(pid,frame.rdx,name)))status=Invalid;
            else status=manager.CreateObject(pid,Kind::Event,frame.rdx?name:nullptr,nullptr,static_cast<uint32_t>(frame.r8),static_cast<uint32_t>(frame.r9),handle);
            c.frame.rdx=handle;break;}
        case 0x53:{Security sd{};status=Read(pid,frame.r8,&sd,sizeof(sd))?manager.SetDacl(pid,frame.rdx,sd):Invalid;break;}
        case 0x54:status=frame.r8>1?Invalid:manager.Unlock(pid,frame.rdx,tid,frame.r8!=0);break;
        default:status=0xC0000002;break;
        }
        if(status==Denied || status==InvalidHandle)++result.denied;
#ifdef GUARDIAN_COMPLETE_KERNEL
        if(!continuation)
#endif
        c.frame.rax=status;waiting[slot]=status==threads::Pending;
        if(auto* thread=scheduler.Get(tid);thread && thread->state!=threads::State::Dead && thread->state!=threads::State::Waiting)thread->result=status;
    }
    Garbage();if(!manager.Counts().processes){running=false;return true;}
    Load(scheduler.Select(),frame,fx);return false;
}
void ProcessSelfTest() noexcept {
    using namespace processes;
    result.coreChecks=ProcessChecks(manager,memoryManager,scheduler,CoreCheck);Check(result.coreChecks>400,9);result.checks|=0x7FE0;
#ifdef GUARDIAN_PE_KERNEL
    Check(PeMappingChecks(manager,memoryManager,verify)==26,20);
    {
        const auto free=memoryManager.physical.Statistics().free;Token peToken{};peToken.user=100;peToken.primaryGroup=10;peToken.integrity=2;peToken.session=1;peToken.privileges=CreateGlobal;
        uint32_t pePid=0,peTid=0;uint64_t startup=0;
        Check(manager.CreateRoot(peToken,pePid)==Success && AddThread(pePid,0,&peTid)==Success,23);
#if defined(GUARDIAN_RUNTIME_KERNEL) && defined(GUARDIAN_IO_KERNEL)
        IoBegin(pePid);
#ifdef GUARDIAN_STORAGE_KERNEL
        StorageBegin(pePid);
#endif
#endif
#ifdef GUARDIAN_ANALYSIS_KERNEL
        const char* baselinePaths[3]={"C:\\a.txt","C:\\b.txt","C:\\c.txt"};for(const char* path:baselinePaths){
            storage::Command previous{};previous.op=storage::Op::Unlink;uint32_t n=0;for(;path[n]&&n<118;++n)previous.path[n]=path[n];const char suffix[]=".locked";for(uint32_t i=0;i<sizeof(suffix);++i)previous.path[n+i]=suffix[i];
            const auto removed=StorageExecute(pePid,previous);Check(!removed||removed==storage::NotFound,0x96);
            storage::Command file{};file.op=storage::Op::Open;telemetry::Text(file.path,path);file.access=storage::All;file.share=7;file.disposition=2;Check(StorageExecute(pePid,file)==0,0x90);
            const auto handle=file.handle;file={};file.op=storage::Op::Write;file.handle=handle;const char initial[]="Original synthetic document\n";file.length=sizeof(initial)-1;for(uint32_t i=0;i<file.length;++i)file.data[i]=initial[i];Check(StorageExecute(pePid,file)==0,0x91);
            file={};file.op=storage::Op::Close;file.handle=handle;Check(StorageExecute(pePid,file)==0,0x92);
        }
        storage::Command key{};key.op=storage::Op::RegCreate;telemetry::Text(key.path,"HKCU\\Lab\\Run");Check(StorageExecute(pePid,key)==0,0x93);
#endif
        if(GuardianProcessTelemetryCapability[0]=='G')TelemetryInitialize(session);telemetry::currentThread=telemetry::currentProcess=0;
#ifdef GUARDIAN_ANALYSIS_KERNEL
        Check(NetworkRuntimeInitialize(session),0x97);
#endif
        auto actor=telemetry::Begin(telemetry::Domain::Process,telemetry::Operation::Query,pePid,100,"owned application root");telemetry::Before(actor);telemetry::End(actor,0);
#ifdef GUARDIAN_PE_GUI_KERNEL
        GraphicsInitialize();
#endif
        Check(PePrepareProcess(manager,memoryManager,pePid,startup),24);
#ifdef GUARDIAN_NATIVE_NT
        Check(PeNativePrepare(pePid,scheduler),0xA1);
#endif
        const auto slot=(peTid&255u)-1;auto& c=contexts[slot];c.frame.rip=UserAddress(&PeProcessBootstrap);c.frame.rcx=startup;c.gs=PeInitialTeb(pePid);c.fs=0;
#ifdef GUARDIAN_RUNTIME_KERNEL
        tebs[slot]=PeInitialTeb(pePid);startups[slot]=startup;
#endif
        auto* teb=
#ifdef GUARDIAN_SCALABLE_PE
            static_cast<uint64_t*>(memoryManager.physical.At(manager.Translate(pePid,PeInitialTeb(pePid),true)));
#else
            Data(pePid);
#endif
        teb[1]=stacks[slot]+0x10000;teb[2]=stacks[slot]+4096;teb[8]=pePid;teb[9]=peTid;
#if defined(GUARDIAN_LIFETIME_KERNEL) && !defined(GUARDIAN_SCALABLE_PE)
        PeChildRollbackChecks(pePid);
#endif
        running=true;ProcessRun(UserAddress(&ProcessBootstrap),cpu::UserStack+cpu::UserStackBytes-0x28);telemetry::currentThread=telemetry::currentProcess=0;
        Check(!running && PeProcessPassed() && memoryManager.physical.Statistics().free==free && scheduler.AllDead(),25);
#if defined(GUARDIAN_ANALYSIS_KERNEL) || defined(GUARDIAN_PE_GUI_KERNEL)
        // Dedicated workloads return after their root PE; independent images
        // retain the additional legacy CPL3 acceptance programs.
        ++result.completed;
#ifdef GUARDIAN_STORAGE_KERNEL
        StorageFinish();
#endif
#ifdef GUARDIAN_IO_KERNEL
        IoFinish();
#endif
        const auto counts=manager.Counts();
        Check(counts.consistent&&!counts.processes&&!counts.objects&&!counts.handles&&!counts.references&&!counts.threads,0x94);
        result.freeAfter=memoryManager.physical.Statistics().free;
        Check(result.freeAfter+21==result.freeBefore&&scheduler.AllDead(),0x95);
        result.liveProcesses=counts.processes;result.liveObjects=counts.objects;result.liveHandles=counts.handles;result.liveReferences=counts.references;
#ifdef GUARDIAN_PE_GUI_KERNEL
        GraphicsComplete();result.checks=1;
#else
        result.checks=telemetry::AnalysisChecks;
#endif
        return;
#endif
#if !defined(GUARDIAN_ANALYSIS_KERNEL) && !defined(GUARDIAN_PE_GUI_KERNEL)
        const char message[]="PE console TLS DLL execution PASS\n";for(char ch:message)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
#ifdef GUARDIAN_RUNTIME_KERNEL
        const char threadMessage[]="PE private TEB TLS thread lifecycle contention PASS\n";for(char ch:threadMessage)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
#ifdef GUARDIAN_LIFETIME_KERNEL
        const char unwindMessage[]="PE x64 virtual stack capture PASS\n";for(char ch:unwindMessage)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
#ifdef GUARDIAN_COMPLETE_KERNEL
        const char exceptionMessage[]="PE x64 exception dispatch recovery nonlocal unwind PASS\n";for(char ch:exceptionMessage)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
#endif
#ifdef GUARDIAN_COMPLETE_KERNEL
        const char dynamicMessage[]="PE dynamic DLL TLS references rollback unload PASS\n";for(char ch:dynamicMessage)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
#endif
        const char lifeMessage[]="PE child processes explicit exits last-thread lifetime PASS\n";for(char ch:lifeMessage)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
#endif
        const char apiMessage[]="PE runtime services heap locks PASS\n";for(char ch:apiMessage)if(ch)__outbyte(0xE9,static_cast<uint8_t>(ch));
#endif
#endif
    }
#endif
#if !defined(GUARDIAN_ANALYSIS_KERNEL) && !defined(GUARDIAN_PE_GUI_KERNEL)
    Token token{};token.user=100;token.primaryGroup=10;token.session=1;token.integrity=2;token.privileges=CreateGlobal;
    Check(manager.CreateRoot(token,parentPid)==Success,10);token.user=200;token.session=2;Check(manager.CreateRoot(token,badPid)==Success,11);
    Check(InitializeData(parentPid,0x1111) && InitializeData(badPid,0x2222),12);

#ifdef GUARDIAN_IO_KERNEL
#ifndef GUARDIAN_RUNTIME_KERNEL
    IoBegin(parentPid);
#ifdef GUARDIAN_STORAGE_KERNEL
    StorageBegin(parentPid);
#endif
#endif
#endif
    auto sd=Manager::DefaultSecurity(manager.Get(parentPid)->token,Kind::Event);sd.aceCount=2;sd.aces[1]={Principal::Everyone,0,Wait|Query,0};
    uint64_t full=0,read=0;Check(manager.CreateObject(parentPid,Kind::Event,"\\Global\\ProcessReady",&sd,GenericAll,0,full)==Success,13);
    Check(manager.DuplicateHandle(parentPid,full,manager.Get(parentPid)->self,Wait|Query,Inherit,read)==Success,14);
    Data(parentPid)[2]=read;Data(parentPid)[3]=full;Data(badPid)[3]=full;
    Check(AddThread(parentPid,0)==Success && AddThread(badPid,1)==Success && AddThread(badPid,3)==Success,15);
    running=true;ProcessRun(UserAddress(&ProcessBootstrap),cpu::UserStack+cpu::UserStackBytes-0x28);telemetry::currentThread=telemetry::currentProcess=0;
#ifdef GUARDIAN_IO_KERNEL
#ifdef GUARDIAN_STORAGE_KERNEL
    StorageFinish();
#endif
    IoFinish();
#endif
    const auto counts=manager.Counts();Check(!running && !failed && counts.consistent && !counts.processes && !counts.objects && !counts.handles && !counts.references,16);
    Check(result.completed==2 && result.faulted==1 && result.inheritance==1 && result.remoteCopies==2 && result.denied>=6 && result.ticks>=4,17);
    result.freeAfter=memoryManager.physical.Statistics().free;Check(result.freeAfter+21==result.freeBefore && scheduler.AllDead(),18);
    result.liveProcesses=counts.processes;result.liveObjects=counts.objects;result.liveHandles=counts.handles;result.liveReferences=counts.references;
    result.checks|=0x1F;Check(result.checks==RequiredChecks,19);
#endif
}

#ifdef GUARDIAN_COMPLETE_KERNEL
uint32_t ProcessStopPeChild(uint32_t parent,uint32_t child) noexcept {
    const auto* p=manager.Get(child);if(!p||p->parent!=parent||p->state!=processes::State::Live)return processes::NotFound;KillProcess(child,0);return 0;
}
uint32_t ProcessPeTebs(uint32_t pid,uint32_t* tids,uint64_t* output,uint32_t capacity) noexcept {
    const auto* p=manager.Get(pid);if(!p)return 0;uint32_t count=0;
    for(auto tid:p->threads)if(tid){const auto index=(tid&255u)-1;const auto* t=scheduler.Get(tid);
        if(index<threads::MaxThreads && owners[index]==pid && tebs[index] && t && t->state!=threads::State::Dead){
            if(count==capacity)return 0;tids[count]=tid;output[count++]=tebs[index];}}
    return count;
}
#endif
#ifdef GUARDIAN_NATIVE_NT
uint32_t ProcessNativeTerminate(uint32_t pid,uint32_t tid,uint32_t code,bool whole) noexcept {
    const auto* p=manager.Get(pid);if(!p||manager.Owner(tid)!=pid)return processes::InvalidHandle;
    if(whole||p->threadCount==1){KillProcess(pid,code);return 0;}return manager.ThreadExit(tid,code);
}
#endif
