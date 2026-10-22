#include "../src/microvm/host/GuestScheduler.h"
#include <iostream>
#include <stdexcept>
using av::microvm::GuestScheduler;
namespace { void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }
int main() {
    try {
        using S = GuestScheduler;
        S queue(4);
        check(queue.Add(40) && queue.Start(40) && queue.Add(10) && queue.Add(30, true), "thread setup failed");
        check(!queue.Add(40) && !queue.Start(10), "duplicate/running CPU accepted");
        check(queue.Relinquish() && queue.Select(0) == 10 && queue.Relinquish() && queue.Select(0) == 40, "FIFO fairness depends on thread IDs");
        check(queue.Wait(100, 20) && queue.Select(0) == 10, "wait did not yield CPU");
        check(queue.Wait(100, 50) && queue.Select(0) == 0, "suspended thread was runnable");
        check(queue.Signal(100, false, 10) == 1 && queue.Select(10) == 40 && queue.Get(10)->state == S::State::Waiting,
            "auto-reset signal did not choose the oldest waiter");
        check(queue.Relinquish() && queue.Select(50) == 40 && queue.Get(10)->result == 0x102, "deadline did not expire pending wait");
        check(queue.Relinquish() && queue.Select(50) == 10 && queue.Get(10)->result == 0x102, "timeout return status was lost");
        uint32_t previous = 999;
        check(queue.Suspend(10, previous) == 0 && previous == 0 && queue.Select(50) == 40, "self-suspend did not deschedule");
        check(queue.Resume(10, previous) == 0 && previous == 1 && queue.Relinquish() && queue.Select(50) == 10 && queue.Get(10)->result == 0,
            "resume retained an earlier timeout result");
        check(queue.Wait(200, 100) && queue.Select(50) == 40 && queue.Wait(200, 100), "multiple wait setup failed");
        check(queue.Signal(200, true, 100) == 0 && queue.Get(40)->result == 0x102 && queue.Get(10)->result == 0x102,
            "signal revived already-expired waiters");
        check(queue.Select(100) == 10 && queue.Wait(300, S::Never) && queue.Select(100) == 40 && queue.Wait(300, S::Never), "manual wait setup failed");
        check(queue.Signal(300, true, 100) == 2 && queue.Select(100) == 10, "manual-reset signal did not wake all/FIFO");
        check(queue.Terminate(10, 77) && queue.Get(10)->exitCode == 77 && !queue.Terminate(10, 1), "termination state incorrect");
        check(queue.Resume(10, previous) == 0xC000004B && queue.Remove(10), "terminated metadata lifecycle failed");
        check(queue.Select(100) == 40 && queue.Wait(0, 150, true) && queue.Select(149) == 0 && queue.Select(150) == 40 && queue.Get(40)->result == 0,
            "delay returned before its deadline or returned timeout");
        check(queue.Terminate(40, 0) && queue.Terminate(30, 0) && queue.AllTerminated(), "process completion incorrect");
        S suspended;
        check(suspended.Add(1) && suspended.Start(1) && suspended.Wait(20, 50) && suspended.Suspend(1, previous) == 0, "suspended waiter setup failed");
        check(suspended.Signal(20, false, 1) == 1 && suspended.Select(1) == 0 && suspended.Resume(1, previous) == 0 && suspended.Select(1) == 1,
            "signal ignored suspension count or lost completion");
        check(suspended.Signal(0, true, 100) == 0, "null wait object was signaled");
        S bounded(1); check(bounded.Add(1) && !bounded.Add(2) && bounded.Remove(1) && bounded.Add(2), "capacity was not bounded/recovered");
        std::cout << "PASS: FIFO scheduling, suspended threads, event wakes, deadline races, delay, termination and capacity\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
