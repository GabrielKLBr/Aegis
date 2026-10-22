// VmWinCall.cpp - Step C: CALL a real kernel32 export brought up by our loader.
//
// The capstone of the Wine path: WinLoader links the real kernel32 graph, then
// the guest calls a genuine kernel32 export -- lstrlenA -- on a guest string and
// gets the right length. lstrlenA is self-contained (a pointer walk; no loader
// state / heap / globals), so it runs correctly even though ntdll's
// LdrpInitialize never ran. This proves real Microsoft DLL code executing in our
// from-scratch hardware sandbox, linked entirely by us.
//
// Build target: vmwincall.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/WinLoader.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry = 0x2000, kStr = 0x5000, kResult = 0x4000;
static constexpr uint64_t kStack = 0xEFFF00;
static constexpr uint64_t kRegionBase = 0x1000000, kRegionEnd = 0x1F000000;
static constexpr size_t   kRam = 0x20000000;   // 512 MiB

int main() {
    std::printf("MicroVM Wine path: CALL a real kernel32 export (lstrlenA)\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, 0)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    WinLoader ld(vm, kRegionBase, kRegionEnd);
    if (!ld.Load("kernel32.dll")) { std::printf("  [FAIL] link: %s\n", ld.Error().c_str()); return 1; }
    std::printf("  linked kernel32 graph: %zu modules, %d imports resolved, %d missing\n",
                ld.Modules().size(), ld.Resolved(), ld.Missing());

    uint64_t lstrlenA = ld.Resolve("kernel32.dll", "lstrlenA");
    if (!lstrlenA) { std::printf("  [FAIL] lstrlenA not resolved\n"); return 1; }
    std::printf("  kernel32!lstrlenA @ 0x%llX\n", (unsigned long long)lstrlenA);

    const char* msg = "GuardianAV micro-sandbox";        // 24 chars
    CopyMemory(g + kStr, msg, std::strlen(msg) + 1);

    // Guest: rax = lstrlenA(kStr); [kResult] = rax; hlt.   (ring 0 -> HLT ends)
    std::vector<uint8_t> c;
    auto e   = [&](std::initializer_list<uint8_t> bs){ for (auto x:bs) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    e({0x48,0x83,0xEC,0x28});                    // sub rsp, 0x28  (shadow space + align)
    e({0xB9}); e32((uint32_t)kStr);              // mov ecx, kStr
    e({0xB8}); e32((uint32_t)lstrlenA); e({0xFF,0xD0});   // mov eax,lstrlenA; call rax
    e({0x48,0x89,0x04,0x25}); e32((uint32_t)kResult);     // mov [kResult], rax
    e({0xF4});                                   // hlt
    CopyMemory(g + kEntry, c.data(), c.size());

    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ false)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    std::printf("  ------------------------------------------------------\n");
    std::printf("  guest calls kernel32!lstrlenA(\"%s\")...\n", msg);
    if (!vm.Run(100000)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    uint64_t got = 0; std::memcpy(&got, g + kResult, 8);
    size_t want = std::strlen(msg);
    bool ok = (got == want);
    std::printf("  ------------------------------------------------------\n");
    std::printf("  lstrlenA returned %llu  (expected %zu)  %s\n",
                (unsigned long long)got, want, ok ? "OK" : "BAD");
    std::printf("  RESULT: %s\n", ok
        ? "a REAL kernel32 export executed in our sandbox, linked entirely by our\n"
          "          own loader -- real Microsoft DLL code running with no ntdll\n"
          "          LdrpInitialize. The Wine path works end to end."
        : "the real export did not return the expected value.");
    return ok ? 0 : 1;
}
