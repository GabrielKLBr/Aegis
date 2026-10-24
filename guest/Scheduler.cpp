#include "Scheduler.h"
namespace guardian::threads {
Thread* Scheduler::Get(uint32_t id) noexcept {
    const auto slot = (id & 255u); if(!slot || slot>MaxThreads) return nullptr;
    auto& t=threads_[slot-1];return t.id==id && t.state!=State::Empty ? &t:nullptr;
}
const Thread* Scheduler::Get(uint32_t id) const noexcept { return const_cast<Scheduler*>(this)->Get(id); }
Object* Scheduler::GetObject(uint32_t id) noexcept {
    const auto slot=id&255u; if(!slot || slot>MaxObjects) return nullptr;
    auto& o=objects_[slot-1];return o.id==id && o.kind!=Kind::Empty ? &o:nullptr;
}
uint32_t Scheduler::CreateObject(Kind kind,uint32_t initial,uint32_t maximum) noexcept {
    if(kind==Kind::Empty || kind>Kind::Completion || (kind==Kind::Semaphore && (!maximum || initial>maximum || maximum>INT32_MAX))) return 0;
    for(uint32_t i=0;i<MaxObjects;++i) if(objects_[i].kind==Kind::Empty) {
        const auto gen=objects_[i].generation+1;if(gen>=0xFFFFFF) continue;
        objects_[i]={};auto& o=objects_[i];o.generation=gen;o.id=(gen<<8)|(i+1);o.kind=kind;
        o.signaled=initial!=0;o.count=initial;o.maximum=maximum;return o.id;
    }
    return 0;
}
uint32_t Scheduler::CreateThread(uint32_t priority,bool suspended) noexcept {
    if(priority>7) return 0;
    for(uint32_t i=0;i<MaxThreads;++i) if(threads_[i].state==State::Empty) {
        const auto gen=threads_[i].generation+1;if(gen>=0xFFFFFF) continue;
        const auto completion=CreateObject(Kind::Completion);if(!completion) return 0;
        threads_[i]={};auto& t=threads_[i];t.id=(gen<<8)|(i+1);t.generation=gen;t.completion=completion;
        t.priority=priority;t.suspended=suspended?1u:0u;t.state=State::Ready;t.order=++order_;t.readyTick=ticks_;return t.id;
    }
    return 0;
}
uint32_t Scheduler::Close(uint32_t id) noexcept {
    auto* o=GetObject(id);if(!o) return BadId;
    if(o->owner || o->readers) return Busy;
    for(const auto& t:threads_) {
        if(t.state!=State::Empty && t.completion==id) return Busy;
        if(t.state==State::Waiting) for(uint32_t n=0;n<t.count;++n) if(t.wait[n]==id) return Busy;
    }
    o->kind=Kind::Empty;return Success;
}
bool Scheduler::Reap(uint32_t id,bool preserveCompletion) noexcept {
    auto* t=Get(id);if(!t || t->state!=State::Dead) return false;
    if(!preserveCompletion)for(const auto& other:threads_) if(other.state==State::Waiting) for(uint32_t i=0;i<other.count;++i) if(other.wait[i]==t->completion) return false;
    auto* o=GetObject(t->completion);if(!o) return false;if(!preserveCompletion)o->kind=Kind::Empty;t->state=State::Empty;return true;
}
void Scheduler::Ready(Thread& t,uint32_t status) noexcept {
    if(current_==t.id) current_=0;
    t.state=State::Ready;t.result=status;t.count=0;t.deadline=Never;t.delay=false;t.alertable=false;t.order=++order_;t.readyTick=ticks_;
}
bool Scheduler::Available(const Object& o,const Thread& t,uint32_t mode) const noexcept {
    switch(o.kind) {
    case Kind::Mutex: case Kind::Critical:return !o.owner || o.owner==t.id;
    case Kind::Srw:
        if(o.owner || (mode==0 && o.readers)) return false;
        if(mode==1) for(const auto& waiting:threads_) if(waiting.state==State::Waiting && waiting.count==1 &&
            waiting.wait[0]==o.id && waiting.mode==0 && waiting.id!=t.id) return false;
        return true;
    case Kind::Semaphore:return o.count!=0;
    default:return o.signaled;
    }
}
uint32_t Scheduler::Consume(Object& o,Thread& t,uint32_t mode) noexcept {
    switch(o.kind) {
    case Kind::Mutex: case Kind::Critical: {
        if(o.depth==127) return Limit;
        const auto status=o.abandoned?Abandoned:Success;o.owner=t.id;++o.depth;o.abandoned=false;return status;
    }
    case Kind::Srw:
        if(mode==1) o.readers|=1u<<((t.id&255u)-1);else o.owner=t.id;return Success;
    case Kind::Semaphore:--o.count;return Success;
    case Kind::AutoEvent:case Kind::AutoTimer:o.signaled=false;return Success;
    default:return Success;
    }
}
bool Scheduler::Satisfy(Thread& t) noexcept {
    if(!t.count) return false;
    if(t.all) {
        for(uint32_t i=0;i<t.count;++i) if(!Available(*GetObject(t.wait[i]),t,t.mode)) return false;
        // Validate recursion limits before consuming any part of wait-all.
        for(uint32_t i=0;i<t.count;++i) { const auto& o=*GetObject(t.wait[i]);if((o.kind==Kind::Mutex || o.kind==Kind::Critical)&&o.depth==127) {Ready(t,Limit);return true;} }
        uint32_t status=Success;
        for(uint32_t i=0;i<t.count;++i) if(Consume(*GetObject(t.wait[i]),t,t.mode)==Abandoned && !status) status=Abandoned+i;
        Ready(t,status);return true;
    }
    for(uint32_t i=0;i<t.count;++i) if(Available(*GetObject(t.wait[i]),t,t.mode)) {
        auto status=Consume(*GetObject(t.wait[i]),t,t.mode);if(status==Success || status==Abandoned) status+=i;Ready(t,status);return true;
    }
    return false;
}
bool Scheduler::Interrupt(Thread& t) noexcept {
    if(!t.alertable) return false;
    if(t.apcCount && !t.delivery) {
        t.dispatched=t.apcs[0];for(uint32_t i=1;i<t.apcCount;++i) t.apcs[i-1]=t.apcs[i];--t.apcCount;
        t.delivery=true;Ready(t,UserApc);return true;
    }
    if(t.alert) {t.alert=false;Ready(t,Alerted);return true;}return false;
}
void Scheduler::Wake() noexcept {
    for(uint32_t n=0;n<MaxThreads;++n) {
        Thread* chosen=nullptr;
        for(auto& t:threads_) if(t.state==State::Waiting && t.count && (!chosen || t.order<chosen->order)) {
            bool possible=t.all;
            for(uint32_t i=0;i<t.count;++i) {const bool a=Available(*GetObject(t.wait[i]),t,t.mode);possible=t.all?(possible&&a):(possible||a);}
            if(possible) chosen=&t;
        }
        if(!chosen || !Satisfy(*chosen)) break;
    }
}
uint32_t Scheduler::Wait(uint32_t id,const uint32_t* objects,uint32_t count,bool all,uint64_t deadline,bool alertable,uint32_t mode) noexcept {
    auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;
    if(t->state==State::Waiting || !objects || !count || count>MaxWait || mode>1) return Invalid;
    for(uint32_t i=0;i<count;++i) {
        auto* o=GetObject(objects[i]);if(!o) return BadId;
        if(o->kind==Kind::Srw && (all || count!=1 || (o->readers&(1u<<((id&255u)-1))) || o->owner==id)) return Invalid;
        for(uint32_t j=0;j<i;++j) if(objects[j]==objects[i]) return Invalid;
    }
    for(uint32_t i=0;i<count;++i) t->wait[i]=objects[i];
    t->count=count;t->all=all;t->deadline=deadline;t->alertable=alertable;t->mode=mode;
    if(Interrupt(*t) || Satisfy(*t)) return t->result;
    if(deadline!=Never && deadline<=now_) {Ready(*t,Timeout);return Timeout;}
    t->state=State::Waiting;t->order=++order_;if(current_==id) current_=0;return Pending;
}
uint32_t Scheduler::Delay(uint32_t id,uint64_t deadline,bool alertable) noexcept {
    auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;
    if(t->state==State::Waiting || deadline==Never) return Invalid;
    t->alertable=alertable;if(Interrupt(*t)) return t->result;
    if(deadline<=now_) {Ready(*t,Success);return Success;}
    t->count=0;t->delay=true;t->deadline=deadline;t->state=State::Waiting;t->order=++order_;if(current_==id) current_=0;return Pending;
}
void Scheduler::Yield() noexcept {if(auto* t=Get(current_)) {t->state=State::Ready;t->order=++order_;t->readyTick=ticks_;}current_=0;}
uint32_t Scheduler::Select() noexcept {
    if(current_) return current_;
    Thread* chosen=nullptr;uint64_t score=0;
    for(auto& t:threads_) if(t.state==State::Ready && !t.suspended) {
        auto effective=uint64_t(t.priority)+(ticks_-t.readyTick)/8;if(effective>7) effective=7;
        if(!chosen || effective>score || (effective==score && t.order<chosen->order)) {chosen=&t;score=effective;}
    }
    if(!chosen) return 0;chosen->state=State::Running;current_=chosen->id;return current_;
}
bool Scheduler::Advance(uint64_t now) noexcept {
    if(now<now_) return false;now_=now;++ticks_;
    // Expiration wins over a signal/timer delivered at the same instant.
    for(auto& t:threads_) if(t.state==State::Waiting && t.deadline!=Never && t.deadline<=now_) Ready(t,t.delay?Success:Timeout);
    for(auto& o:objects_) if((o.kind==Kind::ManualTimer || o.kind==Kind::AutoTimer) && o.due!=Never && o.due<=now_) {
        o.signaled=true;
        if(o.period) {const auto delta=o.period-((now_-o.due)%o.period);o.due=delta>=Never-now_?Never:now_+delta;}else o.due=Never;
    }
    Wake();return true;
}
uint32_t Scheduler::Signal(uint32_t id,uint32_t amount) noexcept {
    auto* o=GetObject(id);if(!o) return BadId;
    if(o->kind==Kind::Semaphore) {if(!amount)return Invalid;if(amount>o->maximum-o->count) return Limit;o->count+=amount;}
    else if(o->kind==Kind::ManualEvent || o->kind==Kind::AutoEvent) o->signaled=true;
    else return Invalid;
    Wake();return Success;
}
uint32_t Scheduler::Reset(uint32_t id) noexcept {
    auto* o=GetObject(id);if(!o) return BadId;
    if(o->kind!=Kind::AutoEvent && o->kind!=Kind::ManualEvent) return Invalid;o->signaled=false;return Success;
}
uint32_t Scheduler::Unlock(uint32_t id,uint32_t object,bool shared) noexcept {
    auto* t=Get(id);auto* o=GetObject(object);if(!t || !o) return BadId;
    if(o->kind==Kind::Srw) {
        const auto bit=1u<<((id&255u)-1);
        if(shared) {if(!(o->readers&bit)) return NotOwned;o->readers&=~bit;}
        else {if(o->owner!=id) return NotOwned;o->owner=0;}
    } else if(o->kind==Kind::Mutex || o->kind==Kind::Critical) {
        if(o->owner!=id) return NotOwned;if(!--o->depth) o->owner=0;
    } else return Invalid;
    Wake();return Success;
}
uint32_t Scheduler::Terminate(uint32_t id,uint32_t status) noexcept {
    auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;
    t->state=State::Dead;t->count=0;t->deadline=Never;t->apcCount=0;t->delivery=false;t->exitCode=status;
    if(current_==id) current_=0;
    for(auto& o:objects_) {
        if(o.owner==id) {o.owner=0;o.depth=0;if(o.kind==Kind::Mutex || o.kind==Kind::Critical) o.abandoned=true;}
        o.readers&=~(1u<<((id&255u)-1));
    }
    GetObject(t->completion)->signaled=true;Wake();return Success;
}
uint32_t Scheduler::Suspend(uint32_t id,uint32_t& previous) noexcept {
    auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;if(t->suspended==127) return Limit;
    previous=t->suspended++;if(current_==id) Yield();return Success;
}
uint32_t Scheduler::Resume(uint32_t id,uint32_t& previous) noexcept {
    auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;previous=t->suspended;if(t->suspended) --t->suspended;return Success;
}
uint32_t Scheduler::Priority(uint32_t id,uint32_t value) noexcept {auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;if(value>7) return Invalid;t->priority=value;return Success;}
uint32_t Scheduler::CancelWait(uint32_t id) noexcept {auto* t=Get(id);if(!t || t->state!=State::Waiting) return Invalid;Ready(*t,Cancelled);return Success;}
uint32_t Scheduler::Alert(uint32_t id) noexcept {auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;t->alert=true;if(t->state==State::Waiting) Interrupt(*t);return Success;}
uint32_t Scheduler::QueueApc(uint32_t id,uint64_t routine,uint64_t argument) noexcept {
    auto* t=Get(id);if(!t || t->state==State::Dead) return BadId;if(!routine) return Invalid;if(t->apcCount==MaxApcs) return NoMemory;
    t->apcs[t->apcCount++]={routine,argument};if(t->state==State::Waiting) Interrupt(*t);return Success;
}
#ifdef GUARDIAN_NATIVE_NT
uint32_t Scheduler::QueueNativeApc(uint32_t id,uint64_t routine,uint64_t a,uint64_t b,uint64_t c) noexcept {
    auto* t=Get(id);if(!t||t->state==State::Dead)return BadId;if(!routine)return Invalid;if(t->apcCount==MaxApcs)return NoMemory;
    t->apcs[t->apcCount++]={routine,a,b,c};if(t->state==State::Waiting)Interrupt(*t);return Success;
}
#endif
uint32_t Scheduler::Arm(uint32_t id,uint64_t due,uint64_t period) noexcept {
    auto* o=GetObject(id);if(!o) return BadId;
    if((o->kind!=Kind::ManualTimer && o->kind!=Kind::AutoTimer) || due==Never || period==Never) return Invalid;
    o->signaled=false;o->due=due;o->period=period;return Success;
}
uint32_t Scheduler::CancelTimer(uint32_t id) noexcept {auto* o=GetObject(id);if(!o) return BadId;if(o->kind!=Kind::ManualTimer && o->kind!=Kind::AutoTimer) return Invalid;o->due=Never;return Success;}
bool Scheduler::AllDead() const noexcept {for(const auto& t:threads_) if(t.state!=State::Empty && t.state!=State::Dead) return false;return true;}
}
