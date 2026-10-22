#include "ExceptionDispatch.h"
namespace guardian::exceptions {
unwind::Context Dispatch::Convert(const Context& c) noexcept {
    unwind::Context out{};out.rip=c.rip;for(uint32_t i=0;i<16;++i){out.gpr[i]=c.gpr[i];for(uint32_t j=0;j<16;++j)out.xmm[i][j]=c.fx[160+i*16+j];}return out;
}
bool Dispatch::ValidContext(const Context& c,const unwind::Stack& stack,const unwind::Backend& api,void* p) noexcept {
    uint32_t mxcsr=0;for(uint32_t i=0;i<4;++i)mxcsr|=uint32_t(c.fx[24+i])<<(8*i);
    return c.flags==ContextFlags && c.cs==0x2B && c.ss==0x23 && (c.eflags&~0xCD5u)==0x202 &&
        !(c.mxcsr&~0xFFBFu) && !(mxcsr&~0xFFBFu) && api.executable && api.executable(p,c.rip) &&
        stack.low<=stack.high && stack.high-stack.low>=40 && c.gpr[4]>=stack.low+32 && c.gpr[4]<stack.high && !(c.gpr[4]&7);
}
bool Dispatch::Begin(const Record& r,const Context& c,const unwind::Stack& s) noexcept {
    if(active_ || !r.code || (r.flags&~Noncontinuable) || r.nested || r.count>15 || r.padding || s.low>=s.high)return false;
    original_=c;record_=r;stack_=s;walk_=Convert(c);depth_=0;waiting_=unwinding_=resumed_=targetCall_=false;active_=true;return true;
}
void Dispatch::Move() noexcept {walk_=next_;if(walk_.rip)--walk_.rip;++depth_;waiting_=false;}
Result Dispatch::Next(const unwind::Backend& api,void* p,Action& action) noexcept {
    if(resumed_)return Result::Resume;if(!active_ || waiting_)return Result::Invalid;
    for(;depth_<32;){
        next_=walk_;unwind::Handler handler{};
        const auto status=unwind::InspectHandler(api,p,stack_,unwinding_?unwind::HandlerType::Termination:unwind::HandlerType::Exception,next_,handler);
        if(status==unwind::Status::End)return Result::Unhandled;
        if(status!=unwind::Status::Ok)return status==unwind::Status::Access?Result::Access:Result::Invalid;
        const bool target=unwinding_ && walk_.gpr[4]<=targetFrame_ && next_.gpr[4]>targetFrame_;
        if(target && handler.routine && handler.establisher!=targetFrame_)return Result::Invalid;
        if(handler.routine){current_=handler;waiting_=true;targetCall_=target;action={handler,walk_,record_};
            if(unwinding_)action.record.flags|=Unwinding|(target?TargetUnwind:0);return Result::Call;}
        if(target){resume_=original_;for(uint32_t i=0;i<16;++i){resume_.gpr[i]=target_.gpr[i];for(uint32_t j=0;j<16;++j)resume_.fx[160+i*16+j]=target_.xmm[i][j];}
            resume_.gpr[4]=targetFrame_;resume_.gpr[0]=value_;resume_.rip=targetIp_;active_=false;resumed_=true;return Result::Resume;}
        Move();
    }return Result::Limit;
}
bool Dispatch::Reply(uint32_t disposition,const Context& changed) noexcept {
    if(!active_ || !waiting_)return false;
    if(unwinding_){if(disposition!=1)return false;
        if(targetCall_){resume_=original_;for(uint32_t i=0;i<16;++i){resume_.gpr[i]=target_.gpr[i];for(uint32_t j=0;j<16;++j)resume_.fx[160+i*16+j]=target_.xmm[i][j];}
            resume_.gpr[4]=targetFrame_;resume_.gpr[0]=value_;resume_.rip=targetIp_;active_=false;waiting_=false;resumed_=true;return true;}
        Move();return true;
    }
    if(disposition==0){if(record_.flags&Noncontinuable)return false;resume_=changed;active_=waiting_=false;resumed_=true;return true;}
    if(disposition!=1)return false;Move();return true;
}
bool Dispatch::UnwindTo(const unwind::Backend& api,void* p,uint64_t frame,uint64_t ip,uint64_t value) noexcept {
    if(!active_ || !waiting_ || unwinding_ || (frame&7) || frame<stack_.low || frame>=stack_.high || !api.executable || !api.executable(p,ip))return false;
    // Validate the complete path and its termination metadata before cleanup.
    auto work=Convert(original_);bool found=false;
    for(uint32_t depth=0;depth<32;++depth){auto caller=work,termination=work;unwind::Handler e{},u{};
        if(unwind::InspectHandler(api,p,stack_,unwind::HandlerType::Exception,caller,e)!=unwind::Status::Ok ||
           unwind::InspectHandler(api,p,stack_,unwind::HandlerType::Termination,termination,u)!=unwind::Status::Ok)return false;
        if(e.routine && e.establisher==frame){if(ip<e.functionBegin || ip>=e.functionEnd)return false;target_=work;found=true;break;}
        work=caller;if(work.rip)--work.rip;
    }
    if(!found)return false;targetFrame_=frame;targetIp_=ip;value_=value;walk_=Convert(original_);depth_=0;
    unwinding_=true;waiting_=false;return true;
}
}
