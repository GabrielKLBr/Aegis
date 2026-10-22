#pragma once
#include "guardian/Threads.h"
namespace guardian::threads {
enum class State : uint8_t { Empty, Ready, Running, Waiting, Dead };
enum class Kind : uint8_t { Empty, ManualEvent, AutoEvent, Mutex, Semaphore, ManualTimer, AutoTimer, Critical, Srw, Completion };
struct Apc { uint64_t routine, argument; };
struct Thread {
    uint32_t id = 0, generation = 0, priority = 0, suspended = 0, result = 0, exitCode = Pending, completion = 0;
    State state = State::Empty;
    bool all = false, alertable = false, alert = false, delay = false, delivery = false;
    uint32_t wait[MaxWait]{}, count = 0, mode = 0;
    uint64_t deadline = Never, order = 0, readyTick = 0;
    Apc apcs[MaxApcs]{}, dispatched{};
    uint32_t apcCount = 0;
};
struct Object {
    uint32_t id = 0, generation = 0, owner = 0, depth = 0, count = 0, maximum = 0, readers = 0;
    Kind kind = Kind::Empty;
    bool signaled = false, abandoned = false;
    uint64_t due = Never, period = 0;
};
// Bounded, freestanding adaptation of GuestScheduler/GuestSynchronization.
// Calls are serialized with guest interrupts masked on the single vCPU.
class Scheduler {
public:
    uint32_t CreateThread(uint32_t priority = 3, bool suspended = false) noexcept;
    uint32_t CreateObject(Kind kind, uint32_t initial = 0, uint32_t maximum = 1) noexcept;
    uint32_t Close(uint32_t object) noexcept;
    bool Reap(uint32_t thread,bool preserveCompletion=false) noexcept;
    Thread* Get(uint32_t id) noexcept;
    const Thread* Get(uint32_t id) const noexcept;
    Object* GetObject(uint32_t id) noexcept;
    uint32_t Select() noexcept;
    uint32_t Current() const noexcept { return current_; }
    uint64_t Now() const noexcept { return now_; }
    bool Advance(uint64_t now) noexcept;
    uint32_t Wait(uint32_t thread, const uint32_t* objects, uint32_t count, bool all, uint64_t deadline, bool alertable = false, uint32_t mode = 0) noexcept;
    uint32_t Delay(uint32_t thread, uint64_t deadline, bool alertable = false) noexcept;
    uint32_t Signal(uint32_t object, uint32_t amount = 1) noexcept;
    uint32_t Reset(uint32_t object) noexcept;
    uint32_t Unlock(uint32_t thread, uint32_t object, bool shared = false) noexcept;
    uint32_t Terminate(uint32_t thread, uint32_t status) noexcept;
    uint32_t Suspend(uint32_t thread, uint32_t& previous) noexcept;
    uint32_t Resume(uint32_t thread, uint32_t& previous) noexcept;
    uint32_t Priority(uint32_t thread, uint32_t value) noexcept;
    uint32_t CancelWait(uint32_t thread) noexcept;
    uint32_t Alert(uint32_t thread) noexcept;
    uint32_t QueueApc(uint32_t thread, uint64_t routine, uint64_t argument) noexcept;
    uint32_t Arm(uint32_t object, uint64_t due, uint64_t period = 0) noexcept;
    uint32_t CancelTimer(uint32_t object) noexcept;
    bool AllDead() const noexcept;
    void Yield() noexcept;
private:
    void Ready(Thread& t, uint32_t status) noexcept;
    void Wake() noexcept;
    bool Available(const Object& o, const Thread& t, uint32_t mode) const noexcept;
    uint32_t Consume(Object& o, Thread& t, uint32_t mode) noexcept;
    bool Satisfy(Thread& t) noexcept;
    bool Interrupt(Thread& t) noexcept;
    Thread threads_[MaxThreads]{};
    Object objects_[MaxObjects]{};
    uint32_t current_ = 0;
    uint64_t now_ = 0, order_ = 0, ticks_ = 0;
};
uint64_t SchedulerChecks(void (*check)(bool, uint32_t) noexcept) noexcept;
}
