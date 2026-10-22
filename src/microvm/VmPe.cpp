// VmPe.cpp - load and run a real PE file through the NT personality.
//
// Step 1: emit a tiny, CRT-free 64-bit PE to disk (nt_hello.exe) that imports
//         WriteFile + ExitProcess from "ntkernel.dll" and calls them.
// Step 2: load that .exe with PeLoader -- it maps sections, resolves the imports
//         to host stubs, and hands back the entry point.
// Step 3: run it in the MicroVM. The PE's WriteFile/ExitProcess calls hit our
//         synthetic kernel, which prints the message and records the exit code.
//
// This is the milestone the user asked for: feed a .exe, watch what it does --
// with the kernel side entirely ours.
//
// Build target: vmpe.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"

#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <filesystem>

using namespace av::microvm;

// ---- guest-physical layout ----
static constexpr uint64_t kLoadGpa = 0x200000;   // == the PE's ImageBase (delta 0)
static constexpr uint64_t kStubGpa = 0x100000;   // host stub arena
static constexpr uint64_t kStack   = 0xF00000;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);  // #UD | #GP
static constexpr size_t   kRam     = 0x1000000;  // 16 MiB

enum : uint64_t { SYS_WriteFile = 1, SYS_ExitProcess = 2 };

static uint64_t Reg(MicroVm& vm, WHV_REGISTER_NAME n) {
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Reg64;
}

// Emit a minimal CRT-free PE32+ importing WriteFile + ExitProcess.
static bool EmitTestPe(const std::wstring& path, const std::string& msg) {
    std::vector<uint8_t> f(0x600, 0);
    auto w16 = [&](size_t o, uint16_t v){ f[o]=v&0xFF; f[o+1]=(v>>8)&0xFF; };
    auto w32 = [&](size_t o, uint32_t v){ for(int i=0;i<4;i++) f[o+i]=(v>>(8*i))&0xFF; };
    auto w64 = [&](size_t o, uint64_t v){ for(int i=0;i<8;i++) f[o+i]=(uint8_t)(v>>(8*i)); };
    auto str = [&](size_t o, const char* s){ for(size_t i=0;s[i];++i) f[o+i]=(uint8_t)s[i]; };

    // DOS header
    w16(0x00, 0x5A4D); w32(0x3C, 0x40);
    // PE signature + FILE_HEADER
    w32(0x40, 0x00004550);
    w16(0x44, 0x8664);  // Machine = AMD64
    w16(0x46, 2);       // NumberOfSections
    w16(0x54, 0xF0);    // SizeOfOptionalHeader
    w16(0x56, 0x0022);  // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
    // OPTIONAL_HEADER64 (base 0x58)
    w16(0x58, 0x020B);  // PE32+
    w32(0x5C, 0x200);   // SizeOfCode
    w32(0x60, 0x200);   // SizeOfInitializedData
    w32(0x68, 0x1000);  // AddressOfEntryPoint
    w32(0x6C, 0x1000);  // BaseOfCode
    w64(0x70, 0x200000);// ImageBase
    w32(0x78, 0x1000);  // SectionAlignment
    w32(0x7C, 0x200);   // FileAlignment
    w16(0x80, 6);       // MajorOperatingSystemVersion
    w16(0x88, 6);       // MajorSubsystemVersion
    w32(0x90, 0x3000);  // SizeOfImage
    w32(0x94, 0x200);   // SizeOfHeaders
    w16(0x9C, 3);       // Subsystem = CONSOLE
    w64(0xA0, 0x100000);// SizeOfStackReserve
    w64(0xA8, 0x1000);  // SizeOfStackCommit
    w64(0xB0, 0x100000);// SizeOfHeapReserve
    w64(0xB8, 0x1000);  // SizeOfHeapCommit
    w32(0xC4, 16);      // NumberOfRvaAndSizes
    w32(0xD0, 0x2000); w32(0xD4, 40);  // Import directory: VA, Size
    // Section headers
    str(0x148, ".text");
    w32(0x148+8, 0x200); w32(0x148+12, 0x1000); w32(0x148+16, 0x200);
    w32(0x148+20, 0x200); w32(0x148+36, 0x60000020);
    str(0x170, ".idata");
    w32(0x170+8, 0x200); w32(0x170+12, 0x2000); w32(0x170+16, 0x200);
    w32(0x170+20, 0x400); w32(0x170+36, 0xC0000040);

    // .text (file 0x200, RVA 0x1000)
    const uint32_t len = (uint32_t)msg.size();
    f[0x200]=0x48; f[0x201]=0x83; f[0x202]=0xEC; f[0x203]=0x28;   // sub rsp,0x28
    f[0x204]=0xBA; w32(0x205, 0x201100);                          // mov edx, &msg
    f[0x209]=0x41; f[0x20A]=0xB8; w32(0x20B, len);                // mov r8d, len
    f[0x20F]=0x31; f[0x210]=0xC9;                                 // xor ecx,ecx
    f[0x211]=0xFF; f[0x212]=0x15; w32(0x213, 0x1029);             // call [rip+IAT WriteFile]
    f[0x217]=0x31; f[0x218]=0xC9;                                 // xor ecx,ecx
    f[0x219]=0xFF; f[0x21A]=0x15; w32(0x21B, 0x1029);             // call [rip+IAT ExitProcess]
    f[0x21F]=0xF4;                                                // hlt (safety)
    str(0x300, msg.c_str());                                      // msg at RVA 0x1100

    // .idata (file 0x400, RVA 0x2000)
    w32(0x400+0,  0x2028);   // descriptor[0].OriginalFirstThunk
    w32(0x400+12, 0x2072);   // descriptor[0].Name
    w32(0x400+16, 0x2040);   // descriptor[0].FirstThunk (IAT)
    // descriptor[1] = all zero (terminator)
    w64(0x428, 0x2058); w64(0x430, 0x2064);          // ILT -> WriteFile, ExitProcess
    w64(0x440, 0x2058); w64(0x448, 0x2064);          // IAT (loader overwrites these)
    w16(0x458, 0); str(0x45A, "WriteFile");
    w16(0x464, 0); str(0x466, "ExitProcess");
    str(0x472, "ntkernel.dll");

    std::ofstream o(std::filesystem::path{path}, std::ios::binary);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(f.data()), (std::streamsize)f.size());
    return (bool)o;
}

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p); size_t sl = s.find_last_of(L"\\/");
    return sl == std::wstring::npos ? L"." : s.substr(0, sl);
}

