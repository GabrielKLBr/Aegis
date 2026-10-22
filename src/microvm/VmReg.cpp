// VmReg.cpp - virtual registry + fake system profile (kernel version / device).
//
// The emitted PE behaves like profiling malware:
//   RtlGetVersion(&osv)                                  -> "what Windows is this?"
//   RegQueryValueA(HKLM ...\CurrentVersion, "ProductName") -> reads the OS name
//   RegSetValueA(HKCU ...\Run, "Updater", "C:\loot\evil.exe") -> persistence
//   ExitProcess(0)
//
// Every answer comes from a SystemProfile we choose. Run it under two profiles
// and the sample "sees" two different machines -- registry writes are captured,
// never touching the real HKLM/HKCU.
//
// Build target: vmreg.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/NtKernel.h"

#include <cstdio>
#include <cstring>
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
static constexpr uint32_t kIat[4]  = {0x2050, 0x2058, 0x2060, 0x2068};

// data addresses (abs = ImageBase 0x200000 + RVA)
static constexpr uint32_t kKeyCv = 0x2010A0, kProdName = 0x2010E0;
static constexpr uint32_t kRunKey = 0x201100, kUpdater = 0x201140, kPayload = 0x201160;
static constexpr uint32_t kOutBuf = 0x201200, kOsvBuf = 0x201300;

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p); size_t sl = s.find_last_of(L"\\/");
    return sl == std::wstring::npos ? L"." : s.substr(0, sl);
}

static bool EmitRegPe(const std::wstring& path) {
    const std::string keyCv   = "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const std::string prod    = "ProductName";
    const std::string runKey  = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const std::string updater = "Updater";
    const std::string payload = "C:\\loot\\evil.exe";

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

    // Code
    std::vector<uint8_t> code;
    auto emit  = [&](std::initializer_list<uint8_t> b){ for(auto x:b) code.push_back(x); };
    auto emit32= [&](uint32_t v){ for(int i=0;i<4;i++) code.push_back((v>>(8*i))&0xFF); };
    auto call  = [&](uint32_t iatRva){
        emit({0xFF, 0x15});
        uint32_t nextRva = 0x1000u + (uint32_t)code.size() + 4;
        emit32(iatRva - nextRva);
    };
    // RtlGetVersion(osvBuf)
    emit({0xB9}); emit32(kOsvBuf); call(kIat[0]);
    // RegQueryValueA(keyCv, "ProductName", outBuf, 256)
    emit({0xB9}); emit32(kKeyCv);
    emit({0xBA}); emit32(kProdName);
    emit({0x41,0xB8}); emit32(kOutBuf);
    emit({0x41,0xB9}); emit32(256);
    call(kIat[1]);
    // RegSetValueA(runKey, "Updater", payload, payloadLen)
    emit({0xB9}); emit32(kRunKey);
    emit({0xBA}); emit32(kUpdater);
    emit({0x41,0xB8}); emit32(kPayload);
    emit({0x41,0xB9}); emit32((uint32_t)payload.size());
    call(kIat[2]);
    // ExitProcess(0)
    emit({0x31,0xC9}); call(kIat[3]);
    emit({0xF4});
    for (size_t i=0;i<code.size();++i) f[0x200+i]=code[i];

    // Data strings (RVA < 0x1200 -> file-backed). file = RVA - 0x1000 + 0x200.
    str(0x2A0, keyCv);     // RVA 0x10A0
    str(0x2E0, prod);      // RVA 0x10E0
    str(0x300, runKey);    // RVA 0x1100
    str(0x340, updater);   // RVA 0x1140
    str(0x360, payload);   // RVA 0x1160

    // .idata: 4 imports
    w32(0x400+0, 0x2028); w32(0x400+12, 0x20B6); w32(0x400+16, 0x2050);
    w64(0x428, 0x2078); w64(0x430, 0x2088); w64(0x438, 0x2099); w64(0x440, 0x20A8);
    w64(0x450, 0x2078); w64(0x458, 0x2088); w64(0x460, 0x2099); w64(0x468, 0x20A8);
    w16(0x478, 0); str(0x47A, "RtlGetVersion");
    w16(0x488, 0); str(0x48A, "RegQueryValueA");
    w16(0x499, 0); str(0x49B, "RegSetValueA");
    w16(0x4A8, 0); str(0x4AA, "ExitProcess");
    str(0x4B6, "ntkernel.dll");

    std::ofstream o(std::filesystem::path{path}, std::ios::binary);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(f.data()), (std::streamsize)f.size());
    return (bool)o;
}

