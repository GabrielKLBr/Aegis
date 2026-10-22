#pragma once
#include "guardian/Validate.h"
#include "../state/State.h"
#include <span>
#include <string>
namespace av::microvm::boot {
class BootChannel {
public:
    explicit BootChannel(uint64_t bootId) : bootId_(bootId) {}
    bool Consume(std::span<const uint8_t> packet);
    void Save(state::Writer& w) const {w.Pod(bootId_);w.Pod(next_);w.Pod(messages_);w.Pod(status_);w.Pod(uint32_t(hello_)|(uint32_t(stopped_)<<1));w.Text(diagnostic_);w.Text(error_);}
    void Load(state::Reader& r,uint64_t boot){auto id=r.Pod<uint64_t>(),next=r.Pod<uint64_t>();auto messages=r.Pod<uint32_t>(),status=r.Pod<uint32_t>(),flags=r.Pod<uint32_t>();auto diagnostic=r.Text(16384),error=r.Text();
        state::Require(id==boot && next==uint64_t(messages)+1 && messages<=128 && flags<=3,"Invalid saved boot channel");bootId_=id;next_=next;messages_=messages;status_=status;hello_=(flags&1)!=0;stopped_=(flags&2)!=0;diagnostic_=std::move(diagnostic);error_=std::move(error);}
    bool Hello() const { return hello_; }
    bool Stopped() const { return stopped_; }
    uint32_t Status() const { return status_; }
    const std::string& Diagnostic() const { return diagnostic_; }
    const std::string& Error() const { return error_; }
private:
    bool Fail(const char* text) { error_ = text; return false; }
    uint64_t bootId_, next_ = 1;
    uint32_t messages_ = 0, status_ = UINT32_MAX;
    bool hello_ = false, stopped_ = false;
    std::string diagnostic_, error_;
};
}
