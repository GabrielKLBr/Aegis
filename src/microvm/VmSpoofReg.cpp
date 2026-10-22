// VmSpoofReg.cpp - anti-VM: read the registry "personality" the way malware does.
//
// Sandbox-aware samples don't just check CPUID -- they walk the registry looking
// for VM artifacts: SystemManufacturer = "VMware, Inc." / "innotek GmbH", a GPU
// DriverDesc of "VMware SVGA 3D" / "VirtualBox Graphics Adapter", a missing
// MachineGuid, an empty Uninstall list, the absence of a real CPU name. MiniNt
// seeds a coherent bare-metal Intel desktop (see MiniNt::SeedPersonality). This
// guest runs exactly those probes through the REAL ntdll stubs and verifies each
// one returns a believable non-VM value -- the machine reads as lived-in metal.
//
// Build target: vmspoofreg.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry = 0x2000, kNtdllBase = 0x400000;
static constexpr uint64_t kKusdVa = 0x7FE00000, kKusdBack = 0x600000;
static constexpr uint64_t kStack = 0xEFFFF8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam = 0x1000000;

// One probe = one key open + one value query. Lay out per-probe scratch in a
// 0x200-byte slot starting at 0x5000: OA, US(key), keybuf, US(val), valbuf,
// HKEY, query-output, ResultLength.
struct Probe { const wchar_t* key; const wchar_t* val; const char* label;
               std::string got = {}; uint32_t type = 0; };

