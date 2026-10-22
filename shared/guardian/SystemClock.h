#pragma once
#include "Cpu.h"
namespace guardian::time {
inline constexpr uint64_t Frequency = 10'000'000;
inline constexpr uint64_t UnixEpoch100ns = 116444736000000000ull;
struct Sample {
    uint64_t monotonicNs, counter, frequency, utc100ns;
};
inline bool SampleAt(const cpu::Clock& clock, uint64_t boot, uint64_t now, Sample& out) noexcept {
    if (clock.magic != cpu::ClockMagic || clock.version != cpu::Version ||
        clock.bytes != sizeof(clock) || clock.bootId != boot || (clock.sequence & 1) ||
        clock.performanceFrequency != Frequency || clock.utcEpoch100ns < UnixEpoch100ns ||
        now < clock.monotonicNs || now / 100 > UINT64_MAX - clock.utcEpoch100ns)
        return false;
    out = {now, now / 100, Frequency, clock.utcEpoch100ns + now / 100};
    return true;
}
inline bool MillisecondDeadline(uint64_t now, uint32_t milliseconds, uint64_t& out) noexcept {
    const auto duration = uint64_t(milliseconds) * 1'000'000;
    if (duration >= UINT64_MAX - now) return false;
    out = now + duration;
    return true;
}
inline bool ReadPublished(const volatile cpu::Clock& published, uint64_t boot, Sample& out) noexcept {
    for(uint32_t attempt=0;attempt<64;++attempt) {
        const auto sequence=published.sequence;
        if(sequence&1)continue;
        const cpu::Clock clock{published.magic,published.version,published.bytes,sequence,published.bootId,
            published.monotonicNs,published.queuedInterrupts,published.utcEpoch100ns,published.performanceFrequency};
        if(sequence==published.sequence)
            return SampleAt(clock,boot,clock.monotonicNs,out);
    }
    return false;
}
}
