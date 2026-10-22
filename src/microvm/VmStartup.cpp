// VmStartup.cpp - CRT startup surface + TLS.
//
// A ring-3 guest calls the functions a C runtime hits before main():
//   GetCurrentProcessId/ThreadId, GetCommandLineW, GetModuleHandleW(NULL) and
//   by name, GetSystemTimeAsFileTime, QueryPerformanceCounter, and TLS
//   (TlsAlloc/TlsSetValue/TlsGetValue). The synthetic NtKernel services them
//   against the TEB/PEB/Ldr we built; results are checked by the host.
//
// Build target: vmstartup.exe  (links WinHvPlatform).

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
static constexpr uint64_t kNdName    = 0x5200;       // L"ntdll.dll"
static constexpr uint64_t kEnvBase   = 0x300000;
static constexpr uint64_t kLdrBase   = 0x310000;
static constexpr uint64_t kStack     = 0xF00000;
static constexpr uint64_t kImageBase = 0x140000000ull;
static constexpr uint64_t kNtdllBase = 0x180000000ull;
static constexpr uint32_t kPid = 0x1A4, kTid = 0x1A8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam       = 0x1000000;

int main() {
    std::printf("MicroVM CRT startup surface + TLS\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls (try 'av vmcheck')\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    const unsigned char code[] = {
        0xB8,0x0F,0,0,0, 0x0F,0x05, 0x48,0x89,0x04,0x25,0x00,0x50,0,0,   // pid     -> [0x5000]
        0xB8,0x10,0,0,0, 0x0F,0x05, 0x48,0x89,0x04,0x25,0x08,0x50,0,0,   // tid     -> [0x5008]
        0xB8,0x12,0,0,0, 0x0F,0x05, 0x48,0x89,0x04,0x25,0x10,0x50,0,0,   // cmdline -> [0x5010]
        0x31,0xC9, 0xB8,0x13,0,0,0, 0x0F,0x05, 0x48,0x89,0x04,0x25,0x18,0x50,0,0, // GetModuleHandleW(NULL) -> [0x5018]
        0xB8,0x15,0,0,0, 0x0F,0x05, 0x48,0x89,0xC3, 0x48,0x89,0x1C,0x25,0x20,0x50,0,0, // TlsAlloc -> rbx,[0x5020]
        0x48,0x89,0xD9, 0xBA,0xEE,0xFF,0xC0,0x00, 0xB8,0x16,0,0,0, 0x0F,0x05,         // TlsSetValue(rbx,0xC0FFEE)
        0x48,0x89,0xD9, 0xB8,0x17,0,0,0, 0x0F,0x05, 0x48,0x89,0x04,0x25,0x28,0x50,0,0, // TlsGetValue -> [0x5028]
        0xB9,0x00,0x51,0,0, 0xB8,0x0D,0,0,0, 0x0F,0x05,                  // GetSystemTimeAsFileTime(0x5100)
        0xB9,0x08,0x51,0,0, 0xB8,0x0E,0,0,0, 0x0F,0x05,                  // QueryPerformanceCounter(0x5108)
        0xB9,0x00,0x52,0,0, 0xB8,0x13,0,0,0, 0x0F,0x05, 0x48,0x89,0x04,0x25,0x30,0x50,0,0, // GetModuleHandleW(L"ntdll.dll") -> [0x5030]
        0x31,0xC9, 0xB8,0x05,0,0,0, 0x0F,0x05, 0xF4,                     // ExitProcess(0) ; hlt
    };
    CopyMemory(g + kEntry, code, sizeof(code));

    // L"ntdll.dll" at kNdName for the by-name lookup.
    const wchar_t* nd = L"ntdll.dll";
    for (int i = 0; nd[i]; ++i) { uint16_t c = (uint16_t)nd[i]; CopyMemory(g + kNdName + i * 2, &c, 2); }

    ProcessEnv env = BuildProcessEnv(vm, kEnvBase, kImageBase, kStack, 0xE00000,
                                     kPid, kTid, L"C:\\sample\\malware.exe");
    std::vector<ModuleSpec> mods = {
        {L"sample.exe", kImageBase, 0x10000}, {L"ntdll.dll", kNtdllBase, 0x200000},
        {L"ntkernel.dll", 0x190000000ull, 0x80000},
    };
    BuildLoaderData(vm, env, kLdrBase, mods);

    NtKernel kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    if (!vm.SetGsBase(env.teb)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    std::printf("  ------------------------------------------------------\n");
    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    auto rd64 = [&](uint64_t a){ uint64_t v; std::memcpy(&v, g + a, 8); return v; };
    uint64_t pid = rd64(kResults+0x00), tid = rd64(kResults+0x08), clPtr = rd64(kResults+0x10);
    uint64_t modSelf = rd64(kResults+0x18), tlsIdx = rd64(kResults+0x20), tlsVal = rd64(kResults+0x28);
    uint64_t ft = rd64(0x5100), qpc = rd64(0x5108), modNtdll = rd64(kResults+0x30);

    std::string cl;
    for (uint64_t i = clPtr; ; i += 2) { uint16_t c; std::memcpy(&c, g+i, 2); if (!c) break; cl.push_back((char)c); }

    auto chk = [](bool b){ return b ? "OK" : "MISMATCH"; };
    std::printf("  GetCurrentProcessId   = 0x%llX            %s\n", (unsigned long long)pid, chk(pid==kPid));
    std::printf("  GetCurrentThreadId    = 0x%llX            %s\n", (unsigned long long)tid, chk(tid==kTid));
    std::printf("  GetCommandLineW       = \"%s\" %s\n", cl.c_str(), chk(cl=="C:\\sample\\malware.exe"));
    std::printf("  GetModuleHandleW(0)   = 0x%llX     %s\n", (unsigned long long)modSelf, chk(modSelf==kImageBase));
    std::printf("  GetModuleHandleW(ntdll)= 0x%llX     %s\n", (unsigned long long)modNtdll, chk(modNtdll==kNtdllBase));
    std::printf("  TlsAlloc/Set/GetValue = idx %llu -> 0x%llX  %s\n",
                (unsigned long long)tlsIdx, (unsigned long long)tlsVal, chk(tlsVal==0xC0FFEE));
    std::printf("  GetSystemTimeAsFileTime= 0x%llX  %s\n", (unsigned long long)ft, chk(ft!=0));
    std::printf("  QueryPerformanceCounter= 0x%llX        %s\n", (unsigned long long)qpc, chk(qpc!=0));

    bool ok = pid==kPid && tid==kTid && cl=="C:\\sample\\malware.exe" &&
              modSelf==kImageBase && modNtdll==kNtdllBase && tlsVal==0xC0FFEE && ft && qpc;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the CRT startup surface answers: ids, command line, module handles,\n"
          "          time, and TLS all work. Real CRT init is within reach."
        : "one or more startup calls returned the wrong thing.");
    return ok ? 0 : 1;
}
