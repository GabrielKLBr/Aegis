#ifdef GUARDIAN_GRAPHICS_KERNEL
#include "GraphicsDriver.h"
#endif
#ifdef GUARDIAN_NETWORK_KERNEL
#include "NetworkDriver.h"
#endif
// Phase 2 executes descriptors, exception handlers and syscall dispatch in guest CPL0.
#include "CpuKernel.h"
#ifdef GUARDIAN_IO_KERNEL
#include "IoKernel.h"
#ifdef GUARDIAN_STORAGE_KERNEL
#include "StorageKernel.h"
#endif
#endif
#ifdef GUARDIAN_PROCESS_KERNEL
#include "ProcessKernel.h"
#endif
#ifdef GUARDIAN_THREAD_KERNEL
#include "ThreadKernel.h"
#endif
#ifdef GUARDIAN_MEMORY_KERNEL
#include "MemoryKernel.h"
#endif
#include "guardian/Validate.h"
#include "HardwareChecks.h"
#include <intrin.h>
#pragma intrinsic(_enable, _disable)
#if defined(_WINDOWS_) || defined(_CPPUNWIND) || defined(_CPPRTTI) || !defined(_M_X64)
#error Guest CPU kernel must not depend on the Windows runtime
#endif
using namespace guardian;
namespace {
#pragma pack(push, 1)
struct TablePointer { uint16_t limit; uint64_t base; };
struct Gate { uint16_t low, selector; uint8_t ist, attributes; uint16_t middle; uint32_t high, zero; };
struct Tss { uint32_t zero; uint64_t rsp[3], zero2, ist[7], zero3; uint16_t zero4, ioMap; };
#pragma pack(pop)
static_assert(sizeof(Tss) == 104 && sizeof(Gate) == 16 && sizeof(TablePointer) == 10);
alignas(16) uint64_t gdt[8];
alignas(4096) Gate idt[256];
alignas(16) Tss tss;
alignas(4096) uint64_t pml4[512], pdpt[512], pd[512], userPt[512];
alignas(4096) uint8_t irqStack[32768], syscallStack[32768], doubleFaultStack[16384], nmiStack[16384];
abi::BootInfo boot;
cpu::Boot cpuBoot;
cpu::Report report;
uint64_t messageSequence = 1, expectedRip = 0, resumeRip = 0, expectedVector = 0, userStatus = UINT64_MAX;
volatile bool userActive = false;
bool helloSent = false, timerArmed = false;
volatile uint64_t ticks = 0;
uint64_t lastClock = 0;
void Copy(volatile uint8_t* out, const void* in, size_t bytes) noexcept {
    auto* p = static_cast<const uint8_t*>(in); for (size_t i = 0; i < bytes; ++i) out[i] = p[i];
}
void Debug(const char* text) noexcept { for (unsigned i = 0; i < 256 && text[i]; ++i) __outbyte(abi::DebugPort, static_cast<uint8_t>(text[i])); }
__declspec(noreturn) void Halt() noexcept { for (;;) __halt(); }
void Send(abi::MessageKind kind, const void* data, uint32_t n) noexcept {
    const abi::MessageHeader h{abi::MessageMagic, abi::Major, abi::Minor, kind, 0, sizeof(h), n, 0, messageSequence++, boot.bootId};
    auto* out = reinterpret_cast<volatile uint8_t*>(boot.messageGpa);
    Copy(out, &h, sizeof(h)); Copy(out + sizeof(h), data, n); _ReadWriteBarrier(); __outbyte(abi::ControlPort, 1);
}
__declspec(noreturn) void Finish(uint32_t status, const char* diagnostic) noexcept {
    _disable();
    if (timerArmed) { __outbyte(cpu::TimerPort, 0); timerArmed = false; }
#ifdef GUARDIAN_MEMORY_KERNEL
    MemoryPublish();
#endif
#ifdef GUARDIAN_THREAD_KERNEL
    ThreadPublish();
#endif
#ifdef GUARDIAN_PROCESS_KERNEL
    ProcessPublish();
#ifdef GUARDIAN_IO_KERNEL
    IoPublish();
#ifdef GUARDIAN_STORAGE_KERNEL
    StoragePublish();
#endif
#endif
#endif
#ifdef GUARDIAN_NETWORK_KERNEL
    NetworkPublish();
#endif
#ifdef GUARDIAN_GRAPHICS_KERNEL
    GraphicsPublish();
#endif
    Copy(reinterpret_cast<volatile uint8_t*>(cpu::ReportGpa), &report, sizeof(report)); _ReadWriteBarrier();
    __outbyte(cpu::TimerPort, 3);
    if (helloSent) {
        uint32_t length = 0; while (diagnostic[length] && length < 255) ++length;
        Send(abi::MessageKind::Diagnostic, diagnostic, length);
        const abi::StopPayload stop{status, 0}; Send(abi::MessageKind::StopResult, &stop, sizeof(stop));
    }
    Halt();
}
cpu::Fault Fault(const TrapFrame& f) noexcept {
    return {f.rip, f.rsp, f.rflags, GUARDIAN_FAULT_ADDRESS(), f.error, static_cast<uint32_t>(f.vector), static_cast<uint32_t>(f.cs & 3)};
}
void Record(const TrapFrame& f) noexcept { if (report.faultCount < 12) report.faults[report.faultCount++] = Fault(f); }
__declspec(noreturn) void Fatal(const TrapFrame& f) noexcept {
    report.fatal = 1; report.fatalFault = Fault(f); Debug("guest CPU fatal fault\n");
    Finish(0xE000u | static_cast<uint32_t>(f.vector), "kernel CPU fatal fault");
}
void Check(bool ok, uint32_t code) noexcept { if (!ok) Finish(0xC000u | code, "kernel CPU self-test FAIL"); }
uint64_t ClockNs() noexcept {
    const auto* c = reinterpret_cast<volatile const cpu::Clock*>(cpu::ClockGpa);
    for (unsigned tries = 0; tries < 16; ++tries) {
        const auto before = c->sequence; _ReadWriteBarrier();
        const auto value = c->monotonicNs; _ReadWriteBarrier();
        if (before == c->sequence && !(before & 1)) {
            Check(c->magic == cpu::ClockMagic && c->version == cpu::Version && c->bytes == sizeof(cpu::Clock) && c->bootId == boot.bootId, 1);
            Check(value >= lastClock, 2); lastClock = value; return value;
        }
    }
    Finish(0xC003, "clock publication failed");
}
void ReturnKernel(TrapFrame& f) noexcept {
    f.rip = reinterpret_cast<uint64_t>(&CpuUserResume); f.cs = 8; f.rsp = CpuSavedKernelRsp; f.ss = 0x10; f.rflags = 2;
    userActive = false;
}
bool SafeUserReturn(const TrapFrame& f) noexcept {
    return (f.cs & 3) == 3 && f.rip >= cpu::UserCode && f.rip < cpu::UserCode + abi::PageBytes &&
        f.rsp >= cpu::UserStack + abi::PageBytes + 32 && f.rsp < cpu::UserStack + cpu::UserStackBytes;
}
void GateAt(unsigned vector, uint64_t entry, uint8_t ist = 0) noexcept {
    idt[vector] = {static_cast<uint16_t>(entry), 8, ist, 0x8E, static_cast<uint16_t>(entry >> 16), static_cast<uint32_t>(entry >> 32), 0};
}
void InitializeCpu() noexcept {
    int features[4]{}; __cpuid(features, static_cast<int>(0x80000001u));
    Check((static_cast<uint32_t>(features[3]) & ((1u << 20) | (1u << 11))) == ((1u << 20) | (1u << 11)), 4);
    gdt[1] = 0x00AF9A000000FFFFull; gdt[2] = 0x00CF92000000FFFFull;
    gdt[3] = 0x00CFFA000000FFFFull; gdt[4] = 0x00CFF2000000FFFFull; gdt[5] = 0x00AFFA000000FFFFull;
    tss.rsp[0] = reinterpret_cast<uint64_t>(irqStack + sizeof(irqStack));
    tss.ist[0] = reinterpret_cast<uint64_t>(doubleFaultStack + sizeof(doubleFaultStack));
    tss.ist[1] = reinterpret_cast<uint64_t>(nmiStack + sizeof(nmiStack)); tss.ioMap = sizeof(Tss);
    const auto address = reinterpret_cast<uint64_t>(&tss);
    gdt[6] = (sizeof(Tss) - 1) | ((address & 0xFFFFFF) << 16) | (0x89ull << 40) | (((address >> 24) & 255) << 56);
    gdt[7] = address >> 32;
    for (unsigned i = 0; i < 256; ++i) GateAt(i, CpuIsrTable[33]);
    for (unsigned i = 0; i < 32; ++i) GateAt(i, CpuIsrTable[i], i == 8 ? 1 : (i == 2 ? 2 : 0));
    GateAt(cpu::TimerVector, CpuIsrTable[32]);
    const TablePointer gdtr{sizeof(gdt) - 1, reinterpret_cast<uint64_t>(gdt)}, idtr{sizeof(idt) - 1, reinterpret_cast<uint64_t>(idt)};
    CpuLoadTables(&gdtr, &idtr);
    TablePointer readGdt{}, readIdt{};
    Check(CpuReadTables(&readGdt, &readIdt) == 0x30 && readGdt.base == gdtr.base && readGdt.limit == gdtr.limit &&
        readIdt.base == idtr.base && readIdt.limit == idtr.limit, 5);
#if defined(GUARDIAN_PROCESS_KERNEL)
    const auto* payload = &ProcessUserStart;
    const auto payloadBytes = reinterpret_cast<uint64_t>(&ProcessUserEnd) - reinterpret_cast<uint64_t>(payload);
#elif defined(GUARDIAN_THREAD_KERNEL)
    const auto* payload = &ThreadUserStart;
    const auto payloadBytes = reinterpret_cast<uint64_t>(&ThreadUserEnd) - reinterpret_cast<uint64_t>(payload);
#elif defined(GUARDIAN_MEMORY_KERNEL)
    const auto* payload = &MemoryUserStart;
    const auto payloadBytes = reinterpret_cast<uint64_t>(&MemoryUserEnd) - reinterpret_cast<uint64_t>(payload);
#else
    const auto* payload = &CpuUserStart;
    const auto payloadBytes = reinterpret_cast<uint64_t>(&CpuUserEnd) - reinterpret_cast<uint64_t>(payload);
#endif
    Check(payloadBytes && payloadBytes <= abi::PageBytes, 6);
    Copy(reinterpret_cast<volatile uint8_t*>(cpu::UserCode), payload, static_cast<size_t>(payloadBytes));
    __outbyte(cpu::TimerPort, 4); // Seal the copied user code RX at the WHP boundary.
    __writemsr(0xC0000080, __readmsr(0xC0000080) | 0x801); // EFER.SCE | NXE
#if defined(GUARDIAN_PROCESS_KERNEL)
    ProcessInitialize(boot, Check);
#elif defined(GUARDIAN_THREAD_KERNEL)
    ThreadInitialize(boot, Check);
#elif defined(GUARDIAN_MEMORY_KERNEL)
    MemoryInitialize(boot, Check);
#else
    pml4[0] = reinterpret_cast<uint64_t>(pdpt) | 7; pdpt[0] = reinterpret_cast<uint64_t>(pd) | 7;
    for (uint64_t i = 0; i < 512; ++i) { pd[i] = (i << 21) | 0x83; userPt[i] = (cpu::UserCode + (i << 12)) | 3 | (1ull << 63); }
    pd[cpu::UserCode >> 21] = reinterpret_cast<uint64_t>(userPt) | 7;
    userPt[0] = cpu::UserCode | 5; // User RX, supervisor pages stay inaccessible to CPL3.
    userPt[(cpu::UserStack - cpu::UserCode) >> 12] = 0;
    for (uint64_t at = cpu::UserStack + abi::PageBytes; at < cpu::UserStack + cpu::UserStackBytes; at += abi::PageBytes)
        userPt[(at - cpu::UserCode) >> 12] = at | 7 | (1ull << 63);
    userPt[(cpu::UserData - cpu::UserCode) >> 12] = cpu::UserData | 7 | (1ull << 63);
    __writecr3(reinterpret_cast<uint64_t>(pml4));
    Check(__readcr3() == reinterpret_cast<uint64_t>(pml4), 7);
#endif
    __writecr0(__readcr0() | 0x10000); // CR0.WP
    __writemsr(0xC0000081, (0x1Bull << 48) | (8ull << 32));
    __writemsr(0xC0000082, reinterpret_cast<uint64_t>(&CpuSyscallEntry));
    __writemsr(0xC0000084, 0x44700); // Mask TF, IF, DF, NT and AC on entry.
    __writemsr(0xC0000101, reinterpret_cast<uint64_t>(&report)); __writemsr(0xC0000102, 0);
    CpuSyscallStack = reinterpret_cast<uint64_t>(syscallStack + sizeof(syscallStack));
    report.checks |= cpu::Tables;
}
#if !defined(GUARDIAN_MEMORY_KERNEL) && !defined(GUARDIAN_THREAD_KERNEL) && !defined(GUARDIAN_PROCESS_KERNEL)
void RunUser(uint64_t mode) noexcept {
    auto* data = reinterpret_cast<volatile cpu::UserState*>(cpu::UserData);
    data->mode = mode; data->cs = 0; data->status = UINT64_MAX; data->progress = 0;
    data->deadline = ClockNs() + 3 * cpu::PeriodNs; data->x87Result = 0;
    userStatus = UINT64_MAX; userActive = true; ++report.userEntries;
    CpuEnterUser(cpu::UserCode, cpu::UserStack + cpu::UserStackBytes - 0x28);
    Check(!userActive && (data->cs & 3) == 3, 8);
    if (mode == 0 || mode == 6) {
        Check(userStatus == 0 && data->status == 0 && data->x87Result == 0x3FF0000000000000ull, 9);
        ++report.userCompleted;
    }
    if (mode == 6) {
        report.deadlineNs = data->deadline; report.deadlineObservedNs = ClockNs(); report.userProgress = data->progress;
        Check(report.deadlineObservedNs >= report.deadlineNs && report.userProgress > 0 && report.timerUserTicks >= 2, 10);
    }
}
void SelfTest() noexcept {
    expectedRip = reinterpret_cast<uint64_t>(&CpuUdSite); resumeRip = reinterpret_cast<uint64_t>(&CpuUdResume); expectedVector = 6;
    CpuProbeUd(); Check(report.kernelFaults == 1, 11);
    expectedRip = reinterpret_cast<uint64_t>(&CpuPfSite); resumeRip = reinterpret_cast<uint64_t>(&CpuPfResume); expectedVector = 14;
    CpuProbePf(); Check(report.kernelFaults == 2 && report.faults[1].cr2 == 0x100000000ull && report.faults[1].error == 0, 12);
    expectedRip = 0; report.checks |= cpu::KernelFaults;
    RunUser(0); Check(report.echoes == 1 && report.unknownSyscalls == 1, 13);
    report.checks |= cpu::Syscalls | cpu::Context | cpu::UnknownSyscall;
    for (uint64_t mode = 1; mode <= 4; ++mode) {
        RunUser(mode); const auto& f = report.faults[static_cast<size_t>(mode + 1)];
        const uint32_t vector = mode == 1 ? 6u : (mode == 2 ? 13u : 14u);
        Check(report.userFaults == mode && f.vector == vector && f.cpl == 3, 14);
        if (mode == 3) Check(f.cr2 == 0x100000 && f.error == 5, 15);
        if (mode == 4) Check(f.cr2 == cpu::UserStack + abi::PageBytes - 8 && f.error == 6, 16);
    }
    RunUser(7); RunUser(8);
    Check(report.userFaults == 6 && report.faults[6].vector == 13 && report.faults[6].error == 0 &&
        report.faults[7].vector == 13 && report.faults[7].error == 0x202, 20);
    report.checks |= cpu::UserIsolation;
    RunUser(5); Check(report.returnRejects == 1 && report.faults[8].vector == 0x100 && report.faults[8].cpl == 3, 17);
    report.checks |= cpu::SafeReturn;
    RunUser(0); // A new user entry must still work after all the contained faults.
    timerArmed = true; __outbyte(cpu::TimerPort, 1); report.clockStartNs = ClockNs();
    const auto maskedDeadline = report.clockStartNs + 2 * cpu::PeriodNs;
    while (ClockNs() < maskedDeadline) _mm_pause();
    Check(ticks == 0, 18); report.checks |= cpu::MaskedTimer;
    _enable(); while (ticks < 2) _mm_pause(); _disable();
    report.checks |= cpu::KernelTimer;
    RunUser(6); report.checks |= cpu::UserTimer | cpu::Deadline;
    report.clockEndNs = ClockNs(); Check(report.clockEndNs > report.clockStartNs && report.checks == cpu::RequiredChecks, 19);
}
#endif
}
#ifdef GUARDIAN_MEMORY_KERNEL
extern "C" uint64_t MemoryRunUser(uint64_t mode, uint64_t address, uint64_t value) noexcept {
    auto* data = reinterpret_cast<volatile cpu::UserState*>(cpu::UserData);
    data->mode = mode; data->cs = 0; data->status = UINT64_MAX; data->progress = address; data->deadline = value; data->x87Result = UINT64_MAX;
    userStatus = UINT64_MAX; userActive = true; ++report.userEntries;
    CpuEnterUser(cpu::UserCode, cpu::UserStack + cpu::UserStackBytes - 0x28);
    Check(!userActive && (data->cs & 3) == 3, 8);
    if (!userStatus) ++report.userCompleted;
    return userStatus;
}
#endif
extern "C" void GuardianTrapDispatch(TrapFrame* frame, void* fx) noexcept {
#if !defined(GUARDIAN_THREAD_KERNEL) && !defined(GUARDIAN_PROCESS_KERNEL)
    (void)fx;
#endif
    auto& f = *frame;
    if (f.vector == cpu::TimerVector) {
        ++report.timerTicks; if ((f.cs & 3) == 3) ++report.timerUserTicks;
        const auto now = ClockNs(); ticks = ticks + 1; __outbyte(cpu::TimerPort, 2);
#ifdef GUARDIAN_THREAD_KERNEL
        if (ThreadActive() && (f.cs & 3) == 3 && ThreadTrap(f, fx, now, true)) ReturnKernel(f);
#elif defined(GUARDIAN_PROCESS_KERNEL)
        if (ProcessActive() && (f.cs & 3) == 3 && ProcessTrap(f, fx, now, true)) ReturnKernel(f);
#else
        (void)now;
#endif
        return;
    }
#ifdef GUARDIAN_PROCESS_KERNEL
    if (ProcessActive() && (f.cs & 3) == 3) {
        if (f.vector == 0x80) { ++report.syscalls; f.rflags = (f.rflags & 0xCD5) | 0x202; }
        else { Record(f); ++report.userFaults; }
        if (ProcessTrap(f, fx, ClockNs(), false)) ReturnKernel(f);
        return;
    }
#endif
#ifdef GUARDIAN_THREAD_KERNEL
    if (ThreadActive() && (f.cs & 3) == 3) {
        if (f.vector == 0x80) { ++report.syscalls; f.rflags = (f.rflags & 0xCD5) | 0x202; }
        else { Record(f); ++report.userFaults; }
        if (ThreadTrap(f, fx, ClockNs(), false)) ReturnKernel(f);
        return;
    }
#endif
    if (f.vector == 0x80) {
        ++report.syscalls;
        if (!userActive || !SafeUserReturn(f)) {
            ++report.returnRejects; f.vector = 0x100; Record(f); userStatus = 0x100; ReturnKernel(f); return;
        }
        f.rflags = (f.rflags & 0xCD5) | 0x202; // User arithmetic/DF only; IOPL/TF/AC/NT/RF cannot cross the boundary.
        switch (f.rax) {
#ifdef GUARDIAN_GRAPHICS_KERNEL
        case 0xA0:f.rax=GraphicsSyscall(f.rax,f.rdx);return;
#endif
#ifdef GUARDIAN_NETWORK_KERNEL
        case 0x90:case 0x91:{uint64_t bytes=0;f.rax=NetworkSyscall(f.rax,f.rdx,f.r8,bytes);f.rdx=bytes;return;}
#endif
        case 1: ++report.echoes; f.rax = f.rdx ^ 0x1122334455667788ull; return;
        case 2: f.rax = ClockNs(); f.rdx = report.timerUserTicks; return;
        case 3: userStatus = f.rdx; ReturnKernel(f); return;
        default: ++report.unknownSyscalls; f.rax = 0xC0000002; return;
        }
    }
    if ((f.cs & 3) == 3 && userActive) {
#ifdef GUARDIAN_MEMORY_KERNEL
        MemoryRecordFault(f);
#endif
        Record(f); ++report.userFaults; userStatus = f.vector; ReturnKernel(f); return;
    }
    if (!(f.cs & 3) && expectedRip && f.rip == expectedRip && f.vector == expectedVector) {
        Record(f); ++report.kernelFaults; f.rip = resumeRip; return;
    }
    Fatal(f);
}
#ifdef GUARDIAN_NETWORK_KERNEL
extern "C" uint64_t NetworkUserInvoke(uint64_t operation,uint64_t address,uint64_t bytes) noexcept {
    auto* data=reinterpret_cast<volatile cpu::UserState*>(cpu::UserData);
    data->mode=9;data->cs=0;data->status=UINT64_MAX;data->progress=operation;data->deadline=address;data->x87Result=bytes;
    userStatus=UINT64_MAX;userActive=true;++report.userEntries;
    CpuEnterUser(cpu::UserCode,cpu::UserStack+cpu::UserStackBytes-0x28);_disable();
    Check(!userActive&&userStatus==0&&(data->cs&3)==3&&data->status<=7,0x1816);return data->status;
}
extern "C" uint32_t NetworkUserExchange(uint32_t operation,guardian::net::Frame& frame) noexcept {
    constexpr auto buffer=cpu::UserData+0x100;
    Check(operation==1||operation==2,0x1817);
    if(operation==1){Check(frame.bytes<=guardian::net::Mtu,0x1818);Copy(reinterpret_cast<volatile uint8_t*>(buffer),frame.data,frame.bytes);}
    const auto status=NetworkUserInvoke(operation==1?0x90:0x91,buffer,operation==1?frame.bytes:guardian::net::Mtu);
    if(operation==2&&status==0){frame={};const auto* data=reinterpret_cast<const volatile cpu::UserState*>(cpu::UserData);Check(data->progress<=guardian::net::Mtu,0x1819);frame.bytes=uint32_t(data->progress);Copy(frame.data,reinterpret_cast<const void*>(buffer),frame.bytes);}
    return uint32_t(status);
}
#endif
#ifdef GUARDIAN_GRAPHICS_KERNEL
extern "C" uint64_t GraphicsRunUser() noexcept {
    auto* data=reinterpret_cast<volatile cpu::UserState*>(cpu::UserData);data->mode=10;data->cs=0;
    userStatus=UINT64_MAX;userActive=true;++report.userEntries;
    CpuEnterUser(cpu::UserCode,cpu::UserStack+cpu::UserStackBytes-0x28);_disable();
    Check(!userActive&&(data->cs&3)==3,0x1905);return userStatus;
}
#endif
extern "C" __declspec(noreturn) void GuardianCpuEntry(const abi::BootInfo* parameter, const cpu::Boot* cpuParameter) noexcept {
#if defined(GUARDIAN_PROCESS_KERNEL)
#ifdef GUARDIAN_IO_KERNEL
#ifdef GUARDIAN_STORAGE_KERNEL
#ifdef GUARDIAN_PE_KERNEL
    Debug("Guardian kernel 0.8.0 PE MAPPING\n");
#else
    Debug("Guardian kernel 0.7.0 STORAGE\n");
#endif
#else
    Debug("Guardian kernel 0.6.0 IO\n");
#endif
#else
    Debug("Guardian kernel 0.5.0 PROCESSES\n");
#endif
#elif defined(GUARDIAN_NETWORK_KERNEL)
    Debug("Guardian kernel 0.10.0 NETWORK\n");
#elif defined(GUARDIAN_THREAD_KERNEL)
    Debug("Guardian kernel 0.4.0 THREADS\n");
#elif defined(GUARDIAN_MEMORY_KERNEL)
    Debug("Guardian kernel 0.3.0 MEMORY\n");
#else
    Debug("Guardian kernel 0.2.0 CPU\n");
#endif
    if (reinterpret_cast<uint64_t>(parameter) != 0x100000 || reinterpret_cast<uint64_t>(cpuParameter) != cpu::BootGpa) { Debug("invalid CPU boot pointer\n"); Halt(); }
    boot = *parameter;
    if (abi::ValidateBoot(boot, 0x100000, boot.ramBytes) != abi::Result::Ok) { Debug("invalid boot header\n"); Halt(); }
    if(!hardware::CheckBootInventory(boot.ramBytes)){Debug("invalid hardware inventory\n");Halt();}
    const auto* regions = reinterpret_cast<const abi::MemoryRegion*>(boot.memoryMapGpa);
    if (abi::ValidateLayout(boot, 0x100000, boot.ramBytes, regions) != abi::Result::Ok ||
        abi::ValidateDevices(boot, reinterpret_cast<const abi::Device*>(boot.devicesGpa)) != abi::Result::Ok ||
        !abi::Covered(regions, boot.memoryMapCount, cpu::BootGpa, 3 * abi::PageBytes, abi::MemoryKind::BootData) ||
        !abi::Covered(regions, boot.memoryMapCount, cpu::UserCode, 0x200000, abi::MemoryKind::Reserved)) { Debug("invalid CPU boot layout\n"); Halt(); }
    cpuBoot = *cpuParameter;
    if (!cpu::ValidBoot(cpuBoot, boot.bootId)) { Debug("invalid CPU boot extension\n"); Halt(); }
    report.magic = cpu::ReportMagic; report.version = cpu::Version; report.bytes = sizeof(report); report.bootId = boot.bootId;
    const abi::HelloPayload hello{abi::X64, 1, 0}; Send(abi::MessageKind::Hello, &hello, sizeof(hello)); helloSent = true;
    (void)ClockNs(); InitializeCpu();
#if defined(GUARDIAN_CPU_TEST_FATAL)
    if constexpr (GUARDIAN_CPU_TEST_FATAL == 2) CpuTriggerDoubleFault();
    else __ud2();
#endif
#if defined(GUARDIAN_PROCESS_KERNEL)
    ProcessSelfTest();
#ifdef GUARDIAN_IO_KERNEL
#ifdef GUARDIAN_STORAGE_KERNEL
    Finish(0, "kernel storage self-test PASS");
#else
    Finish(0, "kernel IO self-test PASS");
#endif
#else
    Finish(0, "kernel processes self-test PASS");
#endif
#elif defined(GUARDIAN_THREAD_KERNEL)
    ThreadSelfTest(); Finish(0, "kernel threads self-test PASS");
#elif defined(GUARDIAN_MEMORY_KERNEL)
    MemorySelfTest(); Finish(0, "kernel memory self-test PASS");
#else
    SelfTest();
#ifdef GUARDIAN_GRAPHICS_KERNEL
    GraphicsSelfTest(Check); Finish(0, "kernel graphics self-test PASS");
#elif defined(GUARDIAN_NETWORK_KERNEL)
    NetworkSelfTest(Check); Finish(0, "kernel network self-test PASS");
#else
    Finish(0, "kernel CPU self-test PASS");
#endif
#endif
}
#ifdef GUARDIAN_PROCESS_KERNEL
extern "C" void ProcessRun(uint64_t rip,uint64_t rsp) noexcept {
    userActive=true;++report.userEntries;timerArmed=true;__outbyte(cpu::TimerPort,1);
    CpuEnterUser(rip,rsp);_disable();__outbyte(cpu::TimerPort,0);timerArmed=false;Check(!userActive,0x22);
}
#endif
#ifdef GUARDIAN_THREAD_KERNEL
extern "C" void ThreadRun(uint64_t rip,uint64_t rsp) noexcept {
    userActive = true; ++report.userEntries; timerArmed = true; __outbyte(cpu::TimerPort, 1);
    CpuEnterUser(rip,rsp); _disable();
    __outbyte(cpu::TimerPort, 0); timerArmed = false; Check(!userActive, 0x21);
}
#endif
