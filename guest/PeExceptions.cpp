#include "PeExceptions.h"
#include "guardian/Threads.h"
namespace {
using namespace guardian;
struct State {uint32_t pid=0,tid=0,code=0,failure=exceptions::InvalidDisposition;bool active=false,noncontinuable=false;uint64_t address=0,token=0,targetIp=0;exceptions::Dispatch dispatch{};ExecutionContext original{};};
using StateArray=State[threads::MaxThreads];
static_assert(sizeof(StateArray)<=PeExceptionScratchBytes);
StateArray& States() noexcept {return *reinterpret_cast<StateArray*>(PeExceptionScratch);}
uint64_t nextToken=0;
exceptions::Context Export(const ExecutionContext& saved) noexcept {
    exceptions::Context c{};const auto& f=saved.frame;
    const uint64_t gpr[16]={f.rax,f.rcx,f.rdx,f.rbx,f.rsp,f.rbp,f.rsi,f.rdi,f.r8,f.r9,f.r10,f.r11,f.r12,f.r13,f.r14,f.r15};
    for(uint32_t i=0;i<16;++i)c.gpr[i]=gpr[i];c.rip=f.rip;c.eflags=static_cast<uint32_t>(f.rflags);
    for(uint32_t i=0;i<512;++i)c.fx[i]=saved.fx[i];c.mxcsr=0;for(uint32_t i=0;i<4;++i)c.mxcsr|=uint32_t(c.fx[24+i])<<(8*i);return c;
}
void Import(ExecutionContext& saved,const exceptions::Context& c) noexcept {
    auto& f=saved.frame;f.rax=c.gpr[0];f.rcx=c.gpr[1];f.rdx=c.gpr[2];f.rbx=c.gpr[3];f.rsp=c.gpr[4];f.rbp=c.gpr[5];f.rsi=c.gpr[6];f.rdi=c.gpr[7];
    f.r8=c.gpr[8];f.r9=c.gpr[9];f.r10=c.gpr[10];f.r11=c.gpr[11];f.r12=c.gpr[12];f.r13=c.gpr[13];f.r14=c.gpr[14];f.r15=c.gpr[15];f.rip=c.rip;f.rflags=c.eflags;
    for(uint32_t i=0;i<512;++i)saved.fx[i]=c.fx[i];for(uint32_t i=0;i<4;++i)saved.fx[24+i]=static_cast<uint8_t>(c.mxcsr>>(8*i));
}
State* Slot(uint32_t pid,uint32_t tid) noexcept {
    const auto index=(tid&255u)-1;if(index>=threads::MaxThreads)return nullptr;auto& s=States()[index];
    if(s.pid!=pid || s.tid!=tid){s={};s.pid=pid;s.tid=tid;}return &s;
}
bool Advance(State& s,ExecutionContext& saved,const PeExceptionEnvironment& env) noexcept {
    exceptions::Action action{};const auto result=s.dispatch.Next(env.api,env.context,action);
    if(result==exceptions::Result::Resume){const auto& c=s.dispatch.ResumeContext();
        if(!exceptions::Dispatch::ValidContext(c,env.stack,env.api,env.context))return false;
        saved=s.original;Import(saved,c);s.active=false;return true;}
    if(result!=exceptions::Result::Call){s.failure=result==exceptions::Result::Unhandled?s.code:exceptions::InvalidDisposition;return false;}
    exceptions::Packet packet{};packet.record=action.record;packet.context=Export(s.original);
    if(action.record.flags&exceptions::Unwinding){packet.context.rip=action.frame.rip;
        for(uint32_t i=0;i<16;++i){packet.context.gpr[i]=action.frame.gpr[i];for(uint32_t j=0;j<16;++j)packet.context.fx[160+i*16+j]=action.frame.xmm[i][j];}}
    const auto& h=action.handler;auto& d=packet.dispatcher;
    d.controlPc=action.frame.rip;d.imageBase=h.imageBase;d.function=s.address+offsetof(exceptions::Packet,function);
    d.establisher=h.establisher;d.context=s.address+offsetof(exceptions::Packet,context);d.handler=h.routine;d.data=h.data;
    packet.function={static_cast<uint32_t>(h.functionBegin-h.imageBase),static_cast<uint32_t>(h.functionEnd-h.imageBase),static_cast<uint32_t>(h.unwindInfo-h.imageBase)};
    packet.routine=h.routine;packet.token=s.token;
    // The target is carried privately by the dispatch engine; its selected
    // continuation is copied only for the language handler's scope decision.
    if(action.record.flags&exceptions::Unwinding)d.targetIp=s.targetIp;
    if(!env.entry || !env.api.executable(env.context,env.entry) || !env.write(env.context,s.address,&packet,sizeof(packet)))return false;
    saved=s.original;saved.frame.rip=env.entry;saved.frame.rsp=s.address-40;saved.frame.rcx=s.address;saved.frame.rflags=0x202;return true;
}
bool Begin(State& s,ExecutionContext& saved,const PeExceptionEnvironment& env,const exceptions::Record& record) noexcept {
    if(s.active || saved.frame.rsp<env.stack.low+sizeof(exceptions::Packet)+4096 || saved.frame.rsp>=env.stack.high)return false;
    s.dispatch={};s.original=saved;s.address=(saved.frame.rsp-sizeof(exceptions::Packet)-64)&~15ull;
    s.code=record.code;s.noncontinuable=(record.flags&exceptions::Noncontinuable)!=0;s.failure=exceptions::InvalidDisposition;s.token=++nextToken;if(!s.token)return false;s.active=true;
    if(!s.dispatch.Begin(record,Export(saved),env.stack))return false;return Advance(s,saved,env);
}
}
uint32_t PeExceptionCode(uint64_t vector) noexcept {
    return vector==0?guardian::exceptions::IntegerDivide:(vector==6?guardian::exceptions::IllegalInstruction:(vector==14?guardian::exceptions::AccessViolation:0xC0000096u));
}
bool PeExceptionFault(uint32_t pid,uint32_t tid,ExecutionContext& saved,const PeExceptionEnvironment& env) noexcept {
    auto* s=Slot(pid,tid);if(!s)return false;guardian::exceptions::Record r{};r.code=PeExceptionCode(saved.frame.vector);r.address=saved.frame.rip;
    if(saved.frame.vector==14){r.count=2;r.information[0]=(saved.frame.error&16)?8:((saved.frame.error&2)?1:0);r.information[1]=GUARDIAN_FAULT_ADDRESS();}
    return Begin(*s,saved,env,r);
}
bool PeExceptionCall(uint32_t pid,uint32_t tid,const TrapFrame& f,ExecutionContext& saved,const PeExceptionEnvironment& env) noexcept {
    auto* s=Slot(pid,tid);if(!s)return false;s->failure=guardian::exceptions::InvalidDisposition;
    if(f.rax==0x88){guardian::exceptions::Record r{};if(!env.api.read(env.context,f.rdx,&r,sizeof(r)))return false;r.address=f.rip;return Begin(*s,saved,env,r);}
    if(!s->active)return false;
    if(f.rax==0x86){
        if(f.rdx!=s->token || f.r9!=s->address || f.r8>1)return false;
        if(!f.r8 && s->noncontinuable){s->failure=guardian::exceptions::NoncontinuableException;return false;}guardian::exceptions::Context changed{};
        if(!env.api.read(env.context,s->address+offsetof(guardian::exceptions::Packet,context),&changed,sizeof(changed)) ||
           !s->dispatch.Reply(static_cast<uint32_t>(f.r8),changed))return false;
    }else if(f.rax==0x87){
        if(!s->dispatch.UnwindTo(env.api,env.context,f.rdx,f.r8,f.r9)){s->failure=guardian::exceptions::InvalidUnwindTarget;return false;}
        // This private value is consumed when constructing the target callback.
        s->targetIp=f.r8;
    }else return false;
    return Advance(*s,saved,env);
}
bool PeExceptionBusy(uint32_t pid) noexcept {for(const auto& s:States())if(s.pid==pid && s.active)return true;return false;}
void PeExceptionExit(uint32_t pid) noexcept {for(auto& s:States())if(s.pid==pid)s={};}

uint32_t PeExceptionFailure(uint32_t pid,uint32_t tid) noexcept {auto* s=Slot(pid,tid);return s?s->failure:guardian::exceptions::InvalidDisposition;}

void PeExceptionsInitialize() noexcept {::new(reinterpret_cast<void*>(PeExceptionScratch)) StateArray{};nextToken=0;}
