// VmNtReg.cpp - Model B: the NT registry (NtOpenKey/NtQueryValueKey/NtSetValueKey).
//
// The loader/CRT read the registry early (SafeDllSearchMode, the OS version,
// Image File Execution Options). This drives that surface with the REAL ntdll
// stubs against MiniNt's in-memory \Registry namespace:
//   NtOpenKey(\Registry\Machine\System\...\Session Manager)
//     + NtQueryValueKey("SafeDllSearchMode")  -> REG_DWORD 1   (seeded)
//   NtOpenKey(\Registry\Machine\Software\...\CurrentVersion)
//     + NtQueryValueKey("ProductName")        -> REG_SZ "Windows 11 Pro" (seeded)
//   NtOpenKey(\Registry\Machine\...\Run)
//     + NtSetValueKey("Updater" = "...evil.exe")  then NtQueryValueKey back
//       -> proves a persistence write is captured virtually (real HKLM untouched).
//
// Build target: vmntreg.exe  (links WinHvPlatform).

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
// OBJECT_ATTRIBUTES + UNICODE_STRING + path buffer per key open.
static constexpr uint64_t kOaA=0x5000, kUsA=0x5040, kPaA=0x5060;
static constexpr uint64_t kOaB=0x5180, kUsB=0x51C0, kPaB=0x51E0;
static constexpr uint64_t kOaC=0x5300, kUsC=0x5340, kPaC=0x5360;
static constexpr uint64_t kHkA=0x5500, kHkB=0x5508, kHkC=0x5510;
// value-name UNICODE_STRINGs + buffers, plus the data to write.
static constexpr uint64_t kVnSafe=0x5520, kVbSafe=0x5540;
static constexpr uint64_t kVnProd=0x5580, kVbProd=0x55A0;
static constexpr uint64_t kVnUpd =0x55E0, kVbUpd =0x5600, kDataUpd=0x5640;
// KEY_VALUE_PARTIAL_INFORMATION output buffers + shared ResultLength.
static constexpr uint64_t kQSafe=0x5700, kQProd=0x5760, kQUpd=0x5800, kRl=0x5890;
static constexpr uint64_t kSq=0xEFF000, kSs=0xEFE000, kSc=0xEFD000, kSt=0xEFC000;
static constexpr uint64_t kStack = 0xEFFFF8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam = 0x1000000;

