// VmSpoof.cpp - anti-VM: make CPUID look like a real bare-metal CPU.
//
// The #1 sandbox-evasion check is CPUID: the hypervisor-present bit (leaf 1
// ECX[31]), the hypervisor vendor leaf (0x40000000 -> "Microsoft Hv"/"VMware..."),
// and the CPU vendor/brand. If those scream "VM", malware bails. We trap CPUID at
// the WHP level and present a believable Intel desktop. This guest runs exactly
// the checks malware runs and we verify it sees no hypervisor.
//
// Build target: vmspoof.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry = 0x2000, kStack = 0xEFF00;
static constexpr uint64_t kEcx1 = 0x4000, kEbx1 = 0x4004, kVend = 0x4010, kHv = 0x4020, kBrand = 0x4030;
static constexpr size_t   kRam = 0x200000;

int main() {
    std::printf("MicroVM anti-VM: spoof CPUID to a bare-metal Intel desktop\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    vm.SpoofCpuid("GenuineIntel", "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz", 8);
    if (!vm.Create(kRam, 0)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());

    std::vector<uint8_t> c;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto eax = [&](uint32_t v){ e({0xB8}); e32(v); };
    auto cpuid = [&]{ e({0x0F,0xA2}); };
    // store r32 (0=eax,1=ecx,2=edx,3=ebx) to [abs32]
    auto st = [&](uint8_t reg, uint32_t addr){ e({0x89,(uint8_t)((reg<<3)|0x04),0x25}); e32(addr); };

    eax(1); cpuid(); st(1, (uint32_t)kEcx1); st(3, (uint32_t)kEbx1);          // leaf 1: ecx,ebx
    e({0x31,0xC0}); cpuid();                                                  // leaf 0
    st(3,(uint32_t)kVend+0); st(2,(uint32_t)kVend+4); st(1,(uint32_t)kVend+8);// vendor ebx,edx,ecx
    eax(0x40000000); cpuid(); st(0,(uint32_t)kHv);                           // hypervisor leaf -> eax
    uint32_t bl[3] = {0x80000002,0x80000003,0x80000004};
    for (int i=0;i<3;i++){ eax(bl[i]); cpuid();
        st(0,(uint32_t)kBrand+i*16+0); st(3,(uint32_t)kBrand+i*16+4);
        st(1,(uint32_t)kBrand+i*16+8); st(2,(uint32_t)kBrand+i*16+12); }   // brand eax,ebx,ecx,edx
    e({0xF4});
    CopyMemory(g + kEntry, c.data(), c.size());

    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ false)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    std::printf("  guest runs the CPUID checks a sandbox-aware sample runs...\n");
    if (!vm.Run(10000)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    uint32_t ecx1, ebx1, hv; std::memcpy(&ecx1,g+kEcx1,4); std::memcpy(&ebx1,g+kEbx1,4); std::memcpy(&hv,g+kHv,4);
    char vend[13]={0}, brand[49]={0};
    std::memcpy(vend, g+kVend, 12); std::memcpy(brand, g+kBrand, 48);
    bool hvBit = (ecx1 >> 31) & 1;
    uint32_t cpus = (ebx1 >> 16) & 0xFF;

    std::printf("  ------------------------------------------------------\n");
    std::printf("  CPUID.1:ECX[31] hypervisor bit : %u  %s\n", hvBit, hvBit?"(DETECTED - bad)":"(clear - bare metal)");
    std::printf("  CPUID.40000000h (hv vendor)    : 0x%08X  %s\n", hv, hv?"(present - bad)":"(absent - good)");
    std::printf("  CPU vendor                     : \"%s\"\n", vend);
    std::printf("  CPU brand                      : \"%s\"\n", brand);
    std::printf("  logical processors (CPUID.1:EBX): %u\n", cpus);

    bool ok = !hvBit && hv==0 && std::string(vend)=="GenuineIntel"
              && std::string(brand).rfind("Intel(R) Core(TM) i7-10700K",0)==0 && cpus==8;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "every hardware anti-VM probe sees a real Intel desktop -- no\n"
          "          hypervisor bit, no hypervisor vendor, a genuine CPU brand and\n"
          "          8 cores. A sandbox-aware sample would detonate, not bail."
        : "the CPUID spoof did not fully hide the VM (see above).");
    return ok ? 0 : 1;
}
