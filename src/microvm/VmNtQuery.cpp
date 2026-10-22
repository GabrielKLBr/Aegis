// VmNtQuery.cpp - Model B: the queries + sync the loader hits before anything.
//
// Before LdrpInitialize maps a single DLL it asks the kernel who it is and what
// the machine looks like, and it touches sync objects. This exercises that
// surface with the REAL ntdll stubs:
//   NtQueryInformationProcess(ProcessBasicInformation) -> PEB base (via GS/TEB)
//   NtQuerySystemInformation(SystemBasicInformation)   -> 8 CPUs (anti-VM persona)
//   NtCreateEvent / NtSetEvent / NtWaitForSingleObject -> a satisfied wait
// MiniNt services them all; the guest reads back the PEB pointer it set up, the
// processor count, and a STATUS_SUCCESS wait.
//
// Build target: vmntquery.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry = 0x2000, kNtdllBase = 0x400000;
static constexpr uint64_t kKusdVa = 0x7FE00000, kKusdBack = 0x600000;
static constexpr uint64_t kTeb = 0x4000, kPeb = 0x4800;
static constexpr uint64_t kPbi = 0x5300, kSbi = 0x5340, kEvtH = 0x5390;
static constexpr uint64_t kRPeb = 0x53A0, kRCpu = 0x53A8, kRWait = 0x53B0;
static constexpr uint64_t kSq = 0xEFF000, kSe = 0xEFE000, kSc = 0xEFD000, kSt = 0xEFC000;
static constexpr uint64_t kStack = 0xEFFFF8;
static constexpr uint64_t kPebMarker = 0xCAFEBABEull;   // PEB.ImageBaseAddress sentinel
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam = 0x1000000;

