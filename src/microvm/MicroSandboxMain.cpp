// MicroSandboxMain.cpp - microsandbox.exe: the consolidated NT-personality tool.
//
//   microsandbox --demo [win7|win10|win11]   detonate a built-in sample
//   microsandbox <file.exe> [win7|win10|win11] detonate a CRT-free PE
//
// Prints a single unified behavior report (files, registry, version queries),
// all virtualized -- the real disk/registry are never touched.

#include "MicroSandbox.h"

#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

using namespace av::microvm;

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p); size_t sl = s.find_last_of(L"\\/");
    return sl == std::wstring::npos ? L"." : s.substr(0, sl);
}

// Emit a CRT-free "fake malware" PE: profiles the OS, drops a file, and writes
// a persistence Run key -- the classic trio, all serviced by our kernel.
static bool EmitSamplePe(const std::wstring& path) {
    const std::string fileName = "C:\\Users\\Public\\dropped.dat";
    const std::string content  = "harvested credentials and keystrokes";
    const std::string runKey   = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const std::string updater  = "Updater";
    const std::string payload  = "C:\\Users\\Public\\dropped.dat";

    std::vector<uint8_t> f(0x600, 0);
    auto w16 = [&](size_t o, uint16_t v){ f[o]=v&0xFF; f[o+1]=(v>>8)&0xFF; };
    auto w32 = [&](size_t o, uint32_t v){ for(int i=0;i<4;i++) f[o+i]=(v>>(8*i))&0xFF; };
    auto w64 = [&](size_t o, uint64_t v){ for(int i=0;i<8;i++) f[o+i]=(uint8_t)(v>>(8*i)); };
    auto str = [&](size_t o, const std::string& s){ for(size_t i=0;i<s.size();++i) f[o+i]=(uint8_t)s[i]; };

    // Headers
    w16(0x00, 0x5A4D); w32(0x3C, 0x40);
    w32(0x40, 0x00004550);
    w16(0x44, 0x8664); w16(0x46, 2); w16(0x54, 0xF0); w16(0x56, 0x0022);
    w16(0x58, 0x020B); w32(0x5C, 0x200); w32(0x60, 0x200);
    w32(0x68, 0x1000); w32(0x6C, 0x1000); w64(0x70, 0x200000);
    w32(0x78, 0x1000); w32(0x7C, 0x200);
    w16(0x80, 6); w16(0x88, 6); w32(0x90, 0x3000); w32(0x94, 0x200);
    w16(0x9C, 3); w64(0xA0, 0x100000); w64(0xA8, 0x1000);
    w64(0xB0, 0x100000); w64(0xB8, 0x1000); w32(0xC4, 16);
    w32(0xD0, 0x2000); w32(0xD4, 40);
    str(0x148, ".text");
    w32(0x148+8, 0x200); w32(0x148+12, 0x1000); w32(0x148+16, 0x200);
    w32(0x148+20, 0x200); w32(0x148+36, 0x60000020);
    str(0x170, ".idata");
    w32(0x170+8, 0x200); w32(0x170+12, 0x2000); w32(0x170+16, 0x200);
    w32(0x170+20, 0x400); w32(0x170+36, 0xC0000040);

    // abs data addresses
    const uint32_t kFileName=0x2010A0, kContent=0x2010C0, kRunKey=0x201100,
                   kUpdater=0x201140, kPayload=0x201160, kOsv=0x201300;
    // IAT slots (6 imports, IAT @ RVA 0x2060)
    const uint32_t I0=0x2060,I1=0x2068,I2=0x2070,I3=0x2078,I4=0x2080,I5=0x2088;

    std::vector<uint8_t> code;
    auto emit  = [&](std::initializer_list<uint8_t> b){ for(auto x:b) code.push_back(x); };
    auto emit32= [&](uint32_t v){ for(int i=0;i<4;i++) code.push_back((v>>(8*i))&0xFF); };
    auto call  = [&](uint32_t iatRva){
        emit({0xFF,0x15});
        uint32_t nextRva = 0x1000u + (uint32_t)code.size() + 4;
        emit32(iatRva - nextRva);
    };
    // RtlGetVersion(osv)
    emit({0xB9}); emit32(kOsv); call(I0);
    // CreateFileA(filename, GENERIC_WRITE, 0, 0)
    emit({0xB9}); emit32(kFileName);
    emit({0xBA,0x00,0x00,0x00,0x40});
    emit({0x45,0x31,0xC0}); emit({0x45,0x31,0xC9});
    call(I1);
    emit({0x48,0x89,0xC3});                       // mov rbx, rax
    // WriteFile(h, content, len, 0)
    emit({0x48,0x89,0xD9});
    emit({0xBA}); emit32(kContent);
    emit({0x41,0xB8}); emit32((uint32_t)content.size());
    emit({0x45,0x31,0xC9});
    call(I2);
    // CloseHandle(h)
    emit({0x48,0x89,0xD9}); call(I3);
    // RegSetValueA(runKey, "Updater", payload, len)
    emit({0xB9}); emit32(kRunKey);
    emit({0xBA}); emit32(kUpdater);
    emit({0x41,0xB8}); emit32(kPayload);
    emit({0x41,0xB9}); emit32((uint32_t)payload.size());
    call(I4);
    // ExitProcess(0)
    emit({0x31,0xC9}); call(I5);
    emit({0xF4});
    for (size_t i=0;i<code.size();++i) f[0x200+i]=code[i];

    // Data strings (file = RVA - 0x1000 + 0x200)
    str(0x2A0, fileName);   // RVA 0x10A0
    str(0x2C0, content);    // RVA 0x10C0
    str(0x300, runKey);     // RVA 0x1100
    str(0x340, updater);    // RVA 0x1140
    str(0x360, payload);    // RVA 0x1160

    // .idata: 6 imports
    w32(0x400+0, 0x2028); w32(0x400+12, 0x20ED); w32(0x400+16, 0x2060);
    // ILT @ file 0x428
    w64(0x428,0x2098); w64(0x430,0x20A8); w64(0x438,0x20B6);
    w64(0x440,0x20C2); w64(0x448,0x20D0); w64(0x450,0x20DF);
    // IAT @ file 0x460
    w64(0x460,0x2098); w64(0x468,0x20A8); w64(0x470,0x20B6);
    w64(0x478,0x20C2); w64(0x480,0x20D0); w64(0x488,0x20DF);
    // names
    w16(0x498,0); str(0x49A, "RtlGetVersion");
    w16(0x4A8,0); str(0x4AA, "CreateFileA");
    w16(0x4B6,0); str(0x4B8, "WriteFile");
    w16(0x4C2,0); str(0x4C4, "CloseHandle");
    w16(0x4D0,0); str(0x4D2, "RegSetValueA");
    w16(0x4DF,0); str(0x4E1, "ExitProcess");
    str(0x4ED, "ntkernel.dll");

    std::ofstream o(std::filesystem::path{path}, std::ios::binary);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(f.data()), (std::streamsize)f.size());
    return (bool)o;
}

