#pragma once
#include "Abi.h"
namespace guardian::cpu {
inline constexpr uint64_t BootMagic = 0x32555043415647ull, ClockMagic = 0x324B4C43415647ull, ReportMagic = 0x32545052415647ull;
inline constexpr uint32_t Version = 1;
inline constexpr uint64_t BootGpa = 0x105000, ClockGpa = 0x106000, ReportGpa = 0x107000;
inline constexpr uint64_t UserCode = 0x600000, UserStack = 0x610000, UserStackBytes = 0x10000, UserData = 0x620000;
inline constexpr uint16_t TimerPort = 0xEC;
inline constexpr uint32_t TimerVector = 0x40;
inline constexpr uint64_t PeriodNs = 5'000'000, RequiredChecks = 0x7FF;
enum Checks : uint64_t { Tables = 1, KernelFaults = 2, Syscalls = 4, UserIsolation = 8, SafeReturn = 16,
    MaskedTimer = 32, KernelTimer = 64, UserTimer = 128, Deadline = 256, Context = 512, UnknownSyscall = 1024 };
struct Boot {
    uint64_t magic; uint32_t version, bytes;
    uint64_t bootId, clockGpa, reportGpa, periodNs;
    uint32_t timerPort, timerVector;
    uint64_t reserved[5];
};
struct alignas(64) Clock {
    uint64_t magic; uint32_t version, bytes;
    // The formerly reserved tail publishes the UTC anchor and QPC frequency.
    // The structure stays 64 bytes; kernels without time APIs ignore the tail.
    uint64_t sequence, bootId, monotonicNs, queuedInterrupts, utcEpoch100ns, performanceFrequency;
};
struct Fault {
    uint64_t rip, rsp, rflags, cr2, error;
    uint32_t vector, cpl;
};
struct Report {
    uint64_t magic; uint32_t version, bytes;
    uint64_t bootId, checks, syscalls, echoes, unknownSyscalls, userEntries, userCompleted, userFaults, kernelFaults,
        returnRejects, timerTicks, timerUserTicks, clockStartNs, clockEndNs, deadlineNs, deadlineObservedNs, userProgress;
    uint32_t faultCount, fatal;
    Fault faults[12], fatalFault;
    uint64_t reserved[4];
};
// User-accessible scratch page: no kernel pointers or host identity.
struct UserState {
    uint64_t mode, cs, status, progress, deadline, x87Result;
};
static_assert(sizeof(Boot) == 96 && sizeof(Clock) == 64 && sizeof(Fault) == 48);
static_assert(sizeof(Report) == 816 && sizeof(UserState) == 48);
inline bool ValidBoot(const Boot& b, uint64_t bootId) noexcept {
    if (b.magic != BootMagic || b.version != Version || b.bytes != sizeof(Boot) || b.bootId != bootId ||
        b.clockGpa != ClockGpa || b.reportGpa != ReportGpa || b.periodNs != PeriodNs || b.timerPort != TimerPort || b.timerVector != TimerVector) return false;
    for (auto x : b.reserved) if (x) return false;
    return true;
}
inline bool ValidReport(const Report& r, uint64_t bootId) noexcept {
    if (r.magic != ReportMagic || r.version != Version || r.bytes != sizeof(Report) || r.bootId != bootId ||
        (r.checks & ~RequiredChecks) || r.faultCount > 12 || r.fatal > 1) return false;
    for (auto x : r.reserved) if (x) return false;
    if (r.fatal && r.fatalFault.cpl != 0 && r.fatalFault.cpl != 3) return false;
    for (uint32_t i = 0; i < r.faultCount; ++i) if (r.faults[i].cpl != 0 && r.faults[i].cpl != 3) return false;
    return true;
}
}
