// VmFs.cpp - virtual FS + behavior report: run a PE that does file I/O and
// report exactly what it did, with the files captured in a virtual FS (never on
// the real disk).
//
// The emitted PE imports CreateFileA/WriteFile/CloseHandle/ExitProcess and:
//   h = CreateFileA("C:\loot\stolen.txt", GENERIC_WRITE, ...)
//   WriteFile(h, "<secret>", len, NULL, NULL)
//   CloseHandle(h)
//   ExitProcess(0)
// The synthetic NtKernel services each call against a VirtualFs and records a
// behavior timeline. Nothing touches the host file system.
//
// Build target: vmfs.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/NtKernel.h"

#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

using namespace av::microvm;

static constexpr uint64_t kLoadGpa = 0x200000;
static constexpr uint64_t kStubGpa = 0x100000;
static constexpr uint64_t kStack   = 0xF00000;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam     = 0x1000000;

// IAT slot RVAs (see .idata layout below).
static constexpr uint32_t kIatCreate = 0x2050, kIatWrite = 0x2058,
                          kIatClose = 0x2060, kIatExit = 0x2068;
static constexpr uint32_t kFileNameRva = 0x1100, kMsgRva = 0x1140;

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p); size_t sl = s.find_last_of(L"\\/");
    return sl == std::wstring::npos ? L"." : s.substr(0, sl);
}

// Emit a CRT-free PE that imports the four functions and does file I/O.
static bool EmitFsPe(const std::wstring& path, const std::string& fileName,
                     const std::string& secret) {
    std::vector<uint8_t> f(0x600, 0);
    auto w16 = [&](size_t o, uint16_t v){ f[o]=v&0xFF; f[o+1]=(v>>8)&0xFF; };
    auto w32 = [&](size_t o, uint32_t v){ for(int i=0;i<4;i++) f[o+i]=(v>>(8*i))&0xFF; };
    auto w64 = [&](size_t o, uint64_t v){ for(int i=0;i<8;i++) f[o+i]=(uint8_t)(v>>(8*i)); };
    auto str = [&](size_t o, const std::string& s){ for(size_t i=0;i<s.size();++i) f[o+i]=(uint8_t)s[i]; };

    // Headers (identical shape to the other demos).
    w16(0x00, 0x5A4D); w32(0x3C, 0x40);
    w32(0x40, 0x00004550);
    w16(0x44, 0x8664); w16(0x46, 2); w16(0x54, 0xF0); w16(0x56, 0x0022);
    w16(0x58, 0x020B); w32(0x5C, 0x200); w32(0x60, 0x200);
    w32(0x68, 0x1000); w32(0x6C, 0x1000); w64(0x70, 0x200000);
    w32(0x78, 0x1000); w32(0x7C, 0x200);
    w16(0x80, 6); w16(0x88, 6); w32(0x90, 0x3000); w32(0x94, 0x200);
    w16(0x9C, 3); w64(0xA0, 0x100000); w64(0xA8, 0x1000);
    w64(0xB0, 0x100000); w64(0xB8, 0x1000); w32(0xC4, 16);
    w32(0xD0, 0x2000); w32(0xD4, 40);                 // Import dir VA, Size
    str(0x148, ".text");
    w32(0x148+8, 0x200); w32(0x148+12, 0x1000); w32(0x148+16, 0x200);
    w32(0x148+20, 0x200); w32(0x148+36, 0x60000020);
    str(0x170, ".idata");
    w32(0x170+8, 0x200); w32(0x170+12, 0x2000); w32(0x170+16, 0x200);
    w32(0x170+20, 0x400); w32(0x170+36, 0xC0000040);

    // --- .text code, emitted with computed RIP-relative call displacements ---
    std::vector<uint8_t> code;
    auto emit  = [&](std::initializer_list<uint8_t> b){ for(auto x:b) code.push_back(x); };
    auto emit32= [&](uint32_t v){ for(int i=0;i<4;i++) code.push_back((v>>(8*i))&0xFF); };
    auto call  = [&](uint32_t iatRva){
        emit({0xFF, 0x15});
        uint32_t nextRva = 0x1000u + (uint32_t)code.size() + 4;  // after the disp32
        emit32(iatRva - nextRva);
    };
    const uint32_t base = 0x200000;
    const uint32_t len  = (uint32_t)secret.size();
    // CreateFileA(name, GENERIC_WRITE, 0, 0, ...)
    emit({0xB9}); emit32(base + kFileNameRva);   // mov ecx, &name
    emit({0xBA, 0x00, 0x00, 0x00, 0x40});        // mov edx, 0x40000000 (GENERIC_WRITE)
    emit({0x45, 0x31, 0xC0});                    // xor r8d, r8d
    emit({0x45, 0x31, 0xC9});                    // xor r9d, r9d
    call(kIatCreate);
    emit({0x48, 0x89, 0xC3});                    // mov rbx, rax  (save handle)
    // WriteFile(h, &secret, len, NULL, NULL)
    emit({0x48, 0x89, 0xD9});                    // mov rcx, rbx
    emit({0xBA}); emit32(base + kMsgRva);        // mov edx, &secret
    emit({0x41, 0xB8}); emit32(len);             // mov r8d, len
    emit({0x45, 0x31, 0xC9});                    // xor r9d, r9d
    call(kIatWrite);
    // CloseHandle(h)
    emit({0x48, 0x89, 0xD9});                    // mov rcx, rbx
    call(kIatClose);
    // ExitProcess(0)
    emit({0x31, 0xC9});                          // xor ecx, ecx
    call(kIatExit);
    emit({0xF4});                                // hlt (safety)
    for (size_t i = 0; i < code.size(); ++i) f[0x200 + i] = code[i];   // .text @ file 0x200

    str(0x300, fileName);   // RVA 0x1100
    str(0x340, secret);     // RVA 0x1140

    // --- .idata (file 0x400, RVA 0x2000): 4 imports from ntkernel.dll ---
    w32(0x400+0, 0x2028); w32(0x400+12, 0x20AE); w32(0x400+16, 0x2050);  // descriptor
    // ILT @ RVA 0x2028 (file 0x428)
    w64(0x428, 0x2078); w64(0x430, 0x2086); w64(0x438, 0x2092); w64(0x440, 0x20A0);
    // IAT @ RVA 0x2050 (file 0x450)
    w64(0x450, 0x2078); w64(0x458, 0x2086); w64(0x460, 0x2092); w64(0x468, 0x20A0);
    // hint/name tables
    w16(0x478, 0); str(0x47A, "CreateFileA");
    w16(0x486, 0); str(0x488, "WriteFile");
    w16(0x492, 0); str(0x494, "CloseHandle");
    w16(0x4A0, 0); str(0x4A2, "ExitProcess");
    str(0x4AE, "ntkernel.dll");

    std::ofstream o(std::filesystem::path{path}, std::ios::binary);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(f.data()), (std::streamsize)f.size());
    return (bool)o;
}