static SystemProfile PickProfile(const std::wstring& s) {
    if (s == L"win7")  return SystemProfile::Win7();
    if (s == L"win10") return SystemProfile::Win10();
    return SystemProfile::Win11();
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::printf("microsandbox - MicroVM NT-personality detonation\n");
    std::printf("  ------------------------------------------------------\n");

    if (argc < 2) {
        std::printf("usage:\n");
        std::printf("  microsandbox --demo [win7|win10|win11]\n");
        std::printf("  microsandbox <file.exe> [win7|win10|win11]\n");
        return 0;
    }

    std::wstring arg1 = argv[1];
    std::wstring profileArg = (argc >= 3) ? argv[2] : L"win11";
    MicroSandboxOptions opt; opt.profile = PickProfile(profileArg);

    std::wstring target;
    if (arg1 == L"--demo") {
        target = ExeDir() + L"\\nt_sample.exe";
        if (!EmitSamplePe(target)) { std::printf("  [FAIL] could not write sample PE\n"); return 1; }
        std::printf("  built-in sample: %ls\n", target.c_str());
    } else {
        target = arg1;
    }
    std::printf("  profile        : %s\n", opt.profile.name.c_str());
    std::printf("  ------------------------------------------------------\n");

    MicroSandbox sandbox;
    MicroSandboxResult r = sandbox.Detonate(target, opt);

    if (!r.error.empty() && !r.loaded) { std::printf("  ERROR: %s\n", r.error.c_str()); return 1; }

    for (auto& d : r.imports)    std::printf("  imports from   : %s\n", d.c_str());
    if (!r.unresolved.empty()) {
        std::printf("  UNRESOLVED     :");
        for (auto& fn : r.unresolved) std::printf(" %s", fn.c_str());
        std::printf("   (not yet implemented by the synthetic kernel)\n");
    }
    std::printf("  ------------------------------------------------------\n");
    std::printf("%s", r.reportText.c_str());

    if (!r.console.empty()) {
        std::printf("  console output:\n");
        std::printf("    | %s", r.console.c_str());
        if (r.console.back() != '\n') std::printf("\n");
    }
    if (!r.files.empty()) {
        std::printf("  virtual files (real disk untouched):\n");
        for (auto& kv : r.files) {
            std::string c(kv.second.begin(), kv.second.end());
            std::printf("    %s  ->  \"%s\"\n", kv.first.c_str(), c.c_str());
        }
    }
    bool anyReg = false;
    for (auto& k : r.registry)
        if (k.first.rfind("HKCU", 0) == 0 && !k.second.empty()) anyReg = true;
    if (anyReg) {
        std::printf("  virtual registry writes (real registry untouched):\n");
        for (auto& k : r.registry)
            if (k.first.rfind("HKCU", 0) == 0)
                for (auto& v : k.second)
                    std::printf("    %s \\ %s = \"%s\"\n", k.first.c_str(), v.first.c_str(), v.second.c_str());
    }
    if (r.faulted)
        std::printf("  blocked        : guest exception vector %u at rip=0x%llX\n",
                    (unsigned)r.faultVector, (unsigned long long)r.faultRip);

    std::printf("  ------------------------------------------------------\n");
    std::printf("  exit code      : %d\n", r.exitCode);
    std::printf("  verdict        : %s\n",
                r.ran && !r.faulted ? "detonated and reported; host untouched"
                                    : r.faulted ? "partially ran; a blocked action was caught"
                                                : "did not run");
    return 0;
}
