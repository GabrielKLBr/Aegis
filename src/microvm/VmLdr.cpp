// VmLdr.cpp - Model B: THE BOSS. Run the real ntdll loader bootstrap.
//
// Instead of calling Nt* by hand, we point the vCPU at ntdll's real
// LdrInitializeThunk -- the entry the Windows kernel itself jumps to on a new
// thread -- with a genuine process environment (TEB/PEB/ProcessParameters/Ldr)
// and let the REAL loader drive. MiniNt services every syscall it makes and
// traces them; #UD/#GP/#PF all trap so we can report the EXACT point the
// bring-up reaches. Reaching NtContinue == LdrpInitialize completed.
//
// This is a probe: the deliverable is the precise wall (first missing Nt* /
// fault), which turns "the loader" into a concrete list of next bricks.
//
// Build target: vmldr.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/ProcessEnv.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEnvBase = 0x200000, kLdrBase = 0x280000, kMainImg = 0x300000;
static constexpr uint64_t kNtdll = 0x400000;
static constexpr uint64_t kCtx = 0x700000, kEntryStub = 0x701000;
static constexpr uint64_t kKusdVa = 0x7FE00000, kKusdBack = 0xA00000;
// kLdrStack is 8 mod 16: the x64 ABI expects RSP ≡ 8 (mod 16) on function entry
// (as if a CALL had pushed the return address); else the loader's `movaps`
// 16-aligned spills #GP.
static constexpr uint64_t kLdrStack = 0xEFFEF8, kUserStack = 0xDFFF00;
static constexpr uint64_t kArenaBase = 0x1000000, kArenaEnd = 0x3E00000;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13) | (1ull << 14); // #UD|#GP|#PF
static constexpr size_t   kRam = 0x4000000;   // 64 MiB

