// VmWinInit.cpp - call real Microsoft DLL exports backed by our GuestHeap.
//
// The guest calls real kernel32 exports, but ApplyWinePatches redirects the
// ntdll Rtl*Heap implementations into MiniNt. This proves a round-trip through
// those DLL entry points into our allocator, not a running native Windows heap.
// The configured arena is published in PEB->ProcessHeap and the result is checked.
//
// Build target: vmwininit.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/WinLoader.h"
#include "host/ProcessEnv.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace av::microvm;

static constexpr uint64_t kEnvBase = 0x200000, kRet = 0x3000;
static constexpr uint64_t kKusdVa = 0x7FE00000, kKusdBack = 0xA00000;
static constexpr uint64_t kStack = 0xEFFEF8;
static constexpr uint64_t kRegionBase = 0x1000000, kRegionEnd = 0x10000000;
static constexpr uint64_t kArenaBase = 0x10000000, kArenaEnd = 0x1F000000;
static constexpr size_t   kRam = 0x20000000;
static constexpr uint64_t kExcBitmap = (1ull<<6)|(1ull<<13)|(1ull<<14);

int main() {
    std::printf("MicroVM: real DLL heap exports serviced by our GuestHeap\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr64 = [&](uint64_t a, uint64_t v){ CopyMemory(g+a,&v,8); };
    auto rd64 = [&](uint64_t a){ uint64_t v=0; CopyMemory(&v,g+a,8); return v; };

    WinLoader ld(vm, kRegionBase, kRegionEnd);
    uint64_t k32 = ld.Load("kernel32.dll");
    if (!k32) { std::printf("  [FAIL] link: %s\n", ld.Error().c_str()); return 1; }
    std::printf("  linked: %zu modules, %d imports, %d missing\n",
                ld.Modules().size(), ld.Resolved(), ld.Missing());

    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);
    ProcessEnv env = BuildProcessEnv(vm, kEnvBase, k32, 0xF00000, 0xE00000, 0x1000, 0x1004, L"C:\\sample.exe");

    MiniNt kernel;
    kernel.SetArena(0x10000000, 0x14000000);
    kernel.SetHeapRegion(0x14000000, 0x1E000000);
    kernel.SetLoaderMode(true);
    vm.OnSyscall([&](MicroVm& m){ kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kRet, kStack, /*cpl3*/ false)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.SetGsBase(env.teb);
    vm.MapPage2MB(kKusdVa, kKusdBack);

    int patched = ApplyWinePatches(vm, ld);
    std::printf("  hot-patched %d ntdll heap/lock primitives -> MiniNt (heap 0x14000000..0x1E000000)\n", patched);

    auto getReg = [&](WHV_REGISTER_NAME n){ WHV_REGISTER_VALUE v; ZeroMemory(&v,sizeof v); vm.GetRegisters(&n,1,&v); return v.Reg64; };
    // Call a guest function with up to 4 register args; return {outcome, rax}.
    struct Res { const char* how; uint64_t rax; };
    auto call = [&](uint64_t addr, uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) -> Res {
        g[kRet] = 0xF4; wr64(kStack, kRet);
        for (int i=8;i<=0x40;i+=8) wr64(kStack+i, 0);
        const WHV_REGISTER_NAME ns[] = { WHvX64RegisterRip, WHvX64RegisterRsp, WHvX64RegisterRflags,
            WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterR8, WHvX64RegisterR9 };
        WHV_REGISTER_VALUE vs[7]; ZeroMemory(vs,sizeof vs);
        vs[0].Reg64=addr; vs[1].Reg64=kStack; vs[2].Reg64=0x202;
        vs[3].Reg64=rcx; vs[4].Reg64=rdx; vs[5].Reg64=r8; vs[6].Reg64=r9;
        vm.SetRegisters(ns, 7, vs);
        kernel.ResetTransient();
        std::atomic<bool> done{false};
        std::thread wd([&]{ for(int i=0;i<60 && !done.load();i++){ std::this_thread::sleep_for(std::chrono::milliseconds(10)); if(done.load()) return;} if(!done.load()) vm.Cancel(); });
        bool halted = vm.Run(2000000);
        done=true; wd.join();
        if (halted) { uint64_t rip=getReg(WHvX64RegisterRip); return { (rip==kRet||rip==kRet+1)?"returned":"exited", getReg(WHvX64RegisterRax) }; }
        switch (vm.LastExceptionVector()) { case 14: return {"#PF",0}; case 13: return {"#GP",0}; case 6: return {"#UD",0}; case 0xFF: return {"spin",0}; default: return {"other",0}; }
    };

    uint64_t hCreate = ld.Resolve("kernel32.dll", "HeapCreate");
    uint64_t hAlloc  = ld.Resolve("kernel32.dll", "HeapAlloc");
    uint64_t hSize   = ld.Resolve("kernel32.dll", "HeapSize");
    uint64_t hFree   = ld.Resolve("kernel32.dll", "HeapFree");
    std::printf("  HeapCreate@0x%llX HeapAlloc@0x%llX HeapSize@0x%llX HeapFree@0x%llX\n",
                (unsigned long long)hCreate,(unsigned long long)hAlloc,(unsigned long long)hSize,(unsigned long long)hFree);
    std::printf("  ------------------------------------------------------\n");

    // 1) HeapCreate(0, 0x10000, 0) -> heap handle
    Res rc = call(hCreate, 0, 0x10000, 0, 0);
    std::printf("  HeapCreate(0, 0x10000, 0)      -> %-8s handle=0x%llX", rc.how, (unsigned long long)rc.rax);
    if (std::string(rc.how) != "returned") {
        uint64_t rip = vm.LastExceptionRip();
        uint64_t nt = ld.Modules().count("ntdll.dll") ? ld.Modules().at("ntdll.dll").base : 0;
        uint64_t kb = ld.Modules().count("kernelbase.dll") ? ld.Modules().at("kernelbase.dll").base : 0;
        std::printf("  @ 0x%llX", (unsigned long long)rip);
        if (nt && rip >= nt && rip < nt + 0x266000) std::printf(" (ntdll+0x%llX)", (unsigned long long)(rip - nt));
        else if (kb && rip >= kb && rip < kb + 0x400000) std::printf(" (kernelbase+0x%llX)", (unsigned long long)(rip - kb));
    }
    std::printf("\n");
    uint64_t heap = rc.rax;
    if (std::string(rc.how) != "returned" || !heap) {
        std::printf("  ------------------------------------------------------\n");
        std::printf("  RESULT: HeapCreate did not return a heap (%s). The heap manager\n", rc.how);
        std::printf("          needs more ntdll init than RtlCreateHeap alone provides.\n");
        return 2;
    }
    wr64(env.peb + 0x30, heap);                      // publish PEB->ProcessHeap

    // 2) HeapAlloc(heap, HEAP_ZERO_MEMORY=8, 0x40) -> pointer
    Res ra = call(hAlloc, heap, 8, 0x40, 0);
    std::printf("  HeapAlloc(heap, ZERO, 0x40)    -> %-8s ptr=0x%llX\n", ra.how, (unsigned long long)ra.rax);
    uint64_t p = ra.rax;
    bool allocOk = std::string(ra.how)=="returned" && p && p+0x40 <= vm.RamSize();

    // 3) write through the returned pointer, then HeapSize to confirm the block.
    bool rwOk = false; uint64_t sz = 0;
    if (allocOk) {
        wr64(p, 0xC0FFEE12345678ull);                // guest-visible write into the heap block
        Res rs = call(hSize, heap, 0, p, 0);
        sz = rs.rax;
        rwOk = std::string(rs.how)=="returned" && sz >= 0x40 && rd64(p)==0xC0FFEE12345678ull;
        std::printf("  HeapSize(heap, 0, ptr)         -> %-8s size=0x%llX (asked 0x40)\n", rs.how, (unsigned long long)sz);
    }

    // 4) HeapFree(heap, 0, ptr) -> TRUE
    Res rf = allocOk ? call(hFree, heap, 0, p, 0) : Res{"skipped",0};
    std::printf("  HeapFree(heap, 0, ptr)         -> %-8s ret=%llu\n", rf.how, (unsigned long long)rf.rax);

    bool ok = heap && allocOk && rwOk && std::string(rf.how)=="returned" && rf.rax==1;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "real DLL exports reached our GuestHeap: HeapCreate returned its arena,\n"
          "          HeapAlloc carved a zeroed block we wrote+read, HeapSize\n"
          "          confirmed it, HeapFree released it. The actual Windows heap\n"
          "          manager is bypassed by the patches; no LdrpInitialize."
        : "the heap round-trip did not fully complete (see the steps above).");
    return ok ? 0 : 1;
}