int main() {
    std::printf("MicroVM virtual FS + behavior report\n");
    std::printf("  ------------------------------------------------------\n");

    const std::string fileName = "C:\\loot\\stolen.txt";
    const std::string secret   = "secrets exfiltrated by the sample";
    std::wstring pePath = ExeDir() + L"\\nt_fileio.exe";
    if (!EmitFsPe(pePath, fileName, secret)) { std::printf("  [FAIL] write PE\n"); return 1; }
    std::printf("  [gen ] wrote test PE: %ls\n", pePath.c_str());

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls (try 'av vmcheck')\n", vm.Error().c_str()); return 1; }

    NtKernel kernel;
    uint64_t stubNext = kStubGpa;
    ImportResolver resolver = [&](const std::string&, const std::string& fn) -> uint64_t {
        int sys = kernel.SyscallFor(fn);
        if (sys < 0) return 0;
        uint64_t gpa = stubNext; stubNext += 8;
        uint8_t* s = static_cast<uint8_t*>(vm.RamData()) + gpa;
        s[0]=0xB8; s[1]=(uint8_t)sys; s[2]=0; s[3]=0; s[4]=0;   // mov eax, sys#
        s[5]=0x0F; s[6]=0x05; s[7]=0xC3;                        // syscall ; ret
        return gpa;
    };

    PeLoader loader;
    PeImage img;
    if (!loader.Load(vm, pePath, kLoadGpa, resolver, img)) {
        std::printf("  [FAIL] load: %ls\n", loader.Error().c_str()); return 1;
    }
    std::printf("  [load] entry=0x%llX, resolved:", (unsigned long long)img.entryPoint);
    for (auto& fn : img.resolvedFns) std::printf(" %s", fn.c_str());
    std::printf("\n");

    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(img.entryPoint, kStack, /*cpl3*/ true)) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1;
    }
    std::printf("  ------------------------------------------------------\n");
    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("%s\n", kernel.Report().Render().c_str());

    std::printf("  captured virtual files (host disk untouched):\n");
    for (const auto& kv : kernel.Fs().Files()) {
        std::string content(kv.second.begin(), kv.second.end());
        std::printf("    %s  ->  \"%s\"\n", kv.first.c_str(), content.c_str());
    }

    bool ok = kernel.ExitCode() == 0 && kernel.Fs().Files().count(fileName) &&
              std::string(kernel.Fs().Files().at(fileName).begin(),
                          kernel.Fs().Files().at(fileName).end()) == secret;
    std::printf("\n  RESULT: %s\n", ok
        ? "the sample's file write was virtualized and reported. Its 'loot'\n"
          "          lives only in our virtual FS; your real disk never saw it."
        : "did not capture the expected file activity.");
    return ok ? 0 : 1;
}
