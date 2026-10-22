#pragma once
#include "Abi.h"
namespace guardian::memory {
inline constexpr uint64_t Magic = 0x33504D454D5647ull;
inline constexpr uint32_t Version = 1;
inline constexpr uint64_t RequestGpa = 0x108000, ReportGpa = 0x109000;
inline constexpr uint16_t Port = 0xED;
inline constexpr uint64_t Arena = 0x40000000, ArenaBytes = 0x1000000;
inline constexpr uint32_t MaxRegions = 64, MaxSpaces = 4;
inline constexpr uint32_t NoAccess = 1, ReadOnly = 2, ReadWrite = 4, ExecuteRead = 0x20, Guard = 0x100;
inline constexpr uint32_t Free = 0x10000, Reserve = 0x2000, Commit = 0x1000;
enum class Status : uint32_t { Success = 0, Invalid = 0xC000000D, NoMemory = 0xC0000017,
    Conflict = 0xC0000018, NotCommitted = 0xC000002D, Protection = 0xC0000045, NotAllocated = 0xC00000A0 };
struct Request {
    uint64_t magic; uint32_t version, bytes;
    uint64_t bootId, sequence, gpa, length;
    uint32_t access, reserved;
};
struct Report {
    uint64_t magic; uint32_t version, bytes;
    uint64_t bootId, checks, freeBefore, freeAfter, peakFrames, allocationFailures,
        cycles, switches, guardFaults, userFaults, heapChecks, physicalRequests, exhaustedFrames;
    uint64_t faultAddress[8], faultError[8];
    uint64_t reserved[4];
};
inline constexpr uint64_t RequiredChecks = 0xFFF;
struct Information {
    uint64_t base, allocationBase; uint32_t allocationProtect, padding;
    uint64_t bytes; uint32_t state, protect, type, padding2;
};
static_assert(sizeof(Request) == 56 && sizeof(Report) == 280 && sizeof(Information) == 48);
inline bool ValidProtection(uint32_t p) noexcept {
    const auto base = p & ~Guard;
    return (base == NoAccess || base == ReadOnly || base == ReadWrite || base == ExecuteRead) &&
        (!(p & Guard) || base != NoAccess);
}
inline bool ValidRequest(const Request& r, uint64_t bootId, uint64_t next, uint64_t ram) noexcept {
    return r.magic == Magic && r.version == Version && r.bytes == sizeof(r) && r.bootId == bootId &&
        r.sequence == next && !r.reserved && r.length == abi::PageBytes && !(r.gpa & (abi::PageBytes - 1)) &&
        r.gpa < ram && r.length <= ram - r.gpa && (r.access == 1 || r.access == 3 || r.access == 5);
}
inline bool ValidReport(const Report& r, uint64_t bootId) noexcept {
    if (r.magic != Magic || r.version != Version || r.bytes != sizeof(r) || r.bootId != bootId || (r.checks & ~RequiredChecks)) return false;
    for (auto x : r.reserved) if (x) return false;
    return true;
}
}