int main() {
    std::printf("MicroVM Model B: THE BOSS -- real ntdll LdrInitializeThunk bring-up\n");
    std::printf("  ==============================================================\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr16 = [&](uint64_t a, uint16_t v){ CopyMemory(g+a,&v,2); };
    auto wr32 = [&](uint64_t a, uint32_t v){ CopyMemory(g+a,&v,4); };
    auto wr64 = [&](uint64_t a, uint64_t v){ CopyMemory(g+a,&v,8); };

    PeLoader loader; PeImage img;
    ImportResolver none = [](const std::string&, const std::string&) -> uint64_t { return 0; };
    if (!loader.Load(vm, L"C:\\Windows\\System32\\ntdll.dll", kNtdll, none, img)) {
        std::printf("  [FAIL] load ntdll: %ls\n", loader.Error().c_str()); return 1;
    }
    std::printf("  ntdll mapped @ 0x%llX, SizeOfImage 0x%llX, entry thunk resolves below\n",
                (unsigned long long)kNtdll, (unsigned long long)img.sizeOfImage);
    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);

    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    auto addr = [&](const char* n){ return kNtdll + (uint64_t)((uint8_t*)GetProcAddress(hn,n) - (uint8_t*)hn); };
    uint64_t aThunk = addr("LdrInitializeThunk");
    std::printf("  LdrInitializeThunk @ 0x%llX\n", (unsigned long long)aThunk);

    // A minimal but valid main-image PE header at kMainImg, so the loader finds
    // sane headers if it inspects PEB->ImageBaseAddress.
    wr16(kMainImg + 0, 0x5A4D);                      // 'MZ'
    wr32(kMainImg + 0x3C, 0x80);                     // e_lfanew
    wr32(kMainImg + 0x80, 0x00004550);              // 'PE\0\0'
    wr16(kMainImg + 0x84, 0x8664);                   // Machine = AMD64
    wr16(kMainImg + 0x86, 0);                        // NumberOfSections
    wr16(kMainImg + 0x94, 0xF0);                     // SizeOfOptionalHeader
    wr16(kMainImg + 0x98, 0x20B);                    // PE32+
    wr32(kMainImg + 0x80 + 0x28, 0x1000);           // AddressOfEntryPoint
    wr64(kMainImg + 0x80 + 0x30, kMainImg);         // ImageBase
    wr32(kMainImg + 0x80 + 0x38, 0x1000);           // SectionAlignment
    wr32(kMainImg + 0x80 + 0x3C, 0x200);            // FileAlignment
    wr32(kMainImg + 0x80 + 0x50, 0x2000);           // SizeOfImage
    wr32(kMainImg + 0x80 + 0x54, 0x200);            // SizeOfHeaders

    // Real process environment: TEB/PEB/ProcessParameters + a loader module list.
    ProcessEnv env = BuildProcessEnv(vm, kEnvBase, kMainImg, kUserStack, kUserStack - 0x40000,
                                     0x1000, 0x1004, L"C:\\sample.exe");
    // NOTE: we deliberately do NOT pre-build PEB->Ldr. The real LdrpInitialize
    // builds the module list itself and walks it expecting its own sentinel; a
    // hand-built circular list makes it spin. Leave PEB->Ldr = 0.
    (void)kLdrBase;

    // The CONTEXT the loader will NtContinue into when init completes.
    g[kEntryStub] = 0xF4;                             // HLT = "image entry reached"
    ZeroMemory(g + kCtx, 0x4D0);
    wr32(kCtx + 0x30, 0x10000B);                     // ContextFlags = CONTEXT_FULL
    wr16(kCtx + 0x38, 0x33);                         // SegCs
    wr16(kCtx + 0x42, 0x2B);                         // SegSs
    wr32(kCtx + 0x44, 0x202);                        // EFlags
    wr64(kCtx + 0x98, kUserStack);                   // Rsp
    wr64(kCtx + 0xF8, kEntryStub);                   // Rip

    MiniNt kernel;
    kernel.SetArena(kArenaBase, kArenaEnd);
    kernel.SetLoaderMode(true);
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });

    // Enter at the loader thunk: RCX=&CONTEXT, RDX=ntdll base, a return stub on
    // the stack, plus shadow space -- the shape the kernel hands the new thread.
    if (!vm.SetupLongMode(aThunk, kLdrStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.SetGsBase(env.teb);
    vm.MapPage2MB(kKusdVa, kKusdBack);
    wr64(kLdrStack, kEntryStub);                     // return address (if it RETs)
    {
        WHV_REGISTER_NAME n[2] = { WHvX64RegisterRcx, WHvX64RegisterRdx };
        WHV_REGISTER_VALUE v[2]; ZeroMemory(v, sizeof(v));
        v[0].Reg64 = kCtx; v[1].Reg64 = kNtdll;
        vm.SetRegisters(n, 2, v);
    }

    std::printf("  handing control to the real loader (RCX=&CONTEXT, RDX=ntdll)...\n");
    std::printf("  ==============================================================\n");

    // Watchdog: the loader can spin with no VM-exits (waiting on a flag another
    // thread would set). If Run() doesn't return within a few seconds, cancel the
    // vCPU so we learn the RIP it was stuck at instead of hanging forever.
    std::atomic<bool> done{false};
    std::thread watchdog([&]{
        for (int i = 0; i < 50 && !done.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!done.load()) vm.Cancel();
    });
    bool halted = vm.Run(2'000'000);
    done.store(true);
    watchdog.join();

    std::printf("%s", kernel.Report().Render().c_str());
    std::printf("  ==============================================================\n");
    if (kernel.ReachedContinue()) {
        std::printf("  RESULT: *** LOADER COMPLETED *** LdrpInitialize ran to NtContinue;\n");
        std::printf("          it would now jump to the image entry @ 0x%llX.\n",
                    (unsigned long long)kernel.ContinueRip());
        return 0;
    }
    if (!halted) {
        std::printf("  WALL: %ls\n", vm.Error().c_str());
        std::printf("  (vector %u = %s)\n", vm.LastExceptionVector(),
            vm.LastExceptionVector()==14 ? "#PF page-fault" :
            vm.LastExceptionVector()==13 ? "#GP general-protection" :
            vm.LastExceptionVector()==6  ? "#UD" : "other");
    } else {
        std::printf("  WALL: the loader stopped (HLT / backstop) -- see the trace above\n");
        std::printf("        for the last call before it gave up.\n");
    }
    std::printf("  This is the boss probe: the trace above is the concrete list of\n");
    std::printf("  what LdrpInitialize needs next. Each line is the next brick.\n");
    return 2;
}
