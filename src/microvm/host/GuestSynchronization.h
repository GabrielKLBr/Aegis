// Guest-only state. No host mutex or semaphore is exposed to the guest.
#pragma once
#include <cstdint>
#include <stdexcept>

namespace av { namespace microvm {
class GuestSemaphore {
public:
    static constexpr uint32_t InvalidParameter = 0xC000000D, LimitExceeded = 0xC0000047;
    static bool Valid(int32_t count, int32_t maximum) { return maximum > 0 && count >= 0 && count <= maximum; }
    GuestSemaphore(int32_t count, int32_t maximum) : count_(count), maximum_(maximum) {
        if (!Valid(count, maximum)) throw std::invalid_argument("Invalid guest semaphore counts");
    }
    int32_t Count() const { return count_; }
    int32_t Maximum() const { return maximum_; }
    bool Acquire() { if (!count_) return false; --count_; return true; }
    uint32_t Release(int32_t amount, int32_t& previous) {
        if (amount <= 0) return InvalidParameter;
        if (amount > maximum_ - count_) return LimitExceeded;
        previous = count_; count_ += amount; return 0;
    }
private:
    int32_t count_, maximum_;
};

class GuestMutant {
public:
    static constexpr uint32_t Abandoned = 0x80, Unavailable = 0x102,
        NotOwned = 0xC0000046, LimitExceeded = 0xC0000191;
    // A signed dispatcher count starts at one and must not fall below INT32_MIN.
    explicit GuestMutant(uint32_t limit = 0x80000001u) : limit_(limit) {
        if (!limit || limit > 0x80000001u) throw std::invalid_argument("Invalid mutant recursion limit");
    }
    uint64_t Owner() const { return owner_; }
    bool IsAbandoned() const { return abandoned_; }
    int32_t Count() const { return static_cast<int32_t>(int64_t(1) - depth_); }
    uint32_t Acquire(uint64_t thread) {
        if (!thread) return 0xC000000D;
        if (owner_ && owner_ != thread) return Unavailable;
        if (depth_ == limit_) return LimitExceeded;
        const auto status = abandoned_ ? Abandoned : 0;
        owner_ = thread; ++depth_; abandoned_ = false; return status;
    }
    uint32_t Release(uint64_t thread, int32_t& previous) {
        if (!thread || owner_ != thread) return NotOwned;
        previous = Count(); if (!--depth_) owner_ = 0; return 0;
    }
    bool Abandon(uint64_t thread) {
        if (!thread || owner_ != thread) return false;
        owner_ = 0; depth_ = 0; abandoned_ = true; return true;
    }
private:
    uint64_t owner_ = 0;
    uint32_t depth_ = 0, limit_;
    bool abandoned_ = false;
};
} }
