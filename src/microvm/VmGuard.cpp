// VmGuard.cpp - hardening ("blindagem") of the NT personality.
//
// Two things malware does to escape a naive sandbox, both caught here on real
// hardware-isolated WHP:
//
//   A) It issues the raw SYSCALL instruction directly, bypassing any hooked
//      system DLLs. We run the guest at CPL3 with SCE off, so every SYSCALL --
//      ours or a malware's direct one -- raises #UD, which we intercept and
//      service in our synthetic kernel.
//   B) It executes a privileged instruction (CLI/OUT/RDMSR/MOV CRx...). At CPL3
//      that faults with #GP, which we intercept and report as a blocked action.
//
// Build target: vmguard.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"

#include <cstdio>
#include <string>

using namespace av::microvm;

static constexpr uint64_t kEntry = 0x2000;
static constexpr uint64_t kData  = 0x3000;
static constexpr uint64_t kStack = 0x6FF00;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);  // #UD | #GP
enum : uint64_t { SYS_Print = 1, SYS_Exit = 2 };

static uint64_t Reg(MicroVm& vm, WHV_REGISTER_NAME n) {
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Reg64;
}

// Scenario A: a ring-3 guest that uses the real SYSCALL instruction.
static bool ScenarioDirectSyscall() {
    std::printf("A) Direct SYSCALL from ring-3 (bypasses DLL hooks)\n");
    const std::string msg = "ring-3 guest issued a raw SYSCALL; our kernel answered";

    MicroVm vm;
    if (!vm.Create(0x100000, kExcBitmap)) { std::printf("   [FAIL] %ls\n", vm.Error().c_str()); return false; }
    auto* mem = static_cast<uint8_t*>(vm.RamData());
    CopyMemory(mem + kData, msg.data(), msg.size());

    const uint32_t len = (uint32_t)msg.size();
    const unsigned char code[] = {
        0xB8, SYS_Print, 0, 0, 0,                                   // mov eax, 1
        0xBF, (unsigned char)(kData & 0xFF), (unsigned char)(kData >> 8), 0, 0, // mov edi, 0x3000
        0xBE, (unsigned char)(len & 0xFF), (unsigned char)(len >> 8), 0, 0,     // mov esi, len
        0x0F, 0x05,                                                 // syscall
        0xB8, SYS_Exit, 0, 0, 0,                                    // mov eax, 2
        0x31, 0xFF,                                                 // xor edi, edi
        0x0F, 0x05,                                                 // syscall
        0xCC,                                                       // int3 (safety)
    };
    CopyMemory(mem + kEntry, code, sizeof(code));

    std::string captured;
    int exitCode = -1;
    vm.OnSyscall([&](MicroVm& m) {
        uint64_t sys = Reg(m, WHvX64RegisterRax);
        if (sys == SYS_Print) {
            uint64_t ptr = Reg(m, WHvX64RegisterRdi), n = Reg(m, WHvX64RegisterRsi);
            const char* g = static_cast<const char*>(m.RamData());
            if (ptr + n <= m.RamSize()) captured.assign(g + ptr, n);
            WHV_REGISTER_NAME rn = WHvX64RegisterRax;
            WHV_REGISTER_VALUE rv; ZeroMemory(&rv, sizeof(rv)); rv.Reg64 = n;
            m.SetRegisters(&rn, 1, &rv);
        } else if (sys == SYS_Exit) {
            exitCode = (int)Reg(m, WHvX64RegisterRdi);
            m.RequestStop();
        }
    });

    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("   [FAIL] %ls\n", vm.Error().c_str()); return false; }
    bool halted = vm.Run();
    if (!halted) { std::printf("   [FAIL] %ls\n", vm.Error().c_str()); return false; }
    std::printf("   serviced syscalls: %llu\n", (unsigned long long)vm.ExitCount());
    std::printf("   kernel captured  : \"%s\"  (exit %d)\n", captured.c_str(), exitCode);
    bool ok = captured == msg && exitCode == 0;
    std::printf("   -> %s\n\n", ok ? "PASS: raw SYSCALL trapped and serviced." : "FAIL");
    return ok;
}

// Scenario B: a ring-3 guest that tries a privileged instruction.
static bool ScenarioPrivilegedOp() {
    std::printf("B) Privileged instruction from ring-3 (CLI)\n");
    MicroVm vm;
    if (!vm.Create(0x100000, kExcBitmap)) { std::printf("   [FAIL] %ls\n", vm.Error().c_str()); return false; }
    auto* mem = static_cast<uint8_t*>(vm.RamData());
    const unsigned char code[] = { 0xFA, 0xF4 };  // cli ; hlt
    CopyMemory(mem + kEntry, code, sizeof(code));

    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("   [FAIL] %ls\n", vm.Error().c_str()); return false; }
    bool halted = vm.Run();
    bool caught = !halted && vm.LastExceptionVector() == 13;  // #GP
    std::printf("   guest result     : %s\n", halted ? "ran (unexpected!)" : "faulted");
    std::printf("   caught exception : vector %u at rip=0x%llX\n",
                (unsigned)vm.LastExceptionVector(), (unsigned long long)vm.LastExceptionRip());
    std::printf("   -> %s\n\n", caught ? "PASS: privileged op blocked (#GP) before it ran."
                                       : "FAIL");
    return caught;
}

int main() {
    std::printf("MicroVM hardening: evasion-resistant syscall + privilege trapping\n");
    std::printf("  ------------------------------------------------------\n");
    bool a = ScenarioDirectSyscall();
    bool b = ScenarioPrivilegedOp();
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", (a && b)
        ? "both evasions are caught. Direct syscalls reach our kernel; ring-3\n"
          "          privileged ops fault before touching anything."
        : "one or more checks failed.");
    return (a && b) ? 0 : 1;
}
