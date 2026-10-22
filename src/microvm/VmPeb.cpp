// VmPeb.cpp - PEB/TEB foundation: prove a ring-3 guest can read its process
// environment through GS exactly like ntdll/CRT do before main().
//
// The guest runs:
//   rax = gs:[0x60]      ; PEB           (TEB.ProcessEnvironmentBlock)
//   r8  = rax            ; keep PEB
//   rcx = [rax+0x10]     ; PEB->ImageBaseAddress
//   rdx = gs:[0x30]      ; TEB self
//   syscall #1 (report rcx=imageBase, rdx=TEB, r8=PEB)
//   syscall #2 (exit)
// The host verifies those match the TEB/PEB it built, then walks
// PEB->ProcessParameters->CommandLine to show the chain is fully navigable.
//
// Build target: vmpeb.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/ProcessEnv.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace av::microvm;

static constexpr uint64_t kEntry     = 0x2000;
static constexpr uint64_t kEnvBase   = 0x300000;       // TEB/PEB/params scratch
static constexpr uint64_t kStack     = 0xF00000;
static constexpr uint64_t kStackLim  = 0xE00000;
static constexpr uint64_t kImageBase = 0x140000000ull; // typical Win64 image base
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam       = 0x1000000;

static uint64_t Reg(MicroVm& vm, WHV_REGISTER_NAME n) {
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Reg64;
}

int main() {
    std::printf("MicroVM PEB/TEB foundation (toward running real CRT binaries)\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls (try 'av vmcheck')\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    const unsigned char code[] = {
        0x65,0x48,0x8B,0x04,0x25,0x60,0x00,0x00,0x00,  // mov rax, gs:[0x60]   (PEB)
        0x49,0x89,0xC0,                                // mov r8, rax          (keep PEB)
        0x48,0x8B,0x48,0x10,                           // mov rcx, [rax+0x10]  (ImageBase)
        0x65,0x48,0x8B,0x14,0x25,0x30,0x00,0x00,0x00,  // mov rdx, gs:[0x30]   (TEB self)
        0xB8,0x01,0x00,0x00,0x00, 0x0F,0x05,           // mov eax,1 ; syscall
        0xB8,0x02,0x00,0x00,0x00, 0x0F,0x05,           // mov eax,2 ; syscall
        0xF4,                                          // hlt
    };
    CopyMemory(g + kEntry, code, sizeof(code));

    ProcessEnv env = BuildProcessEnv(vm, kEnvBase, kImageBase, kStack, kStackLim,
                                     /*pid*/ 0x1A4, /*tid*/ 0x1A8,
                                     L"C:\\sample\\malware.exe --run");

    uint64_t sawImage = 0, sawTeb = 0, sawPeb = 0;
    bool reported = false;
    vm.OnSyscall([&](MicroVm& m) {
        uint64_t sys = Reg(m, WHvX64RegisterRax);
        if (sys == 1) {
            sawImage = Reg(m, WHvX64RegisterRcx);
            sawTeb   = Reg(m, WHvX64RegisterRdx);
            sawPeb   = Reg(m, WHvX64RegisterR8);
            reported = true;
        } else if (sys == 2) {
            m.RequestStop();
        }
    });

    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    if (!vm.SetGsBase(env.teb)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    std::printf("  built TEB=0x%llX  PEB=0x%llX  params=0x%llX\n",
                (unsigned long long)env.teb, (unsigned long long)env.peb,
                (unsigned long long)env.params);
    std::printf("  GS base -> TEB; entering ring-3 guest...\n");
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    if (!reported) { std::printf("  [FAIL] guest did not report\n"); return 1; }

    std::printf("  guest read via GS:\n");
    std::printf("    gs:[0x30] TEB self      = 0x%llX  (built 0x%llX) %s\n",
                (unsigned long long)sawTeb, (unsigned long long)env.teb,
                sawTeb == env.teb ? "OK" : "MISMATCH");
    std::printf("    gs:[0x60] PEB           = 0x%llX  (built 0x%llX) %s\n",
                (unsigned long long)sawPeb, (unsigned long long)env.peb,
                sawPeb == env.peb ? "OK" : "MISMATCH");
    std::printf("    PEB->ImageBaseAddress   = 0x%llX  (built 0x%llX) %s\n",
                (unsigned long long)sawImage, (unsigned long long)kImageBase,
                sawImage == kImageBase ? "OK" : "MISMATCH");

    // Walk PEB->ProcessParameters->CommandLine to prove the chain.
    auto rd64 = [&](uint64_t a){ uint64_t v; std::memcpy(&v, g + a, 8); return v; };
    auto rd16 = [&](uint64_t a){ uint16_t v; std::memcpy(&v, g + a, 2); return v; };
    uint64_t params = rd64(env.peb + 0x20);
    uint16_t clBytes = rd16(params + 0x70);
    uint64_t clBuf = rd64(params + 0x78);
    std::string cl;
    for (int i = 0; i < clBytes / 2; ++i) cl.push_back((char)rd16(clBuf + i * 2));
    std::printf("    PEB->...->CommandLine   = \"%s\"\n", cl.c_str());

    bool ok = sawTeb == env.teb && sawPeb == env.peb && sawImage == kImageBase;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the guest navigated TEB -> PEB -> ImageBase through GS, like real\n"
          "          ntdll/CRT. This is the foundation CRT startup stands on."
        : "TEB/PEB wiring is not correct yet.");
    return ok ? 0 : 1;
}
