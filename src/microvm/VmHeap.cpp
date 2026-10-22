// VmHeap.cpp - Ldr (module list) + working heap: two more CRT prerequisites.
//
// A ring-3 guest:
//   * walks PEB->Ldr->InLoadOrderModuleList to read the first module's DllBase
//     (GetModuleHandle/CRT startup do exactly this), and
//   * calls GetProcessHeap + HeapAlloc, writes 0xCAFEBABE into the allocation,
//     and allocates again to show distinct blocks.
// Results are stored to a fixed guest address; the host verifies them. The heap
// is bump-allocated from a guest-RAM arena by the synthetic NtKernel.
//
// Build target: vmheap.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/ProcessEnv.h"
#include "host/NtKernel.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry     = 0x2000;
static constexpr uint64_t kResults   = 0x5000;
static constexpr uint64_t kEnvBase   = 0x300000;
static constexpr uint64_t kLdrBase   = 0x310000;
static constexpr uint64_t kStack     = 0xF00000;
static constexpr uint64_t kImageBase = 0x140000000ull;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam       = 0x1000000;

int main() {
    std::printf("MicroVM Ldr (module list) + heap (more CRT prerequisites)\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls (try 'av vmcheck')\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    const unsigned char code[] = {
        // walk Ldr
        0x65,0x48,0x8B,0x04,0x25,0x60,0x00,0x00,0x00,   // mov rax, gs:[0x60]  (PEB)
        0x48,0x8B,0x48,0x18,                            // mov rcx, [rax+0x18] (Ldr)
        0x48,0x8B,0x51,0x10,                            // mov rdx, [rcx+0x10] (InLoadOrder.Flink)
        0x48,0x8B,0x72,0x30,                            // mov rsi, [rdx+0x30] (ent0.DllBase)
        0x48,0x89,0x34,0x25,0x00,0x50,0x00,0x00,        // mov [0x5000], rsi
        // heap
        0xB8,0x0A,0x00,0x00,0x00, 0x0F,0x05,            // GetProcessHeap
        0x48,0x89,0xC3,                                 // mov rbx, rax  (heap)
        0x48,0x89,0xD9, 0x31,0xD2, 0x41,0xB8,0x40,0x00,0x00,0x00,  // rcx=heap, edx=0, r8d=0x40
        0xB8,0x0B,0x00,0x00,0x00, 0x0F,0x05,            // HeapAlloc
        0x48,0x89,0xC7,                                 // mov rdi, rax  (ptr1)
        0x48,0x89,0x3C,0x25,0x08,0x50,0x00,0x00,        // mov [0x5008], rdi
        0xC7,0x07,0xBE,0xBA,0xFE,0xCA,                  // mov dword [rdi], 0xCAFEBABE
        // second alloc
        0x48,0x89,0xD9, 0x31,0xD2, 0x41,0xB8,0x40,0x00,0x00,0x00,
        0xB8,0x0B,0x00,0x00,0x00, 0x0F,0x05,            // HeapAlloc
        0x48,0x89,0x04,0x25,0x10,0x50,0x00,0x00,        // mov [0x5010], rax  (ptr2)
        // exit
        0x31,0xC9, 0xB8,0x05,0x00,0x00,0x00, 0x0F,0x05, // ExitProcess(0)
        0xF4,                                           // hlt
    };
    CopyMemory(g + kEntry, code, sizeof(code));

    ProcessEnv env = BuildProcessEnv(vm, kEnvBase, kImageBase, kStack, 0xE00000,
                                     0x1A4, 0x1A8, L"C:\\sample\\malware.exe");
    std::vector<ModuleSpec> mods = {
        {L"sample.exe",   kImageBase,     0x10000},
        {L"ntdll.dll",    0x180000000ull, 0x200000},
        {L"ntkernel.dll", 0x190000000ull, 0x80000},
    };
    BuildLoaderData(vm, env, kLdrBase, mods);

    NtKernel kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    if (!vm.SetGsBase(env.teb)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    std::printf("  built Ldr with %zu modules; heap arena 0x%llX..0x%llX\n",
                mods.size(), (unsigned long long)kernel.HeapBase(), (unsigned long long)kernel.HeapEnd());
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    auto rd64 = [&](uint64_t a){ uint64_t v; std::memcpy(&v, g + a, 8); return v; };
    auto rd32 = [&](uint64_t a){ uint32_t v; std::memcpy(&v, g + a, 4); return v; };
    uint64_t modBase = rd64(kResults + 0x00);
    uint64_t ptr1    = rd64(kResults + 0x08);
    uint64_t ptr2    = rd64(kResults + 0x10);

    std::printf("  Ldr walk:\n");
    std::printf("    first module DllBase = 0x%llX  (expected 0x%llX) %s\n",
                (unsigned long long)modBase, (unsigned long long)kImageBase,
                modBase == kImageBase ? "OK" : "MISMATCH");
    std::printf("  heap:\n");
    bool p1ok = ptr1 >= kernel.HeapBase() && ptr1 < kernel.HeapEnd();
    bool p2ok = ptr2 >= kernel.HeapBase() && ptr2 < kernel.HeapEnd() && ptr2 != ptr1;
    std::printf("    HeapAlloc #1 = 0x%llX  %s\n", (unsigned long long)ptr1, p1ok ? "(in arena)" : "(BAD)");
    std::printf("    [ptr1] value = 0x%08X  %s\n", rd32(ptr1), rd32(ptr1) == 0xCAFEBABE ? "OK" : "MISMATCH");
    std::printf("    HeapAlloc #2 = 0x%llX  %s\n", (unsigned long long)ptr2, p2ok ? "(distinct, in arena)" : "(BAD)");

    std::printf("%s", kernel.Report().Render().c_str());

    bool ok = modBase == kImageBase && p1ok && p2ok && rd32(ptr1) == 0xCAFEBABE;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the guest enumerated loaded modules and used a working heap -- two\n"
          "          more pillars CRT startup needs before main()."
        : "Ldr/heap wiring is not correct yet.");
    return ok ? 0 : 1;
}
