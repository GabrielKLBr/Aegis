// VmWinAll.cpp - automated coverage: CALL every kernel32 export, classify each.
//
// After our WinLoader links the real kernel32 graph, this drives EVERY exported
// function automatically: for each export it resets the vCPU, points RIP at the
// function with null args and a HLT return trampoline on the stack, runs it (with
// MiniNt servicing any syscalls + a real TEB/PEB/KUSER environment), and a
// per-call watchdog cancels spinners. Each outcome is classified -- returned,
// process-exit syscall, #PF/#GP/#UD fault, or spin -- and tallied. This paints,
// with no manual signatures, the real state of every kernel32 export under our
// loader. (Args are null; page 0 is mapped RAM here, so null derefs read zeros
// rather than faulting -- the classification reflects this environment.)
//
// Build target: vmwinall.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/WinLoader.h"
#include "host/ProcessEnv.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEnvBase = 0x200000, kRet = 0x3000;
static constexpr uint64_t kKusdVa = 0x7FE00000, kKusdBack = 0xA00000;
static constexpr uint64_t kStack = 0xEFFEF8;                 // 8 mod 16 (ABI entry)
static constexpr uint64_t kRegionBase = 0x1000000, kRegionEnd = 0x10000000;
static constexpr uint64_t kArenaBase = 0x10000000, kArenaEnd = 0x1F000000;
static constexpr size_t   kRam = 0x20000000;                 // 512 MiB
static constexpr uint64_t kExcBitmap = (1ull<<6)|(1ull<<13)|(1ull<<14); // #UD|#GP|#PF
static constexpr int      kTimeoutMs = 100;

enum Outcome { RETURNED, EXITED, PF, GP, UD, SPIN, LOOPED, OTHER, NUMOUT };
static const char* kName[NUMOUT] = {
    "returned", "called process-exit", "#PF fault", "#GP fault",
    "#UD bad-instr", "spin (watchdog)", "exit-budget loop", "other" };

