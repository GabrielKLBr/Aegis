// VmRealNtdll.cpp - Model B: load the REAL ntdll.dll and run its real syscall
// stubs inside the MicroVM, serviced by the mini NT kernel (real SSNs).
//
//   * PeLoader maps the real C:\Windows\System32\ntdll.dll into the guest.
//   * KUSER_SHARED_DATA is mapped at 0x7FFE0000 (real stubs read [0x7FFE0308]).
//   * a ring-3 guest CALLs the real ntdll NtAllocateVirtualMemory stub (which
//     does mov r10,rcx; mov eax,<SSN>; syscall) and NtTerminateProcess.
//   * MiniNt dispatches by the real SSN and implements the semantics.
//
// Build target: vmntdll.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry     = 0x2000;
static constexpr uint64_t kNtdllBase = 0x400000;     // load the real ntdll here
static constexpr uint64_t kKusdVa    = 0x7FE00000;   // 2 MiB page covering 0x7FFE0000
static constexpr uint64_t kKusdBack  = 0x600000;     // backing GPA in guest RAM
static constexpr uint64_t kBaseSlot  = 0x5000;       // PVOID BaseAddress out
static constexpr uint64_t kSizeSlot  = 0x5008;       // SIZE_T RegionSize in/out
static constexpr uint64_t kStatSlot  = 0x5010;       // NTSTATUS result
static constexpr uint64_t kStack     = 0xEFFFF8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam       = 0x1000000;

int main() {
    std::printf("MicroVM Model B: run the REAL ntdll.dll in a mini NT kernel\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls (try 'av vmcheck')\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    // Map the real ntdll (it has no imports; resolver is never called).
    PeLoader loader; PeImage img;
    ImportResolver none = [](const std::string&, const std::string&) -> uint64_t { return 0; };
    if (!loader.Load(vm, L"C:\\Windows\\System32\\ntdll.dll", kNtdllBase, none, img)) {
        std::printf("  [FAIL] load ntdll: %ls\n", loader.Error().c_str()); return 1;
    }
    std::printf("  [load] real ntdll.dll mapped at 0x%llX (size 0x%llX, reloc=%s)\n",
                (unsigned long long)img.imageBase, (unsigned long long)img.sizeOfImage,
                img.relocated ? "yes" : "no");

    // KUSER_SHARED_DATA at 0x7FFE0000 so real stubs' `test [7FFE0308],1` works.
    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);   // SystemCall bit (0x308) = 0 -> syscall path

    // Resolve the two real stubs (RVAs match between host and guest copies).
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    auto rvaOf = [&](const char* n){ return (uint64_t)((uint8_t*)GetProcAddress(hn, n) - (uint8_t*)hn); };
    uint64_t allocAddr = kNtdllBase + rvaOf("NtAllocateVirtualMemory");
    uint64_t termAddr  = kNtdllBase + rvaOf("NtTerminateProcess");
    std::printf("  [ntdll] NtAllocateVirtualMemory -> guest 0x%llX\n", (unsigned long long)allocAddr);

    // Build the ring-3 guest that calls the two real stubs.
    std::vector<uint8_t> code; size_t pAlloc = 0, pTerm = 0;
    auto emit  = [&](std::initializer_list<uint8_t> b){ for (auto x : b) code.push_back(x); };
    auto emit32= [&](uint32_t v){ for (int i = 0; i < 4; ++i) code.push_back((v >> (8*i)) & 0xFF); };
    emit({0x48,0x83,0xEC,0x48});                              // sub rsp, 0x48
    emit({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF});               // mov rcx, -1 (current process)
    emit({0xBA}); emit32(kBaseSlot);                         // mov edx, &BaseAddress
    emit({0x45,0x31,0xC0});                                   // xor r8d, r8d (ZeroBits)
    emit({0x41,0xB9}); emit32(kSizeSlot);                    // mov r9d, &RegionSize
    emit({0xC7,0x44,0x24,0x20,0x00,0x30,0x00,0x00});          // mov dword [rsp+0x20], 0x3000 (MEM_COMMIT|RESERVE)
    emit({0xC7,0x44,0x24,0x28,0x04,0x00,0x00,0x00});          // mov dword [rsp+0x28], 0x04 (PAGE_READWRITE)
    emit({0xB8}); pAlloc = code.size(); emit32(0);            // mov eax, <NtAllocateVirtualMemory>
    emit({0xFF,0xD0});                                        // call rax
    emit({0x48,0x89,0x04,0x25}); emit32(kStatSlot);           // mov [kStatSlot], rax (NTSTATUS)
    emit({0x48,0x8B,0x1C,0x25}); emit32(kBaseSlot);           // mov rbx, [kBaseSlot]
    emit({0xC7,0x03,0xEF,0xCD,0xAB,0x00});                    // mov dword [rbx], 0x00ABCDEF
    emit({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF});               // mov rcx, -1
    emit({0x31,0xD2});                                        // xor edx, edx (ExitStatus 0)
    emit({0xB8}); pTerm = code.size(); emit32(0);             // mov eax, <NtTerminateProcess>
    emit({0xFF,0xD0});                                        // call rax
    emit({0xF4});                                             // hlt (safety)
    auto patch = [&](size_t o, uint32_t v){ for (int i = 0; i < 4; ++i) code[o+i] = (v >> (8*i)) & 0xFF; };
    patch(pAlloc, (uint32_t)allocAddr);
    patch(pTerm,  (uint32_t)termAddr);
    CopyMemory(g + kEntry, code.data(), code.size());

    // Pre-init the in/out params: BaseAddress=0, RegionSize=0x1000.
    uint64_t zero = 0, sz = 0x1000;
    CopyMemory(g + kBaseSlot, &zero, 8);
    CopyMemory(g + kSizeSlot, &sz, 8);

    MiniNt kernel;
    std::printf("  [ssn ] NtAllocateVirtualMemory = 0x%X, NtTerminateProcess = 0x%X\n",
                kernel.SsnOf("NtAllocateVirtualMemory"), kernel.SsnOf("NtTerminateProcess"));
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);   // re-assert after SetupLongMode set up the tables
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    auto rd64 = [&](uint64_t a){ uint64_t v; std::memcpy(&v, g + a, 8); return v; };
    auto rd32 = [&](uint64_t a){ uint32_t v; std::memcpy(&v, g + a, 4); return v; };
    uint64_t allocated = rd64(kBaseSlot), status = rd64(kStatSlot);

    std::printf("%s", kernel.Report().Render().c_str());
    std::printf("  real NtAllocateVirtualMemory returned STATUS 0x%llX, base 0x%llX\n",
                (unsigned long long)status, (unsigned long long)allocated);
    bool inArena = allocated >= 0xA00000 && allocated < 0xE00000;
    uint32_t marker = inArena ? rd32(allocated) : 0;
    std::printf("  guest wrote 0x%08X to the allocation (read back 0x%08X) %s\n",
                0x00ABCDEF, marker, marker == 0x00ABCDEF ? "OK" : "MISMATCH");

    bool ok = status == 0 && inArena && marker == 0x00ABCDEF && kernel.ExitCode() == 0;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "real ntdll.dll stubs executed inside the guest and were serviced by\n"
          "          our mini NT kernel via the real SSNs. Model B works."
        : "the real-ntdll path did not complete as expected.");
    return ok ? 0 : 1;
}