int main() {
    std::printf("MicroVM PE loader: run a real .exe (ring-3, real SYSCALL path)\n");
    std::printf("  ------------------------------------------------------\n");

    const std::string msg = "Hello, world -- from a real PE loaded by our NT personality!";
    std::wstring pePath = ExeDir() + L"\\nt_hello.exe";
    if (!EmitTestPe(pePath, msg)) { std::printf("  [FAIL] could not write test PE\n"); return 1; }
    std::printf("  [gen ] wrote test PE: %ls\n", pePath.c_str());

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls (try 'av vmcheck')\n", vm.Error().c_str()); return 1; }

    // The synthetic kernel's import resolver: generate a hardened stub per fn.
    // Each stub uses the real SYSCALL instruction (ring-3 -> #UD -> our kernel),
    // so the loaded PE goes through the same evasion-resistant path as vmguard.
    uint64_t stubNext = kStubGpa;
    std::map<std::string, uint64_t> sysOf = {
        {"WriteFile", SYS_WriteFile}, {"ExitProcess", SYS_ExitProcess},
    };
    ImportResolver resolver = [&](const std::string&, const std::string& fn) -> uint64_t {
        auto it = sysOf.find(fn);
        if (it == sysOf.end()) return 0;
        uint64_t gpa = stubNext; stubNext += 8;
        uint8_t* s = static_cast<uint8_t*>(vm.RamData()) + gpa;
        s[0]=0xB8; s[1]=(uint8_t)it->second; s[2]=0; s[3]=0; s[4]=0;  // mov eax, sys#
        s[5]=0x0F; s[6]=0x05;                                        // syscall
        s[7]=0xC3;                                                   // ret
        return gpa;
    };

    PeLoader loader;
    PeImage img;
    if (!loader.Load(vm, pePath, kLoadGpa, resolver, img)) {
        std::printf("  [FAIL] load: %ls\n", loader.Error().c_str()); return 1;
    }
    std::printf("  [load] base=0x%llX entry=0x%llX size=0x%llX reloc=%s\n",
                (unsigned long long)img.imageBase, (unsigned long long)img.entryPoint,
                (unsigned long long)img.sizeOfImage, img.relocated ? "yes" : "no");
    for (auto& d : img.importedDlls) std::printf("  [load] imports from %s\n", d.c_str());
    for (auto& fn : img.resolvedFns) std::printf("  [load]   resolved %s -> host stub\n", fn.c_str());
    for (auto& fn : img.missingFns)  std::printf("  [load]   UNRESOLVED %s\n", fn.c_str());

    // The synthetic kernel.
    std::string captured;
    int exitCode = -1;
    vm.OnSyscall([&](MicroVm& m) {
        uint64_t sys = Reg(m, WHvX64RegisterRax);
        if (sys == SYS_WriteFile) {
            uint64_t ptr = Reg(m, WHvX64RegisterRdx);
            uint64_t n   = Reg(m, WHvX64RegisterR8);
            const char* g = static_cast<const char*>(m.RamData());
            if (ptr + n <= m.RamSize()) captured.assign(g + ptr, n);
            WHV_REGISTER_NAME rn = WHvX64RegisterRax;
            WHV_REGISTER_VALUE rv; ZeroMemory(&rv, sizeof(rv)); rv.Reg64 = n;  // bytes written
            m.SetRegisters(&rn, 1, &rv);
        } else if (sys == SYS_ExitProcess) {
            exitCode = (int)Reg(m, WHvX64RegisterRcx);
            m.RequestStop();  // ring-3 can't HLT; stop the VM from the kernel
        }
    });

    if (!vm.SetupLongMode(img.entryPoint, kStack, /*cpl3*/ true)) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1;
    }
    std::printf("  ------------------------------------------------------\n");
    bool halted = vm.Run();
    std::printf("  ------------------------------------------------------\n");
    if (!halted) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("  WriteFile output : \"%s\"\n", captured.c_str());
    std::printf("  ExitProcess code : %d\n\n", exitCode);

    bool ok = captured == msg && exitCode == 0;
    std::printf("  RESULT: %s\n", ok
        ? "a real PE ran in the sandbox; its Win32 calls were serviced by our\n"
          "          synthetic kernel. The \"feed a .exe and watch it\" loop is live."
        : "PE ran but output/exit did not match expectations.");
    return ok ? 0 : 1;
}
