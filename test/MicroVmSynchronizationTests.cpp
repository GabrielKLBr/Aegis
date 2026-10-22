#include "../src/microvm/host/GuestSynchronization.h"
#include "../src/microvm/host/GuestScheduler.h"
#include "../src/microvm/host/GuestObjects.h"
#include <iostream>
#include <stdexcept>
using namespace av::microvm;
namespace { void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }
int main() {
    try {
        check(!GuestSemaphore::Valid(-1, 2) && !GuestSemaphore::Valid(3, 2) && !GuestSemaphore::Valid(0, 0), "invalid semaphore accepted");
        GuestSemaphore semaphore(1, INT32_MAX); int32_t previous = 123;
        check(semaphore.Acquire() && !semaphore.Acquire() && semaphore.Count() == 0, "semaphore permit accounting failed");
        check(semaphore.Release(INT32_MAX, previous) == 0 && previous == 0, "maximum release failed");
        previous = 123;
        check(semaphore.Release(1, previous) == GuestSemaphore::LimitExceeded && previous == 123 && semaphore.Count() == INT32_MAX, "semaphore overflow changed state");
        check(semaphore.Release(0, previous) == GuestSemaphore::InvalidParameter && semaphore.Release(-1, previous) == GuestSemaphore::InvalidParameter, "nonpositive release accepted");
        GuestMutant mutant(2);
        check(mutant.Count() == 1 && mutant.Acquire(40) == 0 && mutant.Acquire(40) == 0 && mutant.Count() == -1, "recursive ownership failed");
        check(mutant.Acquire(40) == GuestMutant::LimitExceeded && mutant.Acquire(10) == GuestMutant::Unavailable, "recursion/owner bound failed");
        check(mutant.Release(10, previous) == GuestMutant::NotOwned && mutant.Owner() == 40, "non-owner released mutant");
        check(mutant.Release(40, previous) == 0 && previous == -1 && mutant.Owner() == 40, "partial release relinquished ownership");
        check(!mutant.Abandon(10) && mutant.Abandon(40) && mutant.IsAbandoned() && mutant.Count() == 1, "abandon did not reset recursion");
        check(mutant.Acquire(10) == GuestMutant::Abandoned && !mutant.IsAbandoned() && mutant.Acquire(10) == 0, "abandon status not consumed exactly once");
        check(mutant.Release(10, previous) == 0 && mutant.Release(10, previous) == 0 && previous == 0 && mutant.Count() == 1, "final release failed");
        GuestScheduler queue;
        check(queue.Add(40) && queue.Start(40) && queue.Add(10) && queue.Wait(100, 5) && queue.Select(0) == 10 && queue.Wait(100, 50), "waiter setup failed");
        check(queue.NextWaiter(100, 5) == 10 && queue.Get(40)->result == 0x102, "expired waiter consumed resource");
        uint32_t prior = 0;
        check(queue.Suspend(10, prior) == 0 && queue.CompleteWait(10, GuestMutant::Abandoned) && !queue.CompleteWait(10, 0), "completion did not preserve waiting state");
        check(queue.Select(5) == 40 && queue.Terminate(40, 0) && queue.Select(5) == 0, "suspended completion was runnable");
        check(queue.Resume(10, prior) == 0 && queue.Select(5) == 10 && queue.Get(10)->result == GuestMutant::Abandoned, "suspension lost abandoned status");
        uint32_t access = 0;
        check(GuestObjects::Normalize(ObjectKind::Semaphore, 0x10000000, access) == 0 && access == 0x1F0003, "semaphore rights incorrect");
        check(GuestObjects::Normalize(ObjectKind::Mutant, 0x10000000, access) == 0 && access == 0x1F0001, "mutant rights incorrect");
        std::cout << "PASS: semaphore bounds/overflow, recursive ownership/abandonment, timed resource handoff, suspension and access masks\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