static bool RunWithProfile(const std::wstring& pePath, SystemProfile profile) {
    std::printf("=== profile: %s (reported as %u.%u build %u) ===\n",
                profile.name.c_str(), profile.major, profile.minor, profile.build);

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return false; }

    NtKernel kernel(profile);
    uint64_t stubNext = kStubGpa;
    ImportResolver resolver = [&](const std::string&, const std::string& fn) -> uint64_t {
        int sys = kernel.SyscallFor(fn);
        if (sys < 0) return 0;
        uint64_t gpa = stubNext; stubNext += 8;
        uint8_t* s = static_cast<uint8_t*>(vm.RamData()) + gpa;
        s[0]=0xB8; s[1]=(uint8_t)sys; s[2]=0; s[3]=0; s[4]=0; s[5]=0x0F; s[6]=0x05; s[7]=0xC3;
        return gpa;
    };

    PeLoader loader; PeImage img;
    if (!loader.Load(vm, pePath, kLoadGpa, resolver, img)) {
        std::printf("  [FAIL] load: %ls\n", loader.Error().c_str()); return false;
    }
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(img.entryPoint, kStack, /*cpl3*/ true)) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return false;
    }
    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return false; }

    // What the guest actually received back from our synthetic kernel:
    const uint8_t* g = static_cast<const uint8_t*>(vm.RamData());
    auto rd32 = [&](uint32_t abs){ uint32_t v; std::memcpy(&v, g + abs, 4); return v; };
    std::string prodSeen(reinterpret_cast<const char*>(g + kOutBuf));
    std::printf("  guest saw OS version : %u.%u build %u\n",
                rd32(kOsvBuf + 4), rd32(kOsvBuf + 8), rd32(kOsvBuf + 12));
    std::printf("  guest saw ProductName: \"%s\"\n", prodSeen.c_str());

    std::printf("%s", kernel.Report().Render().c_str());
    std::printf("  captured registry writes (real HKCU/HKLM untouched):\n");
    for (const auto& k : kernel.Registry().Keys())
        for (const auto& v : k.second)
            if (k.first.rfind("HKCU", 0) == 0)  // show what the sample wrote
                std::printf("    %s \\ %s = \"%s\"\n", k.first.c_str(), v.first.c_str(), v.second.c_str());

    bool ok = kernel.ExitCode() == 0 &&
              rd32(kOsvBuf + 12) == profile.build && prodSeen == profile.productName;
    std::printf("  -> %s\n\n", ok ? "PASS: sample saw our fabricated system."
                                   : "FAIL");
    return ok;
}

int main() {
    std::printf("MicroVM virtual registry + fake system profile\n");
    std::printf("  ------------------------------------------------------\n");
    std::wstring pePath = ExeDir() + L"\\nt_profiler.exe";
    if (!EmitRegPe(pePath)) { std::printf("  [FAIL] write PE\n"); return 1; }
    std::printf("  wrote test PE: %ls\n\n", pePath.c_str());

    bool a = RunWithProfile(pePath, SystemProfile::Win11());
    bool b = RunWithProfile(pePath, SystemProfile::Win7());

    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", (a && b)
        ? "the same sample saw Windows 11 then Windows 7 -- exactly the OS,\n"
          "          build and persistence view we chose. Registry stayed virtual."
        : "one or more runs failed.");
    return (a && b) ? 0 : 1;
}
