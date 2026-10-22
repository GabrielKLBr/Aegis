// Preflight.h - virtualization readiness probe for the MicroVM sandbox.
//
// This is the entry gate of the MicroVM engine (see
// docs/ARCHITECTURE-microvm-sandbox.md). Before any partition can be created
// with the Windows Hypervisor Platform (WHP), the machine must actually be
// running the Windows hypervisor. This module answers, without requiring admin:
//
//   * Is WHP installed and usable on this machine?      (authoritative)
//   * If not, *why* not, and *how* to fix it?           (diagnosis)
//
// The authoritative readiness signal is WHvGetCapability(HypervisorPresent),
// which needs no elevation. Everything else here exists to explain a "no" and
// produce an actionable remediation plan (BIOS VT-x, WHP feature, hypervisor
// launch type, conflicting third-party hypervisors such as VMware).
#pragma once

#include "../../Common.h"
#include <cstdint>

namespace av {
namespace microvm {

enum class Verdict {
    Ready,        // WHP present and the hypervisor is running -> can create a partition
    NeedsEnable,  // capable hardware, but a feature/flag must be turned on
    Unsupported,  // no virtualization extensions exposed at all (BIOS off / old CPU)
};

struct PreflightResult {
    // ---- CPU (best-effort via CPUID) ----
    std::wstring cpuVendor;             // "GenuineIntel" / "AuthenticAMD" / ...
    bool cpuVmxOrSvm = false;           // VT-x (VMX) or AMD-V (SVM) bit visible
    bool hypervisorPresentCpuid = false;// CPUID leaf 1, ECX bit 31

    // ---- Windows Hypervisor Platform ----
    bool whpDllPresent = false;         // WinHvPlatform.dll could be loaded
    bool whpApiPresent = false;         // WHvGetCapability could be resolved
    bool whpHypervisorPresent = false;  // *** authoritative: hypervisor running ***
    uint64_t whpFeatures = 0;           // WHvCapabilityCodeFeatures bitmask

    // ---- Environment / likely blockers ----
    bool elevated = false;
    bool vmwareInstalled = false;
    std::wstring vmwareDetail;
    bool virtualBoxInstalled = false;
    int  hypervisorLaunchType = -1;     // -1 unknown, 0 off, 1 auto, 2 on (bcdedit)
    std::wstring launchTypeDetail;

    // ---- Verdict ----
    Verdict verdict = Verdict::Unsupported;
    std::vector<std::wstring> reasons;      // why it is not Ready
    std::vector<std::wstring> remediation;  // ordered steps to make it Ready
};

// Runs every check and fills a PreflightResult. Never throws; degrades to
// "unknown" for any probe it cannot complete.
PreflightResult RunPreflight();

// Pretty-prints the result (colored) to the console.
void PrintPreflight(const PreflightResult& r);

} // namespace microvm
} // namespace av
