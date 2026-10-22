#pragma once
#include "Unwind.h"
#include "guardian/Exception.h"
namespace guardian::exceptions {
enum class Result:uint32_t {Call,Resume,Unhandled,Invalid,Access,Limit};
struct Action {unwind::Handler handler{};unwind::Context frame{};Record record{};};
class Dispatch {
public:
    bool Begin(const Record&,const Context&,const unwind::Stack&) noexcept;
    Result Next(const unwind::Backend&,void*,Action&) noexcept;
    bool Reply(uint32_t disposition,const Context&) noexcept;
    bool UnwindTo(const unwind::Backend&,void*,uint64_t frame,uint64_t ip,uint64_t value) noexcept;
    const Context& ResumeContext() const noexcept {return resume_;}
    static bool ValidContext(const Context&,const unwind::Stack&,const unwind::Backend&,void*) noexcept;
private:
    static unwind::Context Convert(const Context&) noexcept;
    void Move() noexcept;
    Context original_{},resume_{};Record record_{};unwind::Stack stack_{};
    unwind::Context walk_{},next_{},target_{};unwind::Handler current_{};
    uint64_t targetFrame_=0,targetIp_=0,value_=0;uint32_t depth_=0;
    bool active_=false,waiting_=false,unwinding_=false,resumed_=false,targetCall_=false;
};
}
