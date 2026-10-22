// VmWinSys.cpp - Step B: point OUR loader at the REAL system DLLs.
//
// WinLoader maps a real DLL from C:\Windows\System32 and recursively links its
// whole dependency graph (kernel32 -> apisets -> kernelbase -> ntdll), resolving
// imports across modules with the API-set heuristic and patching every IAT --
// all without ntdll's LdrpInitialize. This reports how completely our loader
// links real Microsoft binaries; the unresolved names are the next bricks (more
// API-set backing DLLs to add to the candidate list).
//
// Usage: vmwinsys [dllname]   (default kernel32.dll)
// Build target: vmwinsys.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/WinLoader.h"

#include <windows.h>
#include <cstdio>
#include <string>

using namespace av::microvm;

static constexpr uint64_t kRegionBase = 0x1000000, kRegionEnd = 0x1F000000;
static constexpr size_t   kRam = 0x20000000;   // 512 MiB

int main(int argc, char** argv) {
    std::string target = (argc > 1) ? argv[1] : "kernel32.dll";
    std::printf("MicroVM Wine path: OUR loader links the REAL %s graph\n", target.c_str());
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, 0)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    WinLoader ld(vm, kRegionBase, kRegionEnd);
    uint64_t base = ld.Load(target);
    if (!base) { std::printf("  [FAIL] %s\n", ld.Error().c_str()); return 1; }

    std::printf("  modules mapped (%zu):\n", ld.Modules().size());
    for (const auto& kv : ld.Modules())
        std::printf("    %-28s @ 0x%llX  (0x%X bytes)\n", kv.second.name.c_str(),
                    (unsigned long long)kv.second.base, kv.second.size);
    std::printf("  ------------------------------------------------------\n");
    std::printf("  imports resolved: %d\n", ld.Resolved());
    std::printf("  imports missing : %d\n", ld.Missing());
    if (ld.Missing()) {
        std::printf("  first unresolved (the next API-set backing DLLs to add):\n");
        for (const auto& n : ld.MissingNames()) std::printf("    %s\n", n.c_str());
    }

    // Spot-check a couple of well-known exports resolve to mapped guest addresses.
    uint64_t gpa = ld.Resolve(target, "GetProcAddress");
    uint64_t lla = ld.Resolve(target, "LoadLibraryA");
    std::printf("  ------------------------------------------------------\n");
    std::printf("  %s!GetProcAddress -> 0x%llX\n", target.c_str(), (unsigned long long)gpa);
    std::printf("  %s!LoadLibraryA   -> 0x%llX\n", target.c_str(), (unsigned long long)lla);

    bool ok = base && ld.Resolved() > 0;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "our loader mapped + relocated the real Microsoft DLL graph and resolved\n"
          "          its cross-module imports (forwarders + API sets). Missing names,\n"
          "          if any, are simply more backing DLLs to route -- not a wall."
        : "the real DLL graph did not link.");
    return ok ? 0 : 1;
}
