// VmWinLoad.cpp - Step A: prove OUR loader (the Wine path) on controlled DLLs.
//
// WinLoader maps libb.dll, sees it imports AddOne from liba.dll, recursively
// loads liba.dll, and patches libb's IAT with liba's guest address. The guest
// then calls libb!Compute(20), whose body calls the imported liba!AddOne across
// the DLL boundary -- executing real cross-module linked code with NO ntdll
// LdrpInitialize involved. Compute(x) = AddOne(x)*2 + 8, so Compute(20) = 50 and
// liba!Magic() = 0x7E57.
//
// Build target: vmwinload.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/WinLoader.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry = 0x2000, kRes1 = 0x4000, kRes2 = 0x4008;
static constexpr uint64_t kStack = 0xEFFF00;
static constexpr uint64_t kRegionBase = 0x1000000, kRegionEnd = 0x3F00000;
static constexpr size_t   kRam = 0x4000000;

static std::vector<uint8_t> ReadAll(const wchar_t* p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main() {
    std::printf("MicroVM Wine path: OUR loader links + runs cross-module DLLs\n");
    std::printf("  ------------------------------------------------------\n");

    const std::wstring dir = L"C:\\Users\\gabri\\Desktop\\AntiVirus\\test\\";
    std::vector<uint8_t> a = ReadAll((dir + L"liba.dll").c_str());
    std::vector<uint8_t> b = ReadAll((dir + L"libb.dll").c_str());
    if (a.size() < 0x40 || b.size() < 0x40) { std::printf("  [FAIL] build liba/libb first\n"); return 1; }

    MicroVm vm;
    if (!vm.Create(kRam, 0)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    WinLoader ld(vm, kRegionBase, kRegionEnd);
    ld.Provide("liba.dll", a);
    ld.Provide("libb.dll", b);

    uint64_t bBase = ld.Load("libb.dll");
    if (!bBase) { std::printf("  [FAIL] load libb: %s\n", ld.Error().c_str()); return 1; }
    uint64_t compute = ld.Resolve("libb.dll", "Compute");
    uint64_t magic   = ld.Resolve("liba.dll", "Magic");
    uint64_t aBase   = ld.Load("liba.dll");
    std::printf("  liba.dll @ 0x%llX, libb.dll @ 0x%llX\n",
                (unsigned long long)aBase, (unsigned long long)bBase);
    std::printf("  resolved imports: %d, missing: %d\n", ld.Resolved(), ld.Missing());
    std::printf("  libb!Compute @ 0x%llX, liba!Magic @ 0x%llX\n",
                (unsigned long long)compute, (unsigned long long)magic);
    if (!compute || !magic) { std::printf("  [FAIL] export resolution failed\n"); return 1; }

    // Guest: r = Compute(20); m = Magic();  then HLT.
    std::vector<uint8_t> c;
    auto e   = [&](std::initializer_list<uint8_t> bs){ for (auto x:bs) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto absStore = [&](uint32_t addr){ e({0x48,0x89,0x04,0x25}); e32(addr); };   // mov [abs], rax
    e({0xB9}); e32(20);                          // mov ecx, 20
    e({0xB8}); e32((uint32_t)compute); e({0xFF,0xD0});   // mov eax,Compute; call rax
    absStore((uint32_t)kRes1);                   // [kRes1] = rax
    e({0xB8}); e32((uint32_t)magic);  e({0xFF,0xD0});    // mov eax,Magic; call rax
    absStore((uint32_t)kRes2);                   // [kRes2] = rax
    e({0xF4});                                   // hlt
    CopyMemory(g + kEntry, c.data(), c.size());

    // Ring 0 for this loader unit test: no syscalls/hardening here, and it lets
    // the guest HLT cleanly to end (HLT is privileged, so it #GPs in CPL3).
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ false)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    std::printf("  ------------------------------------------------------\n");
    std::printf("  calling libb!Compute(20) (which calls the imported liba!AddOne)...\n");
    if (!vm.Run(100000)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    uint64_t r1 = 0, r2 = 0;
    std::memcpy(&r1, g + kRes1, 8); std::memcpy(&r2, g + kRes2, 8);
    uint32_t compResult = (uint32_t)r1, magicResult = (uint32_t)r2;
    bool ok = (compResult == 50) && (magicResult == 0x7E57) && ld.Missing() == 0;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  Compute(20) = %u  (expected 50)  %s\n", compResult, compResult==50?"OK":"BAD");
    std::printf("  Magic()     = 0x%X  (expected 0x7E57)  %s\n", magicResult, magicResult==0x7E57?"OK":"BAD");
    std::printf("  RESULT: %s\n", ok
        ? "our loader mapped two DLLs, resolved + patched the cross-module import,\n"
          "          and the guest ran linked code calling across the DLL boundary --\n"
          "          a real Windows module brought up WITHOUT ntdll's LdrpInitialize."
        : "the cross-module link/run did not produce the expected values.");
    return ok ? 0 : 1;
}
