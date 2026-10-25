#pragma once
#include "ProcessManager.h"
#include "guardian/Component.h"
namespace guardian::component {
class Core {
public:
    void Initialize(processes::Manager& p) noexcept {processes_=&p;}
    uint32_t Execute(uint32_t pid,uint32_t tid,Request&) noexcept;
    void Exit(uint32_t pid,uint32_t tid=0) noexcept;
    bool Clean() const noexcept;
private:
    struct Endpoint {uint64_t id=0;uint32_t pid=0,tid=0,kind=0;uint8_t iid[16]{};char name[64]{};};
    struct Call {uint64_t id=0,endpoint=0;uint32_t pid=0,tid=0,method=0,status=0,length=0,state=0;uint8_t data[Payload]{};};
    Endpoint endpoints_[Endpoints]{};Call calls_[Calls]{};uint64_t next_=1;
    processes::Manager* processes_=nullptr;
    Endpoint* Find(uint64_t) noexcept;
    void Drop(Endpoint&) noexcept;
};
}
