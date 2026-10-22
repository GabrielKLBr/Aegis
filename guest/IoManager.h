#pragma once
#include "guardian/Io.h"
#include "Scheduler.h"
namespace guardian::io {
using Dispatch=uint32_t (*)(void*,uint32_t,Op,uint64_t,const uint8_t*,uint32_t,uint8_t*,uint32_t&) noexcept;
struct Driver {
    uint32_t version=Version,bytes=sizeof(Driver),machine=abi::X64,device=0,flags=0;
    bool (*start)(void*,uint32_t) noexcept=nullptr;
    void (*stop)(void*,uint32_t) noexcept=nullptr;
    Dispatch dispatch=nullptr;void* context=nullptr;
};
enum class State:uint8_t {Empty,Queued,Active,Complete};
struct Request {uint32_t id=0,generation=0,owner=0,device=0,pin=0,completion=0,status=Pending,transferred=0;uint64_t offset=0,order=0;Op op=Op::Read;uint32_t bytes=0;State state=State::Empty;uint8_t data[MaxTransfer]{};};
struct DriverSlot {Driver definition{};uint32_t id=0,generation=0;bool live=false,failed=false;};
struct Counts {uint64_t drivers=0,requests=0,pins=0,submitted=0,completed=0,cancelled=0,failed=0,loads=0,unloads=0;bool consistent=true;};
class Manager {
public:
    void Configure(threads::Scheduler& s,void* context=nullptr,void (*release)(void*,uint32_t) noexcept=nullptr,bool (*busy)(void*,uint32_t) noexcept=nullptr) noexcept {scheduler_=&s;context_=context;release_=release;busy_=busy;}
    uint32_t Load(const Driver&,uint32_t& id) noexcept;
    uint32_t Unload(uint32_t id) noexcept;
    uint32_t Submit(uint32_t owner,uint32_t device,Op,const uint8_t*,uint32_t bytes,uint64_t offset,uint32_t pin,uint32_t& id) noexcept;
    uint32_t Cancel(uint32_t owner,uint32_t id) noexcept;
    uint32_t Collect(uint32_t owner,uint32_t id,uint8_t* out,uint32_t capacity,uint32_t& transferred) noexcept;
    uint32_t Wait(uint32_t owner,uint32_t id,uint32_t tid) noexcept;
    void Exit(uint32_t owner) noexcept;
    void Pump(uint32_t maximum=MaxRequests) noexcept;
    const Request* Get(uint32_t owner,uint32_t id) const noexcept;
    Counts Statistics() const noexcept;
private:
    uint32_t LoadRaw(const Driver&,uint32_t&) noexcept;
    uint32_t UnloadRaw(uint32_t) noexcept;
    Request* Find(uint32_t owner,uint32_t id) noexcept;
    void Complete(Request&,uint32_t,uint32_t) noexcept;
    void Drop(Request&) noexcept;
    DriverSlot drivers_[DeviceCount]{};Request requests_[MaxRequests]{};
    threads::Scheduler* scheduler_=nullptr;void* context_=nullptr;
    void (*release_)(void*,uint32_t) noexcept=nullptr;bool (*busy_)(void*,uint32_t) noexcept=nullptr;
    Counts counts_{};uint64_t order_=0;
};
uint64_t IoChecks(Manager&,threads::Scheduler&,void (*check)(bool,uint32_t) noexcept) noexcept;
}
