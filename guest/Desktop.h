#pragma once
#include "guardian/Desktop.h"
#include "ProcessManager.h"
namespace guardian::desktop {
struct Surface {Window info{};uint64_t z=0;uint32_t background=0xFFFFFF,drawings=0;bool dirty=true,painting=false;Rect invalid{},clip{};Primitive commands[MaxDrawings]{};};
struct Queue {uint32_t pid=0,tid=0,count=0;Message messages[QueueDepth]{};};
struct Pending {uint64_t id=0;uint32_t sender=0,tid=0,receiver=0,receiverTid=0;bool done=false;int64_t result=0;uint32_t error=0;uint64_t window=0;};
struct Timer {uint32_t pid=0,tid=0,period=0;uint64_t window=0,id=0,due=0,callback=0;};
struct State {Surface surfaces[MaxWindows]{};Queue queues[MaxThreads]{};Pending pending[32]{};Timer timers[32]{};uint8_t keys[256]{},pressed[256]{};uint64_t next=0xD14700000100ull,cookie=1,focus=0,z=1;uint32_t modifiers=0,buttons=0;int32_t mouseX=0,mouseY=0;bool changed=true;};
static_assert(sizeof(State)<=StateBytes);
class Core {
public:
    void Initialize(processes::Manager&,threads::Scheduler&,State&) noexcept;
    uint32_t Execute(uint32_t pid,uint32_t tid,uint64_t now,Request&) noexcept;
    uint32_t Inject(const Input&,uint64_t now) noexcept;
    void Exit(uint32_t pid,uint32_t tid=0) noexcept;
    void Render(uint32_t*) const noexcept;
    Frame Metadata() const noexcept;
    bool Changed() const noexcept {return state_&&state_->changed;}
    bool Painting() const noexcept {if(state_)for(const auto& surface:state_->surfaces)if(surface.info.handle&&surface.painting)return true;return false;}
    void Presented() noexcept {state_->changed=false;}
    bool Clean() const noexcept;
private:
    Surface* Find(uint64_t) noexcept;
    Queue* Messages(uint32_t,uint32_t,bool=true) noexcept;
    uint32_t Push(uint32_t,uint32_t,Message) noexcept;
    uint32_t Destroy(uint32_t,uint64_t) noexcept;
    uint32_t SetFocus(uint32_t,uint64_t) noexcept;
    void Timers(uint64_t) noexcept;
    bool Position(Surface&,const Rect&,bool child) noexcept;
    processes::Manager* processes_=nullptr;threads::Scheduler* scheduler_=nullptr;State* state_=nullptr;
};
}