int main() {
    std::printf("MicroVM Model B: loader queries + sync (NtQuery*/events)\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr64 = [&](uint64_t a, uint64_t v){ CopyMemory(g+a,&v,8); };

    PeLoader loader; PeImage img;
    ImportResolver none = [](const std::string&, const std::string&) -> uint64_t { return 0; };
    if (!loader.Load(vm, L"C:\\Windows\\System32\\ntdll.dll", kNtdllBase, none, img)) {
        std::printf("  [FAIL] load ntdll: %ls\n", loader.Error().c_str()); return 1;
    }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);

    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    auto addr = [&](const char* n){ return kNtdllBase + (uint64_t)((uint8_t*)GetProcAddress(hn,n) - (uint8_t*)hn); };
    uint64_t aQIP = addr("NtQueryInformationProcess"), aQSI = addr("NtQuerySystemInformation"),
             aCE  = addr("NtCreateEvent"), aSE = addr("NtSetEvent"),
             aWait = addr("NtWaitForSingleObject"), aTerm = addr("NtTerminateProcess");

    // Minimal TEB/PEB so NtQueryInformationProcess can return a real PEB base.
    wr64(kTeb + 0x60, kPeb);          // TEB.ProcessEnvironmentBlock
    wr64(kPeb + 0x10, kPebMarker);    // PEB.ImageBaseAddress (sentinel)

    // Pre-filled stack-arg blocks (StackArg(5) reads [rsp+0x28] after the call).
    wr64(kSq + 0x20, 0);              // NtQueryInformationProcess ReturnLength = NULL
    wr64(kSe + 0x20, 0);              // NtCreateEvent InitialState = FALSE

    // --- guest code ---
    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto call= [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto esp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto absLoad = [&](uint8_t modrmReg, uint32_t a){ e({0x48,0x8B,(uint8_t)(modrmReg|0x04),0x25}); e32(a); };
    auto absStore= [&](uint8_t modrmReg, uint32_t a){ e({0x48,0x89,(uint8_t)(modrmReg|0x04),0x25}); e32(a); };

    // 1) NtQueryInformationProcess(-1, ProcessBasicInformation=0, &PBI, 0x30, NULL)
    esp((uint32_t)kSq);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF});       // mov rcx, -1
    e({0x31,0xD2});                                // xor edx, edx  (class 0)
    e({0x41,0xB8}); e32((uint32_t)kPbi);           // mov r8d, &PBI
    e({0x41,0xB9}); e32(0x30);                      // mov r9d, 0x30
    call(aQIP);
    absLoad(0x00, (uint32_t)(kPbi + 0x08));        // mov rax, [PBI.PebBaseAddress]
    absStore(0x00, (uint32_t)kRPeb);               // -> [kRPeb]

    // 2) NtQuerySystemInformation(SystemBasicInformation=0, &SBI, 0x40, NULL)
    esp((uint32_t)kSc);
    e({0x31,0xC9});                                // xor ecx, ecx  (class 0)
    e({0xBA}); e32((uint32_t)kSbi);                // mov edx, &SBI
    e({0x41,0xB8}); e32(0x40);                      // mov r8d, 0x40
    e({0x45,0x31,0xC9});                           // xor r9d, r9d  (RetLen NULL)
    call(aQSI);
    absLoad(0x00, (uint32_t)(kSbi + 0x38));        // mov rax, [SBI.NumberOfProcessors..]
    absStore(0x00, (uint32_t)kRCpu);               // -> [kRCpu]

    // 3) NtCreateEvent(&h, EVENT_ALL_ACCESS, NULL, NotificationEvent=0, FALSE)
    esp((uint32_t)kSe);
    e({0xB9}); e32((uint32_t)kEvtH);               // mov ecx, &h
    e({0xBA}); e32(0x1F0003);                       // mov edx, EVENT_ALL_ACCESS
    e({0x45,0x31,0xC0}); e({0x45,0x31,0xC9});      // xor r8d; xor r9d
    call(aCE);

    // 4) NtSetEvent(h, NULL)
    esp((uint32_t)kSc);
    absLoad(0x08, (uint32_t)kEvtH);                // mov rcx, [h]
    e({0x31,0xD2});                                // xor edx, edx
    call(aSE);

    // 5) NtWaitForSingleObject(h, FALSE, NULL) -> rax = STATUS
    esp((uint32_t)kSc);
    absLoad(0x08, (uint32_t)kEvtH);                // mov rcx, [h]
    e({0x31,0xD2});                                // xor edx, edx (Alertable=FALSE)
    e({0x45,0x31,0xC0});                           // xor r8d (Timeout=NULL)
    call(aWait);
    absStore(0x00, (uint32_t)kRWait);              // mov [kRWait], rax (status)

    // 6) NtTerminateProcess(-1, 0)
    esp((uint32_t)kSt);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2});
    call(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.SetGsBase(kTeb);
    vm.MapPage2MB(kKusdVa, kKusdBack);
    std::printf("  running loader-style NtQuery* + event sync via real ntdll stubs...\n");
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("%s", kernel.Report().Render().c_str());
    uint64_t pebGot = 0, cpuGot = 0, waitGot = 0;
    std::memcpy(&pebGot,  g + kRPeb,  8);
    std::memcpy(&cpuGot,  g + kRCpu,  8);
    std::memcpy(&waitGot, g + kRWait, 8);
    uint8_t numCpu = (uint8_t)(cpuGot & 0xFF);

    bool pebOk  = (pebGot == kPeb);
    bool cpuOk  = (numCpu == 8);   // anti-VM persona: a real desktop, not a 1-CPU sandbox
    bool waitOk = (waitGot == 0);
    std::printf("  NtQueryInformationProcess -> PEB 0x%llX (set up 0x%llX) %s\n",
                (unsigned long long)pebGot, (unsigned long long)kPeb, pebOk ? "OK" : "MISMATCH");
    std::printf("  NtQuerySystemInformation  -> %u processor(s) %s\n", numCpu, cpuOk ? "OK" : "BAD");
    std::printf("  NtWaitForSingleObject     -> status 0x%llX %s\n",
                (unsigned long long)waitGot, waitOk ? "OK (satisfied)" : "BAD");

    bool ok = kernel.ExitCode() == 0 && pebOk && cpuOk && waitOk;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the loader-facing query + sync surface answered through real ntdll\n"
          "          stubs: the guest got its PEB, the machine shape, and a\n"
          "          satisfied event wait. The pre-LdrpInitialize probes work."
        : "the query/sync surface did not answer as expected.");
    return ok ? 0 : 1;
}
