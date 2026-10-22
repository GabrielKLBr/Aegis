// VmSpoofNet.cpp - anti-VM: read the NIC's MAC the way GetAdaptersInfo bottoms out.
//
// The last common evasion vector is the MAC address: VMs hand out adapters with
// tell-tale OUIs (VMware 00:05:69/00:0C:29/00:50:56, VirtualBox 08:00:27,
// Hyper-V 00:15:5D, Xen 00:16:3E, QEMU 52:54:00). Whatever API a sample uses to
// read the MAC, in the kernel it reaches an NDIS OID query:
//   NtCreateFile("\Device\Tcp") ->
//   NtDeviceIoControlFile(IOCTL_NDIS_QUERY_GLOBAL_STATS, OID_802_3_PERMANENT_ADDRESS)
// MiniNt services that with a genuine Intel OUI. This guest runs exactly that path
// and we verify the MAC is a real Intel NIC, with no VM OUI anywhere.
//
// Build target: vmspoofnet.exe  (links WinHvPlatform).

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
static constexpr uint64_t kStack = 0xEFFFF8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam = 0x1000000;
// scratch
static constexpr uint64_t kOa=0x5000, kUs=0x5040, kPath=0x5060, kIosbC=0x5100, kFH=0x5110;
static constexpr uint64_t kOid=0x5120, kIosbI=0x5130, kMac=0x5140;
static constexpr uint64_t kStkC=0xEFF000, kStkI=0xEFE000, kStkT=0xEFD000;

int main() {
    std::printf("MicroVM anti-VM: read the NIC MAC the way GetAdaptersInfo bottoms out\n");
    std::printf("  ------------------------------------------------------------------------\n");

    MicroVm vm;
    vm.SpoofCpuid("GenuineIntel", "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz", 8);
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr16 = [&](uint64_t a, uint16_t v){ CopyMemory(g+a,&v,2); };
    auto wr32 = [&](uint64_t a, uint32_t v){ CopyMemory(g+a,&v,4); };
    auto wr64 = [&](uint64_t a, uint64_t v){ CopyMemory(g+a,&v,8); };
    auto wstr = [&](uint64_t a, const std::wstring& s){ for (size_t i=0;i<s.size();++i) wr16(a+i*2,(uint16_t)s[i]); };

    const std::wstring dev = L"\\Device\\Tcp";
    wr32(kOa+0x00, 0x30); wr64(kOa+0x08, 0); wr64(kOa+0x10, kUs); wr32(kOa+0x18, 0x40);
    uint16_t nb=(uint16_t)(dev.size()*2); wr16(kUs+0,nb); wr16(kUs+2,nb); wr64(kUs+8,kPath); wstr(kPath,dev);
    wr32(kOid, 0x01010101);                  // OID_802_3_PERMANENT_ADDRESS
    wr64(kStkC + 0x38, 1);                    // NtCreateFile CreateDisposition = FILE_OPEN
    // NtDeviceIoControlFile stack args (5..10), esp = kStkI:
    wr64(kStkI + 0x20, kIosbI);               // IoStatusBlock
    wr64(kStkI + 0x28, 0x00170002);           // IoControlCode = IOCTL_NDIS_QUERY_GLOBAL_STATS
    wr64(kStkI + 0x30, kOid);                 // InputBuffer
    wr64(kStkI + 0x38, 4);                    // InputBufferLength
    wr64(kStkI + 0x40, kMac);                 // OutputBuffer
    wr64(kStkI + 0x48, 6);                    // OutputBufferLength

    PeLoader loader; PeImage img;
    ImportResolver none = [](const std::string&, const std::string&) -> uint64_t { return 0; };
    if (!loader.Load(vm, L"C:\\Windows\\System32\\ntdll.dll", kNtdllBase, none, img)) {
        std::printf("  [FAIL] load ntdll: %ls\n", loader.Error().c_str()); return 1;
    }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);

    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    auto addr = [&](const char* n){ return kNtdllBase + (uint64_t)((uint8_t*)GetProcAddress(hn,n) - (uint8_t*)hn); };
    uint64_t aCreate = addr("NtCreateFile"), aIoctl = addr("NtDeviceIoControlFile"),
             aTerm = addr("NtTerminateProcess");

    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto call= [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto esp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto movEcx  = [&](uint32_t v){ e({0xB9}); e32(v); };
    auto movEdx  = [&](uint32_t v){ e({0xBA}); e32(v); };
    auto movR8d  = [&](uint32_t v){ e({0x41,0xB8}); e32(v); };
    auto movR9d  = [&](uint32_t v){ e({0x41,0xB9}); e32(v); };
    auto ldReg   = [&](uint8_t reg, uint32_t a){ e({0x48,0x8B,(uint8_t)((reg<<3)|0x04),0x25}); e32(a); }; // mov reg,[abs]

    // NtCreateFile(&FH, GENERIC_READ, &OA, &IOSB)  disposition=FILE_OPEN
    esp((uint32_t)kStkC); movEcx((uint32_t)kFH); movEdx(0x80000000);
    movR8d((uint32_t)kOa); movR9d((uint32_t)kIosbC); call(aCreate);
    // NtDeviceIoControlFile(hDev, 0, 0, 0, ...stack...)
    esp((uint32_t)kStkI); ldReg(1, (uint32_t)kFH);            // rcx = [FH] (the device handle)
    e({0x31,0xD2}); e({0x45,0x31,0xC0}); e({0x45,0x31,0xC9}); // xor edx; xor r8d; xor r9d
    call(aIoctl);
    // NtTerminateProcess(-1, 0)
    esp((uint32_t)kStkT);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2}); call(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    std::printf("  opening \\Device\\Tcp and issuing the NDIS MAC OID query...\n");
    std::printf("  ------------------------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    uint8_t mac[6]; std::memcpy(mac, g + kMac, 6);
    char macStr[32];
    std::snprintf(macStr, sizeof macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Known VM OUIs (first 3 bytes). Any match = the NIC betrays a hypervisor.
    static const uint8_t vmOuis[][3] = {
        {0x00,0x05,0x69}, {0x00,0x0C,0x29}, {0x00,0x1C,0x14}, {0x00,0x50,0x56}, // VMware
        {0x08,0x00,0x27}, {0x0A,0x00,0x27},                                     // VirtualBox
        {0x00,0x15,0x5D},                                                       // Hyper-V
        {0x00,0x16,0x3E},                                                       // Xen
        {0x00,0x1C,0x42},                                                       // Parallels
        {0x52,0x54,0x00},                                                       // QEMU/KVM
    };
    bool vmOui = false;
    for (auto& o : vmOuis) if (mac[0]==o[0] && mac[1]==o[1] && mac[2]==o[2]) vmOui = true;
    bool intel = (mac[0]==0x8C && mac[1]==0x16 && mac[2]==0x45);

    std::printf("  MAC address                    : %s\n", macStr);
    std::printf("  OUI (vendor prefix)            : %02X:%02X:%02X  %s\n",
                mac[0], mac[1], mac[2],
                vmOui ? "(VM OUI - DETECTED)" : intel ? "(Intel - real NIC)" : "(non-VM)");

    bool ok = kernel.ExitCode() == 0 && !vmOui && intel;
    std::printf("  ------------------------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the NIC returns a genuine Intel MAC through the real NDIS OID query path\n"
          "          (NtDeviceIoControlFile / IOCTL_NDIS_QUERY_GLOBAL_STATS). No VMware/\n"
          "          VirtualBox/Hyper-V/QEMU OUI -- the network side reads as bare metal."
        : "the MAC query did not return a believable non-VM Intel address (see above).");
    return ok ? 0 : 1;
}
