#include "ThreadKernel.h"
#include "ExecutionContext.h"
#include "Scheduler.h"
#include "MemoryManager.h"
#include "guardian/Validate.h"
#include <intrin.h>
using namespace guardian;
namespace {
memory::Manager memoryManager;
memory::Space space;
threads::Scheduler scheduler;
threads::Report result;
using Context=ExecutionContext;
Context contexts[threads::MaxThreads],apcContexts[threads::MaxThreads],idle;
bool inApc[threads::MaxThreads]{},running=false;
bool waiting[threads::MaxThreads]{};
uint64_t stacks[threads::MaxThreads]{},requestSequence=0,session=0;
uint32_t ids[threads::MaxThreads]{};
void (*verify)(bool,uint32_t) noexcept=nullptr;
void Check(bool b,uint32_t code) noexcept {verify(b,0x200+code);}
void CoreCheck(bool b,uint32_t code) noexcept {verify(b,0x400+code);}
void Copy(void* to,const void* from,uint64_t n) noexcept {auto* d=static_cast<volatile uint8_t*>(to);auto* s=static_cast<const uint8_t*>(from);for(uint64_t i=0;i<n;++i)d[i]=s[i];}
bool Permission(void*,uint64_t gpa,uint32_t access) noexcept {
    const memory::Request r{memory::Magic,memory::Version,sizeof(r),session,++requestSequence,gpa,abi::PageBytes,access,0};
    Copy(reinterpret_cast<void*>(memory::RequestGpa),&r,sizeof(r));_ReadWriteBarrier();__outbyte(memory::Port,1);return true;
}
void Change(void*,uint64_t root) noexcept {__writecr3(root);}
uint64_t UserAddress(const uint8_t* label) noexcept {return cpu::UserCode+reinterpret_cast<uint64_t>(label)-reinterpret_cast<uint64_t>(&ThreadUserStart);}
void Seed(Context& c) noexcept {SeedContext(c);}
uint64_t* Shared() noexcept {return reinterpret_cast<uint64_t*>(cpu::UserData);}
uint32_t Create(uint64_t entry,uint32_t priority,bool suspended,uint64_t mode) noexcept {
    if(entry<cpu::UserCode || entry>=cpu::UserCode+abi::PageBytes || priority>7) return 0;
    const auto id=scheduler.CreateThread(priority,suspended);if(!id)return 0;
    const auto slot=(id&255u)-1;uint64_t base=0;
    if(memoryManager.ReserveRange(space,base,0x10000,memory::ReadWrite)!=memory::Status::Success ||
        memoryManager.CommitRange(space,base+4096,0xF000,memory::ReadWrite)!=memory::Status::Success) {
        if(base)memoryManager.ReleaseRange(space,base);scheduler.Terminate(id,threads::NoMemory);scheduler.Reap(id);return 0;
    }
    stacks[slot]=base;ids[slot]=id;waiting[slot]=inApc[slot]=false;contexts[slot]={};auto& c=contexts[slot];Seed(c);
    c.frame.rip=entry;c.frame.rsp=base+0x10000-0x28;c.frame.cs=0x2B;c.frame.ss=0x23;c.frame.rflags=0x202;c.frame.rdi=slot;c.frame.rsi=mode;
    c.gs=cpu::UserData+0x100+slot*128;c.fs=c.gs+64;
    *reinterpret_cast<uint64_t*>(c.gs)=0x12340000+slot;*reinterpret_cast<uint64_t*>(c.fs)=0x56780000+slot;
    return id;
}
bool Readable(uint64_t at,uint64_t bytes) noexcept {
    if(!bytes || at>UINT64_MAX-bytes)return false;
    if(at>=cpu::UserData && at+bytes<=cpu::UserData+4096)return true;
    if(at<memory::Arena || at+bytes>memory::Arena+memory::ArenaBytes)return false;
    for(uint64_t p=at&~4095ull;p<at+bytes;p+=4096)if((memoryManager.Entry(space,p)&5)!=5)return false;
    return true;
}
uint64_t Deadline(uint64_t delta) noexcept {return delta==threads::Never || delta>=threads::Never-scheduler.Now()?threads::Never:scheduler.Now()+delta;}
void Load(uint32_t id,TrapFrame& f,void* fx) noexcept {
    auto& c=id?contexts[(id&255u)-1]:idle;
    if(id) {
        auto* t=scheduler.Get(id);const auto slot=(id&255u)-1;
        if(waiting[slot]) {c.frame.rax=t->result;waiting[slot]=false;}
        if(t->delivery && !inApc[slot]) {
            apcContexts[slot]=c;inApc[slot]=true;t->delivery=false;
            c.frame.rip=UserAddress(&ThreadApcDispatcher);c.frame.rdx=t->dispatched.routine;c.frame.rcx=t->dispatched.argument;
        }
    }
    RestoreContext(c,f,fx);++result.contextSwitches;
}
}
void ThreadInitialize(const abi::BootInfo& b,void (*check)(bool,uint32_t) noexcept) noexcept {
    verify=check;session=b.bootId;result.magic=threads::Magic;result.version=threads::Version;result.bytes=sizeof(result);result.bootId=session;
    const auto* map=reinterpret_cast<const abi::MemoryRegion*>(b.memoryMapGpa);
    Check(abi::Covered(map,b.memoryMapCount,memory::RequestGpa,3*abi::PageBytes,abi::MemoryKind::BootData),1);
    Check((__readcr4()&((1ull<<16)|(1ull<<18)|(1ull<<22)))==0,19); // No FSGSBASE, XSAVE/AVX or protection keys enabled.
    Check(memoryManager.physical.Initialize(0,b.ramBytes,map,b.memoryMapCount),2);result.freeBefore=memoryManager.physical.Statistics().free;
    memoryManager.Configure(nullptr,Permission,Change);Check(memoryManager.Create(space),3);memoryManager.Activate(&space);
    idle.frame.rip=UserAddress(&ThreadIdle);idle.frame.rsp=cpu::UserStack+cpu::UserStackBytes-0x28;
    idle.frame.cs=0x2B;idle.frame.ss=0x23;idle.frame.rflags=0x202;Seed(idle);
}
bool ThreadActive() noexcept {return running;}
void ThreadPublish() noexcept {Copy(reinterpret_cast<void*>(threads::ReportGpa),&result,sizeof(result));_ReadWriteBarrier();__outbyte(threads::Port,1);}
bool ThreadTrap(TrapFrame& f,void* fx,uint64_t now,bool timer) noexcept {
    using namespace threads;
    const auto current=scheduler.Current();
    if(current) {
        SaveContext(contexts[(current&255u)-1],f,fx);
    }
    Check(scheduler.Advance(now),4);
    if(timer) {++result.ticks;if(current)++result.preemptions;else ++result.idleTicks;scheduler.Yield();}
    else if(f.vector!=0x80) {Check(current!=0,5);scheduler.Terminate(current,0xE000u|static_cast<uint32_t>(f.vector));++result.faults;}
    else {
        if(!current && f.rax==0x30) {Load(scheduler.Select(),f,fx);return false;}
        Check(current!=0,6);++result.userSyscalls;uint32_t status=Success;auto& c=contexts[(current&255u)-1];
        const auto slot=(current&255u)-1;
        if(f.rip<cpu::UserCode || f.rip>=cpu::UserCode+4096 || f.rsp<stacks[slot]+4096+32 || f.rsp>=stacks[slot]+0x10000) {
            scheduler.Terminate(current,Invalid);++result.faults;
            if(scheduler.AllDead()) {running=false;return true;}Load(scheduler.Select(),f,fx);return false;
        }
        const auto a=static_cast<uint32_t>(f.rdx);uint32_t previous=0;
        // Nested alertable APC dispatch is not part of this bounded ABI.
        if(inApc[slot] && ((f.rax==0x22 && (f.r9&1)) || (f.rax==0x25 && f.r8) || (f.rax==0x27 && (f.r9&2)))) {
            c.frame.rax=0xC00000BB;scheduler.Get(current)->result=0xC00000BB;Load(scheduler.Select(),f,fx);return false;
        }
        switch(f.rax) {
        case 0x20:scheduler.Yield();break;
        case 0x21:scheduler.Terminate(current,a);++result.completed;break;
        case 0x22:status=f.rdx!=a || f.r9>3 || (f.r8!=Never && f.r8>=Never-now)?Invalid:scheduler.Wait(current,&a,1,false,Deadline(f.r8),(f.r9&1)!=0,(f.r9&2)?1u:0u);break;
        case 0x23:status=f.rdx!=a || f.r8>1?Invalid:scheduler.Unlock(current,a,f.r8!=0);break;
        case 0x24:status=f.rdx!=a || f.r8>INT32_MAX?Invalid:scheduler.Signal(a,static_cast<uint32_t>(f.r8));break;
        case 0x25:status=f.r8>1 || f.rdx>=Never-now?Invalid:scheduler.Delay(current,Deadline(f.rdx),f.r8!=0);break;
        case 0x26: {
            if(!inApc[slot])status=Invalid;
            else {c=apcContexts[slot];inApc[slot]=false;++result.apcs;status=UserApc;}
            break;
        }
        case 0x27: {
            uint32_t objects[MaxWait]{};
            if(f.r8==0 || f.r8>MaxWait || f.r9>3 || (f.r10!=Never && f.r10>=Never-now) || !Readable(f.rdx,f.r8*sizeof(uint32_t)))status=Invalid;
            else {Copy(objects,reinterpret_cast<const void*>(f.rdx),f.r8*sizeof(uint32_t));status=scheduler.Wait(current,objects,static_cast<uint32_t>(f.r8),(f.r9&1)!=0,Deadline(f.r10),(f.r9&2)!=0);}
            break;
        }
        case 0x29:status=f.rdx!=a?Invalid:scheduler.Suspend(a,previous);c.frame.rdx=previous;break;
        case 0x2A:status=f.rdx!=a?Invalid:scheduler.Resume(a,previous);c.frame.rdx=previous;break;
        case 0x2B:status=f.rdx!=a?Invalid:scheduler.CancelWait(a);break;
        case 0x2C:status=f.rdx!=a?Invalid:scheduler.Alert(a);break;
        case 0x2D:
            status=f.rdx!=a || f.r8<cpu::UserCode || f.r8>=cpu::UserCode+4096?Invalid:scheduler.QueueApc(a,f.r8,f.r9);break;
        case 0x2E:status=f.rdx!=a || f.r8>7?Invalid:scheduler.Priority(a,static_cast<uint32_t>(f.r8));break;
        case 0x2F: {
            if(f.r8>7 || f.r9>1 || f.rdx<cpu::UserCode || f.rdx>=cpu::UserCode+4096) {status=Invalid;break;}
            const auto id=Create(f.rdx,static_cast<uint32_t>(f.r8),f.r9!=0,0);status=id?Success:NoMemory;c.frame.rdx=id;
            c.frame.r8=id?scheduler.Get(id)->completion:0;break;
        }
        default:status=0xC0000002;break;
        }
        c.frame.rax=status;
        waiting[slot]=status==Pending;
        if(auto* t=scheduler.Get(current);t && t->state!=State::Dead && t->state!=State::Waiting)t->result=status;
    }
    if(scheduler.AllDead()) {running=false;return true;}
    Load(scheduler.Select(),f,fx);return false;
}
void ThreadSelfTest() noexcept {
    using namespace threads;
    result.coreChecks=SchedulerChecks(CoreCheck);Check(result.coreChecks>400,7);result.checks|=1|16|32|64|128|256|512|1024|2048|4096|8192;
    const auto baseline=memoryManager.physical.Statistics().free;memoryManager.physical.Budget(0);
    Check(!Create(cpu::UserCode,3,false,0),24);memoryManager.physical.Budget(-1);
    Check(memoryManager.physical.Statistics().free==baseline && scheduler.AllDead(),25);
    Shared()[4]=scheduler.CreateObject(Kind::AutoEvent,1);
    Shared()[5]=scheduler.CreateObject(Kind::Mutex);Shared()[6]=scheduler.CreateObject(Kind::Critical);Shared()[7]=scheduler.CreateObject(Kind::Srw);
    Shared()[8]=scheduler.CreateObject(Kind::Semaphore,1,2);Shared()[9]=scheduler.CreateObject(Kind::AutoTimer);
    Shared()[10]=scheduler.CreateObject(Kind::Mutex); // Deliberately abandoned by the faulting thread.
    auto* multiple=reinterpret_cast<uint32_t*>(cpu::UserData+128);multiple[0]=static_cast<uint32_t>(Shared()[4]);multiple[1]=static_cast<uint32_t>(Shared()[8]);
    for(uint64_t i=0;i<7;++i)Check(Create(cpu::UserCode,i==5?7u:3u,false,i)!=0,8);
    Check(scheduler.QueueApc(ids[4],UserAddress(&ThreadApcCallback),7)==Success,9);
    const auto* clock=reinterpret_cast<volatile const cpu::Clock*>(cpu::ClockGpa);
    Check(scheduler.Arm(static_cast<uint32_t>(Shared()[9]),clock->monotonicNs+2*cpu::PeriodNs)==Success,10);
    running=true;
    // A tiny bootstrap syscall loads the first complete context through the
    // same frame/FX path used for every later preemption.
    ThreadRun(UserAddress(&ThreadBootstrap),cpu::UserStack+cpu::UserStackBytes-0x28);
    Check(!running && scheduler.AllDead() && Shared()[3]==0 && result.faults==1 && result.completed==6 && result.apcs==1,12);
    Check(result.ticks>=4 && result.preemptions>=4 && result.contextSwitches>10 && Shared()[1]==32 && Shared()[2]==7,13);
    for(uint32_t i=0;i<4;++i)Check(Shared()[12+i]==8,20+i);
    for(uint32_t i=0;i<4;++i)result.counters[i]=*reinterpret_cast<volatile uint64_t*>(cpu::UserData+0x108+i*128);
    Check(result.counters[0]>0 && result.counters[1]>0 && result.counters[2]==16 && result.counters[3]==16,14);
    for(uint32_t i=0;i<MaxThreads;++i)if(ids[i]) {Check(scheduler.Reap(ids[i]),15);Check(memoryManager.ReleaseRange(space,stacks[i])==memory::Status::Success,16);ids[i]=0;stacks[i]=0;}
    // The sole initial thread creates a suspended child through the real ABI,
    // resumes it, then blocks. Timer IRQs must also wake an otherwise idle CPU.
    Check(Create(cpu::UserCode,3,false,7)!=0,26);running=true;
    ThreadRun(UserAddress(&ThreadBootstrap),cpu::UserStack+cpu::UserStackBytes-0x28);
    Check(!running && scheduler.AllDead() && result.idleTicks>0 && result.completed==8 && !Shared()[3],27);
    for(uint32_t i=0;i<MaxThreads;++i)if(ids[i]) {Check(scheduler.Reap(ids[i]),28);Check(memoryManager.ReleaseRange(space,stacks[i])==memory::Status::Success,29);ids[i]=0;stacks[i]=0;}
    result.freeAfter=memoryManager.physical.Statistics().free;Check(result.freeAfter+21==result.freeBefore,17);
    result.checks|=2|4|8|16384;Check(result.checks==RequiredChecks,18);
}