int main() {
    std::printf("MicroVM anti-VM: read the registry personality a sandbox-aware sample reads\n");
    std::printf("  ------------------------------------------------------------------------\n");

    MicroVm vm;
    vm.SpoofCpuid("GenuineIntel", "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz", 8);
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr16 = [&](uint64_t a, uint16_t v){ CopyMemory(g+a,&v,2); };
    auto wr32 = [&](uint64_t a, uint32_t v){ CopyMemory(g+a,&v,4); };
    auto wr64 = [&](uint64_t a, uint64_t v){ CopyMemory(g+a,&v,8); };
    auto wstr = [&](uint64_t a, const std::wstring& s){ for (size_t i=0;i<s.size();++i) wr16(a+i*2,(uint16_t)s[i]); };

    Probe probes[] = {
        { L"\\Registry\\Machine\\Hardware\\Description\\System\\BIOS", L"SystemManufacturer", "BIOS SystemManufacturer" },
        { L"\\Registry\\Machine\\Hardware\\Description\\System\\CentralProcessor\\0", L"ProcessorNameString", "CPU ProcessorNameString" },
        { L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000", L"DriverDesc", "GPU DriverDesc" },
        { L"\\Registry\\Machine\\Software\\Microsoft\\Cryptography", L"MachineGuid", "MachineGuid" },
        { L"\\Registry\\Machine\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam", L"DisplayName", "Installed program (Steam)" },
    };
    const int N = (int)(sizeof(probes)/sizeof(probes[0]));

    // Per-probe scratch layout.
    // 0x400 per probe: OA/US headers low, then a 256-byte key buffer (the GPU
    // class key is ~100 wchars = 200 bytes), a value-name buffer, and a roomy
    // KEY_VALUE_PARTIAL_INFORMATION output.
    auto OA   = [](int i){ return 0x5000 + (uint64_t)i*0x400; };
    auto USk  = [](int i){ return 0x5040 + (uint64_t)i*0x400; };
    auto USv  = [](int i){ return 0x5060 + (uint64_t)i*0x400; };
    auto HK   = [](int i){ return 0x5078 + (uint64_t)i*0x400; };
    auto RL   = [](int i){ return 0x5080 + (uint64_t)i*0x400; };
    auto KBUF = [](int i){ return 0x5100 + (uint64_t)i*0x400; }; // up to 256 bytes
    auto VBUF = [](int i){ return 0x5200 + (uint64_t)i*0x400; }; // up to 128 bytes
    auto OUTB = [](int i){ return 0x5280 + (uint64_t)i*0x400; }; // up to 384 bytes
    // Per-probe call stacks (each NtXxx needs >=0x28 shadow+args), packed high.
    auto STK  = [](int i){ return 0xEFF000 - (uint64_t)i*0x400; };

    for (int i = 0; i < N; ++i) {
        std::wstring k = probes[i].key, v = probes[i].val;
        // OBJECT_ATTRIBUTES { Length, RootDirectory, ObjectName, Attributes }
        wr32(OA(i)+0x00, 0x30); wr64(OA(i)+0x08, 0); wr64(OA(i)+0x10, USk(i)); wr32(OA(i)+0x18, 0x40);
        uint16_t kb=(uint16_t)(k.size()*2); wr16(USk(i)+0,kb); wr16(USk(i)+2,kb); wr64(USk(i)+8,KBUF(i)); wstr(KBUF(i),k);
        uint16_t vb=(uint16_t)(v.size()*2); wr16(USv(i)+0,vb); wr16(USv(i)+2,vb); wr64(USv(i)+8,VBUF(i)); wstr(VBUF(i),v);
        // query stack args: [rsp+0x20]=Length(0x100), [rsp+0x28]=&ResultLength
        wr64(STK(i)+0x20, 0x100); wr64(STK(i)+0x28, RL(i));
    }

    PeLoader loader; PeImage img;
    ImportResolver none = [](const std::string&, const std::string&) -> uint64_t { return 0; };
    if (!loader.Load(vm, L"C:\\Windows\\System32\\ntdll.dll", kNtdllBase, none, img)) {
        std::printf("  [FAIL] load ntdll: %ls\n", loader.Error().c_str()); return 1;
    }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    ZeroMemory(g + kKusdBack, 0x200000);

    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    auto addr = [&](const char* n){ return kNtdllBase + (uint64_t)((uint8_t*)GetProcAddress(hn,n) - (uint8_t*)hn); };
    uint64_t aOpen = addr("NtOpenKey"), aQuery = addr("NtQueryValueKey"),
             aCreate = addr("NtCreateFile"), aTerm = addr("NtTerminateProcess");

    // Disk probes: open a VM driver (must be absent) and a normal file (present).
    struct FileProbe { const wchar_t* path; bool expectFound; const char* label; uint32_t status = 0; };
    FileProbe files[] = {
        { L"\\??\\C:\\Windows\\System32\\drivers\\vmci.sys", false, "VMware vmci.sys driver" },
        { L"\\??\\C:\\Windows\\System32\\drivers\\VBoxGuest.sys", false, "VirtualBox guest driver" },
        { L"\\??\\C:\\Windows\\System32\\kernel32.dll", true, "kernel32.dll (normal)" },
        { L"\\??\\C:\\Users\\Gabriel\\Desktop\\notas.txt", true, "user file (notas.txt)" },
    };
    const int NF = (int)(sizeof(files)/sizeof(files[0]));
    auto FOA = [](int j){ return 0x7000 + (uint64_t)j*0x200; };
    auto FUS = [](int j){ return 0x7040 + (uint64_t)j*0x200; };
    auto FPB = [](int j){ return 0x7060 + (uint64_t)j*0x200; };
    auto FIO = [](int j){ return 0x7180 + (uint64_t)j*0x200; };
    auto FFH = [](int j){ return 0x71A0 + (uint64_t)j*0x200; };
    auto FST = [](int j){ return 0x71B0 + (uint64_t)j*0x200; };  // captured NTSTATUS
    auto FSTK= [&](int j){ return 0xEFF000 - (uint64_t)(N+j)*0x400; };
    for (int j = 0; j < NF; ++j) {
        std::wstring p = files[j].path;
        wr32(FOA(j)+0x00, 0x30); wr64(FOA(j)+0x08, 0); wr64(FOA(j)+0x10, FUS(j)); wr32(FOA(j)+0x18, 0x40);
        uint16_t pb=(uint16_t)(p.size()*2); wr16(FUS(j)+0,pb); wr16(FUS(j)+2,pb); wr64(FUS(j)+8,FPB(j)); wstr(FPB(j),p);
        wr64(FSTK(j)+0x38, 1);   // CreateDisposition = FILE_OPEN (StackArg 8): a pure open
    }

    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto call= [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto esp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto absLoad = [&](uint8_t reg, uint32_t a){ e({0x48,0x8B,(uint8_t)(reg|0x04),0x25}); e32(a); };
    auto movEcx  = [&](uint32_t v){ e({0xB9}); e32(v); };
    auto movEdx  = [&](uint32_t v){ e({0xBA}); e32(v); };
    auto movR8d  = [&](uint32_t v){ e({0x41,0xB8}); e32(v); };
    auto movR9d  = [&](uint32_t v){ e({0x41,0xB9}); e32(v); };
    auto stEax   = [&](uint32_t a){ e({0x89,0x04,0x25}); e32(a); };  // mov [abs32], eax

    for (int i = 0; i < N; ++i) {
        // NtOpenKey(&hk, KEY_READ, &oa)
        esp((uint32_t)STK(i)); movEcx((uint32_t)HK(i)); movEdx(0x20019); movR8d((uint32_t)OA(i)); call(aOpen);
        // NtQueryValueKey(hk, &valName, KeyValuePartialInformation(2), &out, len, &rl)
        esp((uint32_t)STK(i)); absLoad(0x08,(uint32_t)HK(i)); movEdx((uint32_t)USv(i));
        movR8d(2); movR9d((uint32_t)OUTB(i)); call(aQuery);
    }
    for (int j = 0; j < NF; ++j) {
        // NtCreateFile(&FH, GENERIC_READ, &OA, &IOSB, ... disposition=FILE_OPEN)
        esp((uint32_t)FSTK(j)); movEcx((uint32_t)FFH(j)); movEdx(0x80000000);
        movR8d((uint32_t)FOA(j)); movR9d((uint32_t)FIO(j)); call(aCreate);
        stEax((uint32_t)FST(j));                 // capture the NTSTATUS
    }
    // NtTerminateProcess(-1, 0)
    esp((uint32_t)STK(0));
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2}); call(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    std::printf("  walking the registry + disk for VM artifacts through real ntdll stubs...\n");
    std::printf("  ------------------------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    // KEY_VALUE_PARTIAL_INFORMATION { TitleIndex, Type, DataLength, Data[] }.
    auto decodeSz = [&](uint64_t buf){
        std::string s; uint32_t dlen; std::memcpy(&dlen, g + buf + 8, 4);
        for (uint32_t i = 0; i + 1 < dlen; i += 2) { char ch=(char)g[buf+0xC+i]; if(!ch) break; s.push_back(ch); }
        return s;
    };
    auto hasVmTell = [](const std::string& s){
        static const char* tells[] = { "vmware", "virtualbox", "vbox", "qemu", "xen",
                                       "innotek", "parallels", "bochs", "hyper-v", "microsoft basic" };
        std::string low; for (char ch : s) low.push_back((char)tolower((unsigned char)ch));
        for (auto* t : tells) if (low.find(t) != std::string::npos) return true;
        return false;
    };

    bool ok = (kernel.ExitCode() == 0);
    std::printf("  registry:\n");
    for (int i = 0; i < N; ++i) {
        std::memcpy(&probes[i].type, g + OUTB(i) + 4, 4);
        probes[i].got = decodeSz(OUTB(i));
        bool clean = probes[i].type == 1 && !probes[i].got.empty() && !hasVmTell(probes[i].got);
        ok = ok && clean;
        std::printf("    %-26s -> \"%s\"  %s\n", probes[i].label, probes[i].got.c_str(),
                    clean ? "(non-VM)" : "(MISSING/VM-TELL)");
    }
    // Disk: a VM driver must report STATUS_OBJECT_NAME_NOT_FOUND (0xC0000034);
    // a normal file must open (STATUS_SUCCESS).
    std::printf("  disk:\n");
    for (int j = 0; j < NF; ++j) {
        std::memcpy(&files[j].status, g + FST(j), 4);
        bool found = (files[j].status == 0);
        bool good  = (found == files[j].expectFound);
        ok = ok && good;
        char w[64]; std::snprintf(w, sizeof w, "0x%08X %s", files[j].status,
                                  found ? "(present)" : "(NOT_FOUND)");
        std::printf("    %-26s -> %-22s %s\n", files[j].label, w,
                    good ? (files[j].expectFound ? "(real file)" : "(no VM artifact)") : "(WRONG)");
    }

    std::printf("  ------------------------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "every anti-VM probe returns a believable bare-metal value -- ASUS board,\n"
          "          Intel CPU, NVIDIA GPU, real MachineGuid, installed software; and on\n"
          "          disk the VMware/VirtualBox drivers are absent while normal files are\n"
          "          present. No VM tell anywhere; a sandbox-aware sample would detonate."
        : "a probe returned a VM tell, a missing value, or a wrong file state (see above).");
    return ok ? 0 : 1;
}