int main() {
    std::printf("MicroVM Model B: NT registry (NtOpenKey/NtQueryValueKey/NtSetValueKey)\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(kRam, kExcBitmap)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    auto* g = static_cast<uint8_t*>(vm.RamData());
    auto wr16 = [&](uint64_t a, uint16_t v){ CopyMemory(g+a,&v,2); };
    auto wr32 = [&](uint64_t a, uint32_t v){ CopyMemory(g+a,&v,4); };
    auto wr64 = [&](uint64_t a, uint64_t v){ CopyMemory(g+a,&v,8); };
    auto wstr = [&](uint64_t a, const std::wstring& s){ for (size_t i=0;i<s.size();++i) wr16(a+i*2,(uint16_t)s[i]); };
    // Fill an OBJECT_ATTRIBUTES + UNICODE_STRING + buffer for a key path.
    auto fillKey = [&](uint64_t oa, uint64_t us, uint64_t pbuf, const std::wstring& p){
        wr32(oa+0x00, 0x30); wr64(oa+0x08, 0); wr64(oa+0x10, us); wr32(oa+0x18, 0x40);
        uint16_t nb=(uint16_t)(p.size()*2); wr16(us+0,nb); wr16(us+2,nb); wr64(us+8,pbuf); wstr(pbuf,p);
    };
    // Fill a UNICODE_STRING value name + buffer.
    auto fillVal = [&](uint64_t us, uint64_t buf, const std::wstring& n){
        uint16_t nb=(uint16_t)(n.size()*2); wr16(us+0,nb); wr16(us+2,nb); wr64(us+8,buf); wstr(buf,n);
    };

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
             aSet = addr("NtSetValueKey"), aTerm = addr("NtTerminateProcess");

    fillKey(kOaA, kUsA, kPaA, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Session Manager");
    fillKey(kOaB, kUsB, kPaB, L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion");
    fillKey(kOaC, kUsC, kPaC, L"\\Registry\\Machine\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    fillVal(kVnSafe, kVbSafe, L"SafeDllSearchMode");
    fillVal(kVnProd, kVbProd, L"ProductName");
    fillVal(kVnUpd,  kVbUpd,  L"Updater");
    const std::wstring evil = L"C:\\Users\\Public\\evil.exe";
    wstr(kDataUpd, evil); wr16(kDataUpd + evil.size()*2, 0);   // REG_SZ + null
    uint32_t evilBytes = (uint32_t)((evil.size() + 1) * 2);

    // Shared stack args: query (len, &resultLen); set (data ptr, data size).
    wr64(kSq + 0x20, 0x80);  wr64(kSq + 0x28, kRl);
    wr64(kSs + 0x20, kDataUpd); wr64(kSs + 0x28, evilBytes);

    // --- guest code ---
    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto call= [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto esp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto absLoad = [&](uint8_t modrmReg, uint32_t a){ e({0x48,0x8B,(uint8_t)(modrmReg|0x04),0x25}); e32(a); };
    auto movEcx  = [&](uint32_t v){ e({0xB9}); e32(v); };
    auto movEdx  = [&](uint32_t v){ e({0xBA}); e32(v); };
    auto movR8d  = [&](uint32_t v){ e({0x41,0xB8}); e32(v); };
    auto movR9d  = [&](uint32_t v){ e({0x41,0xB9}); e32(v); };
    auto openKey = [&](uint32_t hk, uint32_t access, uint32_t oa){
        esp((uint32_t)kSc); movEcx(hk); movEdx(access); movR8d(oa); call(aOpen);
    };
    auto queryVal = [&](uint32_t hk, uint32_t vn, uint32_t out){
        esp((uint32_t)kSq); absLoad(0x08, hk); movEdx(vn); movR8d(2); movR9d(out); call(aQuery);
    };

    openKey((uint32_t)kHkA, 0x20019, (uint32_t)kOaA);          // KEY_READ
    queryVal((uint32_t)kHkA, (uint32_t)kVnSafe, (uint32_t)kQSafe);
    openKey((uint32_t)kHkB, 0x20019, (uint32_t)kOaB);
    queryVal((uint32_t)kHkB, (uint32_t)kVnProd, (uint32_t)kQProd);
    openKey((uint32_t)kHkC, 0xF003F, (uint32_t)kOaC);          // KEY_ALL_ACCESS
    // NtSetValueKey([hkC], &Updater, 0, REG_SZ, data, size)
    esp((uint32_t)kSs); absLoad(0x08, (uint32_t)kHkC); movEdx((uint32_t)kVnUpd);
    e({0x45,0x31,0xC0});                                       // xor r8d (TitleIndex)
    movR9d(1);                                                 // REG_SZ
    call(aSet);
    queryVal((uint32_t)kHkC, (uint32_t)kVnUpd, (uint32_t)kQUpd);  // read back what we set
    // NtTerminateProcess(-1, 0)
    esp((uint32_t)kSt);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2});
    call(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    std::printf("  reading seeded system keys + capturing a persistence write...\n");
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("%s", kernel.Report().Render().c_str());

    auto decodeSz = [&](uint64_t buf){            // UTF-16 Data[] -> narrow
        std::string s; uint32_t dlen; std::memcpy(&dlen, g + buf + 8, 4);
        for (uint32_t i = 0; i + 1 < dlen; i += 2) { char ch=(char)g[buf+0xC+i]; if(!ch) break; s.push_back(ch); }
        return s;
    };
    uint32_t safeType, safeData; std::memcpy(&safeType, g+kQSafe+4, 4); std::memcpy(&safeData, g+kQSafe+0xC, 4);
    uint32_t prodType; std::memcpy(&prodType, g+kQProd+4, 4);
    uint32_t updType;  std::memcpy(&updType,  g+kQUpd+4, 4);
    std::string prod = decodeSz(kQProd), upd = decodeSz(kQUpd);
    std::string evilN; for (wchar_t w : evil) evilN.push_back((char)w);

    bool safeOk = (safeType == 4 && safeData == 1);
    bool prodOk = (prodType == 1 && prod == "Windows 11 Pro");
    bool updOk  = (updType  == 1 && upd == evilN);
    std::printf("  SafeDllSearchMode -> REG_DWORD %u  (type %u) %s\n", safeData, safeType, safeOk?"OK":"BAD");
    std::printf("  ProductName       -> REG_SZ \"%s\" %s\n", prod.c_str(), prodOk?"OK":"BAD");
    std::printf("  Run\\Updater (set then read back) -> \"%s\" %s\n", upd.c_str(), updOk?"OK":"BAD");

    bool ok = kernel.ExitCode() == 0 && safeOk && prodOk && updOk;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the NT registry answered through real ntdll stubs: seeded system\n"
          "          values read with correct types, and a persistence write was\n"
          "          captured + round-tripped -- real HKLM/HKCU untouched."
        : "the registry surface did not answer as expected.");
    return ok ? 0 : 1;
}
