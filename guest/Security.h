#pragma once
#include "guardian/Security.h"
#include "Volume.h"
namespace guardian::security {
struct Bridge {void* context=nullptr;uint32_t (*start)(void*,uint32_t,const char*,uint32_t&,uint64_t&) noexcept=nullptr;uint32_t (*stop)(void*,uint32_t,uint32_t) noexcept=nullptr;uint64_t (*clock)(void*) noexcept=nullptr;};
class Core {
public:
    bool Initialize(processes::Manager&,threads::Scheduler&,storage::Volume&,Bridge) noexcept;
    uint32_t Execute(uint32_t pid,uint32_t tid,Request&) noexcept;
    uint32_t Close(uint32_t pid,uint64_t handle,uint32_t kind=0) noexcept;
    void Exit(uint32_t pid) noexcept;
    bool Finish() noexcept;
    uint32_t Handles() const noexcept;
private:
    struct Handle {uint64_t id=0,raw=0,parent=0;uint32_t pid=0,kind=0,access=0,index=0,generation=0;char path[128]{};};
    struct Service {bool used=false,deleted=false;uint32_t object=0,creator=0,control=0;ServiceConfig config{};ServiceStatus status{};};
    Handle* Find(uint32_t,uint64_t,uint32_t=0) noexcept;
    Handle* New(uint32_t,uint32_t,uint32_t) noexcept;
    uint32_t Registry(uint32_t,Request&) noexcept;
    uint32_t Tokens(uint32_t,Request&) noexcept;
    uint32_t Services(uint32_t,Request&) noexcept;
    uint32_t Events(uint32_t,Request&) noexcept;
    void Poll() noexcept;
    uint32_t Collect(uint32_t) noexcept;
    uint32_t ServicePath(const char*,char*) noexcept;
    uint32_t SaveService(uint32_t,const processes::Token&,bool) noexcept;
    processes::Manager* processes_=nullptr;threads::Scheduler* scheduler_=nullptr;storage::Volume* volume_=nullptr;Bridge bridge_{};
    Handle handles_[MaxHandles]{};Service services_[MaxServices]{};uint64_t nextHandle_=0x5365000000000100ull;
    uint32_t eventSequence_=0,eventCount_=0;struct Event {uint32_t number=0,id=0,type=0,user=0,time=0;char source[32]{},text[128]{};};Event events_[MaxEvents]{};
};
}
