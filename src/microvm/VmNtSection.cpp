// VmNtSection.cpp - Model B: NT sections (the loader's core mechanism).
//
// A ring-3 guest calls the REAL ntdll NtCreateFile -> NtCreateSection ->
// NtMapViewOfSection stubs to map a file-backed section into memory, then reads
// the file's bytes straight out of the mapped view. MiniNt services them: the
// section is backed by a VirtualFs file and NtMapViewOfSection copies the file's
// content into a freshly allocated view. This is exactly how the Windows loader
// maps DLLs (image sections add PE layout on top).
//
// Build target: vmntsect.exe  (links WinHvPlatform).

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
static constexpr uint64_t kOA = 0x5100, kUStr = 0x5140, kPath = 0x5160;
static constexpr uint64_t kIosbC = 0x5200, kFH = 0x5210, kSH = 0x5218;
static constexpr uint64_t kBase = 0x5220, kView = 0x5228, kResult = 0x5230;
static constexpr uint64_t kSf = 0xEFF000, kSs = 0xEFE000, kSm = 0xEFD000, kSt = 0xEFC000;
static constexpr uint64_t kStack = 0xEFFFF8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam = 0x1000000;

int main() {
    std::printf("MicroVM Model B: NT sections (NtCreateSection/NtMapViewOfSection)\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr16 = [&](uint64_t a, uint16_t v){ CopyMemory(g+a,&v,2); };
    auto wr32 = [&](uint64_t a, uint32_t v){ CopyMemory(g+a,&v,4); };
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
    uint64_t aCreate = addr("NtCreateFile"), aSection = addr("NtCreateSection"),
             aMap = addr("NtMapViewOfSection"), aTerm = addr("NtTerminateProcess");

    // The "DLL"/data to be section-mapped, pre-populated in the VirtualFs.
    const std::string content = "MAPPED-VIA-SECTION-OBJECT (real ntdll stubs)";
    const std::wstring ntpath = L"\\??\\C:\\data\\src.bin";

    // OBJECT_ATTRIBUTES + UNICODE_STRING for the open.
    wr32(kOA + 0x00, 0x30); wr64(kOA + 0x10, kUStr); wr32(kOA + 0x18, 0x40);
    uint16_t pb = (uint16_t)(ntpath.size() * 2);
    wr16(kUStr + 0, pb); wr16(kUStr + 2, (uint16_t)(pb + 2)); wr64(kUStr + 8, kPath);
    for (size_t i = 0; i < ntpath.size(); ++i) wr16(kPath + i*2, (uint16_t)ntpath[i]);
    // NtCreateFile stack args (disposition FILE_OPEN=1)
    wr64(kSf + 0x28, 0x80); wr64(kSf + 0x38, 1); wr64(kSf + 0x40, 0x40);
    // NtCreateSection stack args: Prot=PAGE_READONLY(2), Attrs=SEC_COMMIT(0x8000000), FileHandle filled by guest
    wr64(kSs + 0x20, 2); wr64(kSs + 0x28, 0x8000000);
    // NtMapViewOfSection stack args: Commit=0, Off=0, &ViewSize, Inherit=2, AllocType=0, Protect=2
    wr64(kSm + 0x30, kView); wr64(kSm + 0x38, 2); wr64(kSm + 0x48, 2);

    // --- guest code ---
    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto call= [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto esp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto absLoad = [&](uint8_t modrmReg, uint32_t a){ e({0x48,0x8B,(uint8_t)(modrmReg|0x04),0x25}); e32(a); }; // mov r64,[abs]
    auto absStore= [&](uint8_t modrmReg, uint32_t a){ e({0x48,0x89,(uint8_t)(modrmReg|0x04),0x25}); e32(a); }; // mov [abs],r64
    // NtCreateFile(&FH, GENERIC_READ, &OA, &IOSB) -> [kFH]
    esp((uint32_t)kSf);
    e({0xB9}); e32((uint32_t)kFH);
    e({0xBA}); e32(0x80000000);
    e({0x41,0xB8}); e32((uint32_t)kOA);
    e({0x41,0xB9}); e32((uint32_t)kIosbC);
    call(aCreate);
    // fileHandle -> NtCreateSection arg7 slot
    absLoad(0x00, (uint32_t)kFH);                 // mov rax, [kFH]
    absStore(0x00, (uint32_t)(kSs + 0x30));       // mov [kSs+0x30], rax
    // NtCreateSection(&SH, SECTION_ALL, 0, 0, ...) -> [kSH]
    esp((uint32_t)kSs);
    e({0xB9}); e32((uint32_t)kSH);
    e({0xBA}); e32(0x000F001F);
    e({0x45,0x31,0xC0}); e({0x45,0x31,0xC9});      // xor r8d; xor r9d
    call(aSection);
    // NtMapViewOfSection(SH, -1, &base, 0, ...) -> [kBase]
    esp((uint32_t)kSm);
    absLoad(0x08, (uint32_t)kSH);                 // mov rcx, [kSH]
    e({0x48,0xC7,0xC2,0xFF,0xFF,0xFF,0xFF});       // mov rdx, -1
    e({0x41,0xB8}); e32((uint32_t)kBase);          // mov r8d, &base
    e({0x45,0x31,0xC9});                           // xor r9d
    call(aMap);
    // read mapped[0..8] -> [kResult]
    absLoad(0x18, (uint32_t)kBase);               // mov rbx, [kBase]
    e({0x48,0x8B,0x03});                           // mov rax, [rbx]
    absStore(0x00, (uint32_t)kResult);            // mov [kResult], rax
    // NtTerminateProcess(-1, 0)
    esp((uint32_t)kSt);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2});
    call(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    // Pre-populate the file that the section maps.
    auto fr = kernel.Fs().Open("C:\\data\\src.bin");
    kernel.Fs().Write(fr.handle, reinterpret_cast<const uint8_t*>(content.data()), content.size());
    kernel.Fs().Close(fr.handle);

    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    std::printf("  mapping a file-backed section via real ntdll stubs...\n");
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("%s", kernel.Report().Render().c_str());
    uint64_t got; std::memcpy(&got, g + kResult, 8);
    uint64_t want = 0; std::memcpy(&want, content.data(), 8);
    char gotc[9] = {0}; std::memcpy(gotc, &got, 8);
    std::printf("  guest read from the mapped view: \"%s...\"  (expected \"%.8s...\") %s\n",
                gotc, content.c_str(), got == want ? "OK" : "MISMATCH");

    bool ok = kernel.ExitCode() == 0 && got == want;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "a file-backed NT section was created and mapped via real ntdll stubs;\n"
          "          the guest read the file's bytes from the mapped view. Sections work."
        : "the section/map path did not complete as expected.");
    return ok ? 0 : 1;
}
