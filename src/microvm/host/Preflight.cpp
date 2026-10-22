// Preflight.cpp - implementation of the virtualization readiness probe.
#include "Preflight.h"

#include <cstdio>   // _wpopen / _pclose
#include <cstring>  // memcpy
#include <cctype>   // tolower
#include <cwchar>   // swprintf

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace av {
namespace microvm {

// --------------------------------------------------------------------------
// CPUID helper (portable across MSVC and MinGW).
// --------------------------------------------------------------------------
static void CpuId(unsigned leaf, unsigned& a, unsigned& b, unsigned& c, unsigned& d) {
    a = b = c = d = 0;
#if defined(_MSC_VER)
    int r[4] = {0};
    __cpuid(r, (int)leaf);
    a = (unsigned)r[0]; b = (unsigned)r[1]; c = (unsigned)r[2]; d = (unsigned)r[3];
#else
    unsigned ra = 0, rb = 0, rc = 0, rd = 0;
    if (__get_cpuid(leaf, &ra, &rb, &rc, &rd)) { a = ra; b = rb; c = rc; d = rd; }
#endif
}

// --------------------------------------------------------------------------
// Small process-output capture (used only for the optional bcdedit probe).
// --------------------------------------------------------------------------
static std::string RunCapture(const wchar_t* cmd) {
    std::string out;
    FILE* p = _wpopen(cmd, L"rb");
    if (!p) return out;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    _pclose(p);
    return out;
}

static bool RegKeyExists(HKEY root, const wchar_t* sub) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        RegCloseKey(k);
        return true;
    }
    if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_32KEY, &k) == ERROR_SUCCESS) {
        RegCloseKey(k);
        return true;
    }
    return false;
}

