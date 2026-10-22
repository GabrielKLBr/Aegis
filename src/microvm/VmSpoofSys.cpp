// VmSpoofSys.cpp - anti-VM: uptime + process list the way a sandbox-aware sample sees them.
//
// Two more evasion checks, beyond CPUID and the registry/disk personality:
//   * Uptime. A box that booted seconds ago is a sandbox (analysts spin a fresh
//     VM per sample). GetTickCount64 just reads KUSER_SHARED_DATA, which we seed
//     to ~3 days (MiniNt::SeedSharedData). This guest reads it the same way.
//   * Process list. A sandbox runs a handful of processes, and malware scans the
//     list for analysis tools (vmtoolsd, vboxservice, wireshark, procmon, x64dbg).
//     MiniNt answers NtQuerySystemInformation(SystemProcessInformation) with a
//     believable ~46-process desktop and none of those tools.
//
// Build target: vmspoofsys.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry = 0x2000, kNtdllBase = 0x400000;
static constexpr uint64_t kKusdVa = 0x7FE00000, kKusdBack = 0x600000;
static constexpr uint64_t kStack = 0xEFFFF8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam = 0x1000000;
// scratch + process-list output buffer
static constexpr uint64_t kUpTick=0x8000, kUpInt=0x8008, kStatus=0x8010, kRet=0x8018;
static constexpr uint64_t kBuf=0x20000, kLen=0x8000, kStk=0xEFF000;

int main() {
    std::printf("MicroVM anti-VM: uptime + process list a sandbox-aware sample inspects\n");
    std::printf("  ------------------------------------------------------------------------\n");

    MicroVm vm;
    vm.SpoofCpuid("GenuineIntel", "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz", 8);
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    PeLoader loader; PeImage img;
    ImportResolver none = [](const std::string&, const std::string&) -> uint64_t { return 0; };
    if (!loader.Load(vm, L"C:\\Windows\\System32\\ntdll.dll", kNtdllBase, none, img)) {
        std::printf("  [FAIL] load ntdll: %ls\n", loader.Error().c_str()); return 1;
    }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);

    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    auto addr = [&](const char* n){ return kNtdllBase + (uint64_t)((uint8_t*)GetProcAddress(hn,n) - (uint8_t*)hn); };
    uint64_t aQuery = addr("NtQuerySystemInformation"), aTerm = addr("NtTerminateProcess");

    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto call= [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto esp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto movEcx  = [&](uint32_t v){ e({0xB9}); e32(v); };
    auto movEdx  = [&](uint32_t v){ e({0xBA}); e32(v); };
    auto movR8d  = [&](uint32_t v){ e({0x41,0xB8}); e32(v); };
    auto movR9d  = [&](uint32_t v){ e({0x41,0xB9}); e32(v); };
    auto ld64    = [&](uint8_t reg, uint32_t a){ e({0x48,0x8B,(uint8_t)((reg<<3)|0x04),0x25}); e32(a); }; // mov reg,[abs]
    auto st64    = [&](uint8_t reg, uint32_t a){ e({0x48,0x89,(uint8_t)((reg<<3)|0x04),0x25}); e32(a); }; // mov [abs],reg
    auto st32    = [&](uint8_t reg, uint32_t a){ e({0x89,(uint8_t)((reg<<3)|0x04),0x25}); e32(a); };       // mov [abs],reg32

    // uptime: rax=[KUSER+0x320] (TickCountQuad), store; rax=[KUSER+0x008] (InterruptTime)
    ld64(0, (uint32_t)(0x7FFE0000 + 0x320)); st64(0, (uint32_t)kUpTick);
    ld64(0, (uint32_t)(0x7FFE0000 + 0x008)); st64(0, (uint32_t)kUpInt);
    // NtQuerySystemInformation(SystemProcessInformation=5, kBuf, kLen, &kRet)
    esp((uint32_t)kStk); movEcx(5); movEdx((uint32_t)kBuf); movR8d((uint32_t)kLen); movR9d((uint32_t)kRet);
    call(aQuery); st32(0, (uint32_t)kStatus);
    // NtTerminateProcess(-1, 0)
    esp((uint32_t)kStk);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2}); call(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    kernel.SeedSharedData(vm, kKusdBack);     // seed believable uptime into KUSER
    std::printf("  reading uptime from KUSER_SHARED_DATA + walking the process list...\n");
    std::printf("  ------------------------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    // --- uptime ---
    uint64_t tickQuad, intTime; std::memcpy(&tickQuad, g+kUpTick, 8); std::memcpy(&intTime, g+kUpInt, 8);
    // GetTickCount64 == (TickCountQuad * TickCountMultiplier) >> 24, multiplier 0x0FA00000.
    uint64_t upMs = (tickQuad * 0x0FA00000ull) >> 24;
    uint64_t upH = upMs / 3600000, upM = (upMs % 3600000) / 60000;
    bool upOk = upMs > 3600000;   // > 1 hour: not a freshly-booted sandbox

    // --- process list ---
    uint32_t status; std::memcpy(&status, g+kStatus, 4);
    static const char* tools[] = { "vmtoolsd","vboxservice","vboxtray","vmwaretray","vmwareuser",
        "wireshark","dumpcap","procmon","procexp","processhacker","x64dbg","x32dbg","ollydbg",
        "idaq","ida64","dnspy","fiddler","autoruns","regmon","filemon","apatedns","windbg" };
    int count = 0; bool toolFound = false; std::string foundName;
    if (status == 0) {
        uint64_t p = kBuf;
        for (int guard = 0; guard < 512; ++guard) {
            uint32_t next; std::memcpy(&next, g+p+0x00, 4);
            uint16_t nlen; std::memcpy(&nlen, g+p+0x38, 2);
            uint64_t nbuf; std::memcpy(&nbuf, g+p+0x40, 8);
            std::string nm;
            for (uint16_t k = 0; k+1 < nlen && nbuf; k += 2) nm.push_back((char)g[nbuf+k]);
            std::string low; for (char ch : nm) low.push_back((char)tolower((unsigned char)ch));
            for (auto* t : tools) if (low.find(t) != std::string::npos) { toolFound = true; foundName = nm; }
            ++count;
            if (!next) break;
            p += next;
        }
    }
    bool procOk = (status == 0) && count >= 30 && !toolFound;

    std::printf("  uptime (GetTickCount64 view)   : %lluh %llum  %s\n",
                (unsigned long long)upH, (unsigned long long)upM,
                upOk ? "(lived-in, not a fresh sandbox)" : "(too low - sandbox tell)");
    std::printf("  process count                  : %d  %s\n", count,
                (count >= 30) ? "(busy desktop)" : "(too few - sandbox tell)");
    std::printf("  analysis tools in process list : %s\n",
                toolFound ? ("YES -> " + foundName).c_str() : "none");

    bool ok = kernel.ExitCode() == 0 && upOk && procOk;
    std::printf("  ------------------------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the box has days of uptime and a busy, tool-free process list -- both\n"
          "          read straight from KUSER_SHARED_DATA and NtQuerySystemInformation,\n"
          "          the way malware reads them. Neither timing nor process inspection\n"
          "          betrays a VM; a sandbox-aware sample would detonate."
        : "uptime or the process list still looks like a sandbox (see above).");
    return ok ? 0 : 1;
}
