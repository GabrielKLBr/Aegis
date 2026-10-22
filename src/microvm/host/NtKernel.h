// NtKernel.h - the synthetic kernel: services guest syscalls against the
// virtual world (VirtualFs now; registry/devices next) and records behavior.
//
// Use: register OnSyscall(kernel.DispatchFn()) and resolve PE imports via
// SyscallFor(); each supported Win32 function maps to a syscall number whose
// stub (mov eax,N; syscall; ret) traps into Dispatch().
#pragma once

#include "../vmm/MicroVmMonitor.h"
#include "VirtualFs.h"
#include "VirtualRegistry.h"
#include "ReportBuilder.h"

#include <cstdint>
#include <string>
#include <map>

namespace av {
namespace microvm {

// The fake "system personality" the synthetic kernel presents: the OS version
// it reports and the hardware it claims. Change it and the sample sees a
// different machine -- the heart of the anti-detection / profile feature.
struct SystemProfile {
    std::string name;          // "Windows 11"
    uint32_t    major, minor, build;
    std::string productName;   // HKLM ...\CurrentVersion\ProductName
    std::string gpu;           // fake display adapter

    static SystemProfile Win7();
    static SystemProfile Win10();
    static SystemProfile Win11();
};

class NtKernel {
public:
    explicit NtKernel(SystemProfile profile = SystemProfile::Win11());

    // Syscall number for an imported function, or -1 if unsupported.
    int SyscallFor(const std::string& fn) const;

    // The handler to pass to MicroVm::OnSyscall.
    void Dispatch(MicroVm& vm);

    int ExitCode() const { return exitCode_; }
    const ReportBuilder&   Report()  const { return report_; }
    const SystemProfile&   Profile() const { return profile_; }
    const std::string&     Console() const { return console_; }  // stdout/stderr writes
    VirtualFs&       Fs()       { return fs_; }
    VirtualRegistry& Registry() { return reg_; }

    // Guest-RAM heap arena the synthetic kernel bump-allocates from.
    uint64_t HeapBase() const { return heapBase_; }
    uint64_t HeapEnd()  const { return heapEnd_; }

private:
    std::string ReadCStr(MicroVm& vm, uint64_t gva) const;
    void SeedRegistry();

    VirtualFs fs_;
    VirtualRegistry reg_;
    SystemProfile profile_;
    ReportBuilder report_;
    std::string console_;
    std::map<std::string, int> table_;
    int exitCode_ = -1;

    // Bump-allocated heap arena in guest RAM (8 MiB .. 14 MiB).
    uint64_t heapBase_ = 0x800000;
    uint64_t heapEnd_  = 0xE00000;
    uint64_t heapNext_ = 0x800000;
    int      tlsNext_  = 0;        // next TlsAlloc index
};

} // namespace microvm
} // namespace av
