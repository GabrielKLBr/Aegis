// VmNtImage.cpp - Model B: image sections (SEC_IMAGE) with real PE layout.
//
// This is the loader's actual DLL-mapping mechanism. A ring-3 guest calls the
// REAL ntdll NtCreateFile -> NtCreateSection(SEC_IMAGE) -> NtMapViewOfSection
// stubs over a genuine relocatable DLL (imgtest.dll) sitting in the VirtualFs.
// MiniNt services NtMapViewOfSection by laying the PE out by SizeOfImage and
// per-section RVAs and applying DIR64 base relocations -- exactly like NT maps a
// DLL. The guest then CALLs the mapped image's exported Probe() and reads its
// return value.
//
// Probe() returns *g_ptr, where g_ptr is a global initialized to the absolute
// address of g_marker (== 0xC0FFEE5). That absolute address only resolves inside
// the mapped view if base relocations were applied. So a correct return value
// proves, in one shot: headers + sections placed at the right RVAs, executable
// code running from the image, and relocation fixups applied.
//
// Build target: vmntimage.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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

static std::vector<uint8_t> ReadAll(const wchar_t* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}
static uint32_t RvaToOff(const std::vector<uint8_t>& f, uint32_t rva) {
    auto dos = (const IMAGE_DOS_HEADER*)f.data();
    auto nth = (const IMAGE_NT_HEADERS*)(f.data() + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nth);
    for (unsigned i = 0; i < nth->FileHeader.NumberOfSections; ++i) {
        uint32_t va = sec[i].VirtualAddress;
        uint32_t vs = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (rva >= va && rva < va + vs) return sec[i].PointerToRawData + (rva - va);
    }
    return 0;
}
static uint32_t ExportRva(const std::vector<uint8_t>& f, const char* want) {
    auto dos = (const IMAGE_DOS_HEADER*)f.data();
    auto nth = (const IMAGE_NT_HEADERS*)(f.data() + dos->e_lfanew);
    auto d = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!d.VirtualAddress) return 0;
    auto exp   = (const IMAGE_EXPORT_DIRECTORY*)(f.data() + RvaToOff(f, d.VirtualAddress));
    auto names = (const uint32_t*)(f.data() + RvaToOff(f, exp->AddressOfNames));
    auto ords  = (const uint16_t*)(f.data() + RvaToOff(f, exp->AddressOfNameOrdinals));
    auto funcs = (const uint32_t*)(f.data() + RvaToOff(f, exp->AddressOfFunctions));
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* nm = (const char*)(f.data() + RvaToOff(f, names[i]));
        if (std::strcmp(nm, want) == 0) return funcs[ords[i]];
    }
    return 0;
}

int main() {
    std::printf("MicroVM Model B: image section (SEC_IMAGE + PE layout + relocations)\n");
    std::printf("  ------------------------------------------------------\n");

    // The project's trusted fixture, rather than a previous user's checkout.
#ifdef GUARDIAN_IMAGE_FIXTURE
    const auto dllPath = std::filesystem::path(GUARDIAN_IMAGE_FIXTURE).wstring();
#else
    const std::wstring dllPath = L"test/imgtest.dll";
#endif
    std::vector<uint8_t> dll = ReadAll(dllPath.c_str());
    if (dll.size() < 0x40) { std::printf("  [FAIL] cannot read imgtest.dll (build it first)\n"); return 1; }
    uint32_t probeRva = ExportRva(dll, "Probe");
    if (!probeRva) { std::printf("  [FAIL] Probe export not found\n"); return 1; }
    std::printf("  imgtest.dll: %zu bytes on disk, Probe at RVA 0x%X\n", dll.size(), probeRva);

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

    const std::wstring ntpath = L"\\??\\C:\\test\\imgtest.dll";

    // OBJECT_ATTRIBUTES + UNICODE_STRING for the open.
    wr32(kOA + 0x00, 0x30); wr64(kOA + 0x10, kUStr); wr32(kOA + 0x18, 0x40);
    uint16_t pb = (uint16_t)(ntpath.size() * 2);
    wr16(kUStr + 0, pb); wr16(kUStr + 2, (uint16_t)(pb + 2)); wr64(kUStr + 8, kPath);
    for (size_t i = 0; i < ntpath.size(); ++i) wr16(kPath + i*2, (uint16_t)ntpath[i]);
    // NtCreateFile stack args (disposition FILE_OPEN=1)
    wr64(kSf + 0x28, 0x80); wr64(kSf + 0x38, 1); wr64(kSf + 0x40, 0x40);
    // NtCreateSection stack args: Prot=PAGE_EXECUTE_READ(0x20), Attrs=SEC_IMAGE(0x1000000)
    wr64(kSs + 0x20, 0x20); wr64(kSs + 0x28, 0x1000000);
    // NtMapViewOfSection stack args: &ViewSize, Inherit=2, Protect=PAGE_EXECUTE_READ(0x20)
    wr64(kSm + 0x30, kView); wr64(kSm + 0x38, 2); wr64(kSm + 0x48, 0x20);

    // --- guest code ---
    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto call= [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto esp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto absLoad = [&](uint8_t modrmReg, uint32_t a){ e({0x48,0x8B,(uint8_t)(modrmReg|0x04),0x25}); e32(a); };
    auto absStore= [&](uint8_t modrmReg, uint32_t a){ e({0x48,0x89,(uint8_t)(modrmReg|0x04),0x25}); e32(a); };
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
    // CALL the mapped image's Probe(): rax = base + probeRva; call rax -> [kResult]
    esp((uint32_t)kSt);
    absLoad(0x00, (uint32_t)kBase);               // mov rax, [kBase]
    e({0x48,0x05}); e32(probeRva);                 // add rax, probeRva
    e({0xFF,0xD0});                                // call rax
    absStore(0x00, (uint32_t)kResult);            // mov [kResult], rax
    // NtTerminateProcess(-1, 0)
    esp((uint32_t)kSt);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2});
    call(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    // Drop the real DLL into the VirtualFs where the guest opens it.
    auto fr = kernel.Fs().Open("C:\\test\\imgtest.dll");
    kernel.Fs().Write(fr.handle, dll.data(), dll.size());
    kernel.Fs().Close(fr.handle);

    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    std::printf("  mapping imgtest.dll as an image section, then CALLing Probe()...\n");
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("%s", kernel.Report().Render().c_str());
    uint64_t got = 0; std::memcpy(&got, g + kResult, 8);
    const uint64_t want = 0xC0FFEE5ull;
    std::printf("  Probe() returned 0x%llX  (expected 0x%llX) %s\n",
                (unsigned long long)got, (unsigned long long)want,
                got == want ? "OK" : "MISMATCH");

    bool ok = kernel.ExitCode() == 0 && got == want;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "a real relocatable DLL was mapped as a SEC_IMAGE section (headers +\n"
          "          sections by RVA + DIR64 relocations); the guest executed its\n"
          "          exported Probe() and got the relocated value. Image sections work."
        : "the image-section layout did not produce the expected relocated value.");
    return ok ? 0 : 1;
}
