// Phase 0 only: freestanding compile/link boundary, NOT a bootable kernel.
#include "guardian/Validate.h"
#if defined(_WIN32) && !defined(_M_X64)
#error The initial guest target requires x64
#endif
#if defined(_WINDOWS_) || defined(_CPPUNWIND) || defined(_CPPRTTI)
#error Guest contract must not depend on Windows headers, C++ unwinding or RTTI
#endif
#if defined(_MSC_VER) && (!defined(_KERNEL_MODE) || !defined(_VC_NODEFAULTLIB))
#error Guest contract requires kernel-subset compilation and omitted CRT directives
#endif
extern "C" uint32_t GuardianCheckBoot(const guardian::abi::BootInfo* info, uint64_t selfGpa,
                                    uint64_t ramBytes, const guardian::abi::MemoryRegion* regions) noexcept {
    if (!info) return static_cast<uint32_t>(guardian::abi::Result::InvalidRange);
    return static_cast<uint32_t>(guardian::abi::ValidateLayout(*info, selfGpa, ramBytes, regions));
}
extern "C" uint32_t GuardianCheckMessage(const guardian::abi::MessageHeader* header, uint64_t bytes,
                                       uint64_t bootId, uint64_t sequence) noexcept {
    if (!header) return static_cast<uint32_t>(guardian::abi::Result::InvalidRange);
    return static_cast<uint32_t>(guardian::abi::ValidateMessage(*header, bytes, bootId, sequence));
}
