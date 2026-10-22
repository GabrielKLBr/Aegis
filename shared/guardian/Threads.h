#pragma once
#include "Abi.h"
namespace guardian::threads {
inline constexpr uint64_t Magic = 0x34524854415647ull, ReportGpa = 0x10A000, RequiredChecks = 0x7FFF;
inline constexpr uint32_t Version = 1, MaxThreads = 16, MaxObjects = 64, MaxWait = 8, MaxApcs = 4;
inline constexpr uint16_t Port = 0xEE;
inline constexpr uint64_t Never = UINT64_MAX;
inline constexpr uint32_t Success = 0, Abandoned = 0x80, UserApc = 0xC0, Alerted = 0x101, Timeout = 0x102,
    Pending = 0x103, Invalid = 0xC000000D, BadId = 0xC0000008, NoMemory = 0xC0000017,
    NotOwned = 0xC0000046, Limit = 0xC0000047, Cancelled = 0xC0000120, Busy = 0xC00000AA;
struct Report {
    uint64_t magic; uint32_t version, bytes;
    uint64_t bootId, checks, coreChecks, ticks, idleTicks, preemptions, contextSwitches, userSyscalls,
        completed, faults, apcs, freeBefore, freeAfter, counters[4], reserved[4];
};
static_assert(sizeof(Report) == 184);
inline bool ValidReport(const Report& r, uint64_t bootId) noexcept {
    if(r.magic!=Magic || r.version!=Version || r.bytes!=sizeof(r) || r.bootId!=bootId || (r.checks & ~RequiredChecks)) return false;
    for(auto n:r.reserved) if(n) return false;
    return true;
}
}