int main() {
    std::printf("MicroVM Wine path: AUTO-CALL every kernel32 export, classify each\n");
    std::printf("  ==============================================================\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr64 = [&](uint64_t a, uint64_t v){ CopyMemory(g+a,&v,8); };

    WinLoader ld(vm, kRegionBase, kRegionEnd);
    uint64_t k32 = ld.Load("kernel32.dll");
    if (!k32) { std::printf("  [FAIL] link: %s\n", ld.Error().c_str()); return 1; }
    std::printf("  linked: %zu modules, %d imports resolved, %d missing\n",
                ld.Modules().size(), ld.Resolved(), ld.Missing());

    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);
    ProcessEnv env = BuildProcessEnv(vm, kEnvBase, k32, 0xF00000, 0xE00000,
                                     0x1000, 0x1004, L"C:\\sample.exe");

    MiniNt kernel;
    kernel.SetArena(kArenaBase, 0x14000000);
    kernel.SetHeapRegion(0x14000000, 0x1E000000);
    kernel.SetLoaderMode(true);
    vm.OnSyscall([&](MicroVm& m){ kernel.Dispatch(m); });

    if (!vm.SetupLongMode(kRet, kStack, /*cpl3*/ false)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.SetGsBase(env.teb);
    vm.MapPage2MB(kKusdVa, kKusdBack);

    // Wine patches: heap -> bump allocator, locks -> no-ops (single-threaded).
    int patched = ApplyWinePatches(vm, ld);
    std::printf("  hot-patched %d ntdll heap/lock primitives\n", patched);

    auto getReg = [&](WHV_REGISTER_NAME n){ WHV_REGISTER_VALUE v; ZeroMemory(&v,sizeof v); vm.GetRegisters(&n,1,&v); return v.Reg64; };
    auto setCall = [&](uint64_t addr){
        g[kRet] = 0xF4;                                       // HLT return trampoline
        wr64(kStack, kRet);                                   // return address
        for (int i = 8; i <= 0x40; i += 8) wr64(kStack + i, 0);   // shadow + stack args
        const WHV_REGISTER_NAME ns[] = {
            WHvX64RegisterRip, WHvX64RegisterRsp, WHvX64RegisterRflags,
            WHvX64RegisterRax, WHvX64RegisterRbx, WHvX64RegisterRcx, WHvX64RegisterRdx,
            WHvX64RegisterRsi, WHvX64RegisterRdi, WHvX64RegisterRbp,
            WHvX64RegisterR8,  WHvX64RegisterR9,  WHvX64RegisterR10, WHvX64RegisterR11,
            WHvX64RegisterR12, WHvX64RegisterR13, WHvX64RegisterR14, WHvX64RegisterR15 };
        WHV_REGISTER_VALUE vs[18]; ZeroMemory(vs, sizeof vs);
        vs[0].Reg64 = addr; vs[1].Reg64 = kStack; vs[2].Reg64 = 0x202;
        vm.SetRegisters(ns, 18, vs);
    };
    // Run the function under a per-call watchdog. Returns true if it halted.
    auto runOnce = [&](uint64_t addr){
        setCall(addr);
        kernel.ResetTransient();
        std::mutex mtx; bool done = false;
        std::thread wd([&]{
            for (int i = 0; i < kTimeoutMs / 5; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                std::lock_guard<std::mutex> lk(mtx); if (done) return;
            }
            std::lock_guard<std::mutex> lk(mtx); if (!done) vm.Cancel();
        });
        bool halted = vm.Run(100000);
        { std::lock_guard<std::mutex> lk(mtx); done = true; }
        wd.join();
        return halted;
    };
    auto classify = [&](uint64_t addr) -> Outcome {
        bool halted = runOnce(addr);
        // Absorb a spurious latched cancel from the previous call: canceled with
        // no progress (RIP still at entry) -> retry once.
        if (!halted && vm.LastExceptionVector() == 0xFF && getReg(WHvX64RegisterRip) == addr)
            halted = runOnce(addr);
        if (halted) {
            // HLT advances RIP by 1, so the trampoline lands at kRet or kRet+1.
            uint64_t rip = getReg(WHvX64RegisterRip);
            return (rip == kRet || rip == kRet + 1) ? RETURNED : EXITED;
        }
        switch (vm.LastExceptionVector()) {
            case 14: return PF;
            case 13: return GP;
            case 6:  return UD;
            case 0xFF: return SPIN;
            default: return vm.Error().find(L"budget") != std::wstring::npos ? LOOPED : OTHER;
        }
    };

    std::vector<std::string> names = ld.Exports("kernel32.dll");
    std::printf("  kernel32.dll exports: %zu  (calling each with null args)\n", names.size());
    std::printf("  ==============================================================\n");

    int counts[NUMOUT] = {0};
    std::map<int, std::vector<std::string>> examples;
    int data = 0, done = 0;
    for (const auto& n : names) {
        uint64_t addr = ld.Resolve("kernel32.dll", n);
        if (!addr) { ++data; continue; }                     // data export / unresolved
        Outcome o = classify(addr);
        ++counts[o];
        if (examples[o].size() < 10) examples[o].push_back(n);
        if (++done % 200 == 0) std::printf("  ... %d/%zu called\n", done, names.size());
    }

    std::printf("  ==============================================================\n");
    std::printf("  RESULTS over %d called exports (%d data/unresolved skipped):\n", done, data);
    for (int o = 0; o < NUMOUT; ++o) {
        if (!counts[o]) continue;
        std::printf("    %-22s %5d  (%.1f%%)\n", kName[o], counts[o], 100.0 * counts[o] / done);
        std::printf("        e.g. ");
        for (size_t i = 0; i < examples[o].size(); ++i)
            std::printf("%s%s", examples[o][i].c_str(), i + 1 < examples[o].size() ? ", " : "");
        std::printf("\n");
    }
    std::printf("  ==============================================================\n");
    std::printf("  RESULT: every kernel32 export was driven automatically through our\n");
    std::printf("          loader; '%s' ran to completion without faulting. The rest\n", kName[RETURNED]);
    std::printf("          fault/spin on null args or need initialized DllMain state --\n");
    std::printf("          the concrete map of what running real binaries needs next.\n");
    return 0;
}