static bool FileExists(const wchar_t* path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// --------------------------------------------------------------------------
// Individual probes.
// --------------------------------------------------------------------------
static void ProbeCpu(PreflightResult& r) {
    unsigned a, b, c, d;
    CpuId(0, a, b, c, d);
    char v[13];
    std::memcpy(v + 0, &b, 4);
    std::memcpy(v + 4, &d, 4);
    std::memcpy(v + 8, &c, 4);
    v[12] = 0;
    r.cpuVendor = Utf8ToWide(v);

    CpuId(1, a, b, c, d);
    r.hypervisorPresentCpuid = (c & (1u << 31)) != 0;   // running under a hypervisor
    bool vmx = (c & (1u << 5)) != 0;                     // Intel VT-x

    CpuId(0x80000001u, a, b, c, d);
    bool svm = (c & (1u << 2)) != 0;                     // AMD-V (SVM)

    r.cpuVmxOrSvm = vmx || svm;
}

static void ProbeWhp(PreflightResult& r) {
    typedef HRESULT(WINAPI * PFN_GetCap)(int, void*, UINT32, UINT32*);
    HMODULE h = LoadLibraryW(L"WinHvPlatform.dll");
    if (!h) return;
    r.whpDllPresent = true;

    auto fn = reinterpret_cast<PFN_GetCap>(
        reinterpret_cast<void*>(GetProcAddress(h, "WHvGetCapability")));
    if (fn) {
        r.whpApiPresent = true;

        // WHvCapabilityCodeHypervisorPresent = 0x00000000  -> BOOL
        BOOL present = FALSE;
        UINT32 written = 0;
        if (SUCCEEDED(fn(0x00000000, &present, sizeof(present), &written)))
            r.whpHypervisorPresent = (present != FALSE);

        // WHvCapabilityCodeFeatures = 0x00000001  -> union, first member is UINT64
        UINT64 feats = 0;
        written = 0;
        if (SUCCEEDED(fn(0x00000001, &feats, sizeof(feats), &written)))
            r.whpFeatures = feats;
    }
    FreeLibrary(h);
}

static void ProbeEnvironment(PreflightResult& r) {
    r.elevated = IsElevated();

    // VMware Workstation / Player.
    if (RegKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VMware, Inc.\\VMware Workstation") ||
        FileExists(L"C:\\Program Files (x86)\\VMware\\VMware Workstation\\vmware.exe") ||
        FileExists(L"C:\\Program Files (x86)\\VMware\\VMware Player\\vmplayer.exe")) {
        r.vmwareInstalled = true;
        r.vmwareDetail = L"VMware Workstation/Player detected";
    }

    // VirtualBox.
    if (RegKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Oracle\\VirtualBox") ||
        FileExists(L"C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe")) {
        r.virtualBoxInstalled = true;
    }

    // hypervisorlaunchtype is only readable from the BCD store when elevated.
    if (r.elevated) {
        std::string b = RunCapture(L"bcdedit /enum {current}");
        // Lower-case ASCII search; bcdedit emits ASCII tokens regardless of code page.
        for (auto& ch : b) ch = (char)std::tolower((unsigned char)ch);
        size_t pos = b.find("hypervisorlaunchtype");
        if (pos != std::string::npos) {
            std::string seg = b.substr(pos, 48);
            if (seg.find("off") != std::string::npos) {
                r.hypervisorLaunchType = 0;
                r.launchTypeDetail = L"Off (hypervisor disabled at boot)";
            } else if (seg.find("auto") != std::string::npos) {
                r.hypervisorLaunchType = 1;
                r.launchTypeDetail = L"Auto";
            } else if (seg.find("on") != std::string::npos) {
                r.hypervisorLaunchType = 2;
                r.launchTypeDetail = L"On";
            }
        } else if (!b.empty()) {
            // Token absent: on client SKUs this means the default (Auto).
            r.hypervisorLaunchType = 1;
            r.launchTypeDetail = L"Auto (default; not explicitly set)";
        }
    }
}

// --------------------------------------------------------------------------
// Verdict + remediation synthesis.
// --------------------------------------------------------------------------
static void Decide(PreflightResult& r) {
    if (r.whpApiPresent && r.whpHypervisorPresent) {
        r.verdict = Verdict::Ready;
        return;
    }

    // Not ready: explain and prescribe, in order.
    if (!r.cpuVmxOrSvm && !r.hypervisorPresentCpuid) {
        // No virtualization extensions visible and nothing is running them.
        r.verdict = Verdict::Unsupported;
        r.reasons.push_back(L"CPU virtualization (Intel VT-x / AMD-V) is not exposed.");
        r.remediation.push_back(
            L"Reboot into BIOS/UEFI and enable 'Intel Virtualization Technology "
            L"(VT-x)' or 'SVM Mode' (AMD). Save and reboot, then re-run 'av vmcheck'.");
    } else {
        // Hardware is capable; it just isn't wired up for WHP yet.
        r.verdict = Verdict::NeedsEnable;
    }

    if (!r.whpDllPresent) {
        r.reasons.push_back(L"Windows Hypervisor Platform (WHP) feature is not installed.");
        r.remediation.push_back(
            L"In an ELEVATED PowerShell:  Enable-WindowsOptionalFeature -Online "
            L"-FeatureName HypervisorPlatform -All");
        r.remediation.push_back(
            L"Also enable:  Enable-WindowsOptionalFeature -Online "
            L"-FeatureName VirtualMachinePlatform -All   then REBOOT.");
    } else if (!r.whpHypervisorPresent) {
        r.reasons.push_back(
            L"WHP is installed, but the Windows hypervisor is NOT running, so no "
            L"partition can be created.");
        if (r.hypervisorLaunchType == 0) {
            r.reasons.push_back(L"bcdedit shows hypervisorlaunchtype = Off.");
        }
        r.remediation.push_back(
            L"In an ELEVATED command prompt:  bcdedit /set hypervisorlaunchtype auto   "
            L"then REBOOT.");
        if (r.vmwareInstalled) {
            r.reasons.push_back(
                L"VMware is installed - it may have set hypervisorlaunchtype=off to take "
                L"the CPU's virtualization for itself.");
            r.remediation.push_back(
                L"VMware Workstation 15.5+/17 coexists with the Windows hypervisor: in "
                L"VMware, keep 'Windows Hypervisor Platform' mode, and do NOT run "
                L"'bcdedit /set hypervisorlaunchtype off'.");
        }
        r.remediation.push_back(
            L"Optional: turning ON Core Isolation > Memory Integrity (Windows Security > "
            L"Device security) also forces the hypervisor to launch.");
        if (!r.elevated) {
            r.remediation.push_back(
                L"Re-run 'av vmcheck' from an elevated prompt to read the exact "
                L"hypervisorlaunchtype value.");
        }
    }
}

PreflightResult RunPreflight() {
    PreflightResult r;
    ProbeCpu(r);
    ProbeWhp(r);
    ProbeEnvironment(r);
    Decide(r);
    return r;
}

// --------------------------------------------------------------------------
// Reporting.
// --------------------------------------------------------------------------
static std::wstring YesNo(bool b) { return b ? L"yes" : L"no"; }

void PrintPreflight(const PreflightResult& r) {
    PrintLine(L"MicroVM preflight - virtualization readiness", Color::Cyan);
    PrintLine(L"  ------------------------------------------------------", Color::Gray);

    PrintLine(L"CPU", Color::Cyan);
    PrintLine(L"  vendor              : " + (r.cpuVendor.empty() ? L"(unknown)" : r.cpuVendor),
              Color::Gray);
    PrintLine(L"  VT-x / AMD-V exposed : " + YesNo(r.cpuVmxOrSvm),
              r.cpuVmxOrSvm ? Color::Green : Color::Yellow);
    PrintLine(L"  under a hypervisor   : " + YesNo(r.hypervisorPresentCpuid), Color::Gray);

    PrintLine(L"", Color::Default);
    PrintLine(L"Windows Hypervisor Platform (WHP)", Color::Cyan);
    PrintLine(L"  WinHvPlatform.dll    : " + YesNo(r.whpDllPresent),
              r.whpDllPresent ? Color::Green : Color::Yellow);
    PrintLine(L"  WHvGetCapability     : " + YesNo(r.whpApiPresent),
              r.whpApiPresent ? Color::Green : Color::Yellow);
    PrintLine(L"  hypervisor present   : " + YesNo(r.whpHypervisorPresent),
              r.whpHypervisorPresent ? Color::Green : Color::Red);
    {
        wchar_t b[32];
        swprintf(b, 32, L"0x%016llX", (unsigned long long)r.whpFeatures);
        PrintLine(std::wstring(L"  features bitmask     : ") + b, Color::Gray);
    }

    PrintLine(L"", Color::Default);
    PrintLine(L"Environment", Color::Cyan);
    PrintLine(L"  elevated             : " + YesNo(r.elevated), Color::Gray);
    PrintLine(L"  VMware installed     : " + YesNo(r.vmwareInstalled),
              r.vmwareInstalled ? Color::Yellow : Color::Gray);
    PrintLine(L"  VirtualBox installed : " + YesNo(r.virtualBoxInstalled),
              r.virtualBoxInstalled ? Color::Yellow : Color::Gray);
    if (!r.launchTypeDetail.empty())
        PrintLine(L"  hypervisorlaunchtype : " + r.launchTypeDetail,
                  r.hypervisorLaunchType == 0 ? Color::Red : Color::Gray);
    else
        PrintLine(L"  hypervisorlaunchtype : (run elevated to read)", Color::Gray);

    PrintLine(L"", Color::Default);
    switch (r.verdict) {
        case Verdict::Ready:
            PrintLine(L"VERDICT: READY - WHP is usable. The MicroVM can create a partition.",
                      Color::Green);
            PrintLine(L"Next: roadmap step 1 (WHP PoC) can run on this machine.", Color::Gray);
            break;
        case Verdict::NeedsEnable:
            PrintLine(L"VERDICT: NEEDS ENABLE - capable hardware, but virtualization is off.",
                      Color::Yellow);
            break;
        case Verdict::Unsupported:
            PrintLine(L"VERDICT: UNSUPPORTED - no virtualization extensions are exposed.",
                      Color::Red);
            break;
    }

    if (!r.reasons.empty()) {
        PrintLine(L"", Color::Default);
        PrintLine(L"Why:", Color::Cyan);
        for (auto& s : r.reasons) PrintLine(L"  - " + s, Color::Yellow);
    }
    if (!r.remediation.empty()) {
        PrintLine(L"", Color::Default);
        PrintLine(L"How to fix (in order):", Color::Cyan);
        int i = 1;
        for (auto& s : r.remediation) {
            PrintLine(L"  " + std::to_wstring(i++) + L". " + s, Color::Gray);
        }
    }
}

} // namespace microvm
} // namespace av
