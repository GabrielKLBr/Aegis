// Cooperative single-CPU scheduling policy. Time is supplied by the caller in
// monotonic 100 ns ticks, so ordering and timeout tests do not sleep.
#pragma once
#include <cstdint>
#include <map>
#include <new>

namespace av { namespace microvm {
class GuestScheduler {
public:
    enum class State { Ready, Running, Waiting, Terminated };
    static constexpr uint64_t Never = UINT64_MAX;
    struct Thread {
        State state = State::Ready;
        uint32_t suspendCount = 0, result = 0, exitCode = 0x103;
        uint64_t object = 0, deadline = Never, order = 0;
        bool delay = false;
    };
    explicit GuestScheduler(size_t limit = 64) : limit_(limit) {}
    bool Add(uint64_t id, bool suspended = false) {
        if (!id || threads_.size() >= limit_ || threads_.count(id)) return false;
        try { threads_.emplace(id, Thread{}); } catch (const std::bad_alloc&) { return false; }
        auto& thread = threads_.at(id); thread.suspendCount = suspended ? 1 : 0; thread.order = ++order_; return true;
    }
    const Thread* Get(uint64_t id) const { auto it = threads_.find(id); return it == threads_.end() ? nullptr : &it->second; }
    uint64_t Current() const { return current_; }
    bool Remove(uint64_t id) {
        auto it = threads_.find(id);
        if (it == threads_.end() || current_ == id || it->second.state == State::Waiting) return false;
        threads_.erase(it); return true;
    }
    bool Start(uint64_t id) {
        auto it = threads_.find(id);
        if (current_ || it == threads_.end() || it->second.state != State::Ready || it->second.suspendCount) return false;
        current_ = id; it->second.state = State::Running; return true;
    }
    bool Relinquish() {
        if (!current_) return false;
        auto& thread = threads_.at(current_); thread.state = State::Ready; thread.order = ++order_; thread.result = 0;
        current_ = 0; return true;
    }
    bool Wait(uint64_t object, uint64_t deadline, bool delay = false) {
        if (!current_ || (!object && !delay)) return false;
        auto& thread = threads_.at(current_); thread.state = State::Waiting; thread.object = object;
        thread.deadline = deadline; thread.delay = delay; thread.order = ++order_; current_ = 0; return true;
    }
    void Expire(uint64_t now) {
        // Deadline order, then wait order, independent of numeric thread IDs.
        for (;;) {
            auto chosen = threads_.end();
            for (auto it = threads_.begin(); it != threads_.end(); ++it) {
                const auto& t = it->second;
                if (t.state != State::Waiting || t.deadline == Never || t.deadline > now) continue;
                if (chosen == threads_.end() || t.deadline < chosen->second.deadline ||
                    (t.deadline == chosen->second.deadline && t.order < chosen->second.order)) chosen = it;
            }
            if (chosen == threads_.end()) break;
            Ready(chosen->second, chosen->second.delay ? 0 : 0x102);
        }
    }
    size_t Signal(uint64_t object, bool all, uint64_t now) {
        if (!object) return 0;
        Expire(now); size_t count = 0;
        for (;;) {
            auto chosen = threads_.end();
            for (auto it = threads_.begin(); it != threads_.end(); ++it)
                if (it->second.state == State::Waiting && it->second.object == object &&
                    (chosen == threads_.end() || it->second.order < chosen->second.order)) chosen = it;
            if (chosen == threads_.end()) break;
            Ready(chosen->second, 0); ++count; if (!all) break;
        }
        return count;
    }
    // Resource handoff needs the selected thread ID before completing its wait.
    // Expired waits never consume a semaphore permit or mutant ownership.
    uint64_t NextWaiter(uint64_t object, uint64_t now) {
        Expire(now); if (!object) return 0;
        auto chosen = threads_.end();
        for (auto it = threads_.begin(); it != threads_.end(); ++it)
            if (it->second.state == State::Waiting && it->second.object == object &&
                (chosen == threads_.end() || it->second.order < chosen->second.order)) chosen = it;
        return chosen == threads_.end() ? 0 : chosen->first;
    }
    bool CompleteWait(uint64_t id, uint32_t result) {
        auto it = threads_.find(id);
        if (it == threads_.end() || it->second.state != State::Waiting) return false;
        Ready(it->second, result); return true;
    }
    uint64_t Select(uint64_t now) {
        Expire(now);
        if (current_) return current_;
        auto chosen = threads_.end();
        for (auto it = threads_.begin(); it != threads_.end(); ++it)
            if (it->second.state == State::Ready && !it->second.suspendCount &&
                (chosen == threads_.end() || it->second.order < chosen->second.order)) chosen = it;
        if (chosen == threads_.end()) return 0;
        current_ = chosen->first; chosen->second.state = State::Running; return current_;
    }
    uint64_t NextDeadline() const {
        uint64_t result = Never;
        for (const auto& pair : threads_) if (pair.second.state == State::Waiting && pair.second.deadline < result) result = pair.second.deadline;
        return result;
    }
    bool Terminate(uint64_t id, uint32_t code) {
        auto it = threads_.find(id); if (it == threads_.end() || it->second.state == State::Terminated) return false;
        auto& thread = it->second; thread.state = State::Terminated; thread.exitCode = code;
        thread.object = 0; thread.deadline = Never; if (current_ == id) current_ = 0; return true;
    }
    uint32_t Suspend(uint64_t id, uint32_t& previous) {
        auto it = threads_.find(id); if (it == threads_.end()) return 0xC0000008;
        auto& t = it->second; if (t.state == State::Terminated) return 0xC000004B;
        if (t.suspendCount >= 127) return 0xC000004A;
        previous = t.suspendCount++;
        if (current_ == id) { t.state = State::Ready; t.result = 0; t.order = ++order_; current_ = 0; }
        return 0;
    }
    uint32_t Resume(uint64_t id, uint32_t& previous) {
        auto it = threads_.find(id); if (it == threads_.end()) return 0xC0000008;
        auto& t = it->second; if (t.state == State::Terminated) return 0xC000004B;
        previous = t.suspendCount; if (t.suspendCount) --t.suspendCount; return 0;
    }
    bool AllTerminated() const {
        for (const auto& pair : threads_) if (pair.second.state != State::Terminated) return false;
        return true;
    }
    bool OtherReady() const {
        for (const auto& pair : threads_) if (pair.second.state == State::Ready && !pair.second.suspendCount) return true;
        return false;
    }
private:
    void Ready(Thread& thread, uint32_t result) {
        thread.state = State::Ready; thread.result = result; thread.object = 0; thread.deadline = Never; thread.order = ++order_;
    }
    std::map<uint64_t, Thread> threads_;
    size_t limit_;
    uint64_t current_ = 0, order_ = 0;
};
} }
