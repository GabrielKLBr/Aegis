#pragma once
#include "guardian/Cpu.h"
// LLVM 18's MSVC intrinsic declaration truncates CR2 to unsigned long on
// Windows x64. Keep fault addresses 64-bit through an explicitly typed ABI.
#if defined(__clang__)
extern "C" uint64_t GuardianReadCr2() noexcept;
#define GUARDIAN_FAULT_ADDRESS() GuardianReadCr2()
#else
#define GUARDIAN_FAULT_ADDRESS() __readcr2()
#endif
struct TrapFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
static_assert(sizeof(TrapFrame) == 176 && offsetof(TrapFrame, rip) == 136 && offsetof(TrapFrame, cs) == 144);
extern "C" {
void CpuLoadTables(const void* gdt, const void* idt) noexcept;
uint16_t CpuReadTables(void* gdt, void* idt) noexcept;
void CpuEnterUser(uint64_t rip, uint64_t rsp) noexcept;
void CpuUserResume() noexcept;
void CpuSyscallEntry() noexcept;
void CpuProbeUd() noexcept;
void CpuProbePf() noexcept;
void CpuTriggerDoubleFault() noexcept;
extern uint8_t CpuUdSite, CpuUdResume, CpuPfSite, CpuPfResume;
extern uint8_t CpuUserStart, CpuUserEnd;
extern uint64_t CpuIsrTable[];
extern uint64_t CpuSavedKernelRsp, CpuSyscallStack, CpuSavedUserRsp;
void GuardianTrapDispatch(TrapFrame* frame, void* fx) noexcept;
}
