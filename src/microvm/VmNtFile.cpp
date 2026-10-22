// VmNtFile.cpp - Model B grows: the real NT file subsystem.
//
// A ring-3 guest calls the REAL ntdll NtCreateFile / NtWriteFile / NtClose stubs
// (with real OBJECT_ATTRIBUTES / UNICODE_STRING / IO_STATUS_BLOCK structures) to
// drop a file. MiniNt services them by the real SSN, backed by the VirtualFs --
// so the file is captured virtually, real disk untouched. To keep the guest tiny
// the host pre-fills the structures and each call's stack-argument block, and the
// guest just sets RSP/registers and calls the real stub.
//
// Build target: vmntfile.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/MiniNt.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace av::microvm;

static constexpr uint64_t kEntry     = 0x2000;
static constexpr uint64_t kNtdllBase = 0x400000;
static constexpr uint64_t kKusdVa    = 0x7FE00000, kKusdBack = 0x600000;
static constexpr uint64_t kOA = 0x5100, kUStr = 0x5140, kPath = 0x5160;
static constexpr uint64_t kIosbC = 0x5200, kFH = 0x5210, kData = 0x5220, kIosbW = 0x5240;
static constexpr uint64_t kS1 = 0xEFF000, kS2 = 0xEFE000, kS3 = 0xEFD000;
static constexpr uint64_t kStack = 0xEFFFF8;
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);
static constexpr size_t   kRam = 0x1000000;

int main() {
    std::printf("MicroVM Model B: real ntdll file subsystem (NtCreateFile/Write/Close)\n");
    std::printf("  ------------------------------------------------------\n");

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
    uint64_t aCreate = addr("NtCreateFile"), aWrite = addr("NtWriteFile"),
             aClose = addr("NtClose"), aTerm = addr("NtTerminateProcess");

    // --- structures ---
    const std::wstring ntpath = L"\\??\\C:\\out\\dropped.bin";
    const std::string  payload = "real ntdll wrote this via NtWriteFile";
    // OBJECT_ATTRIBUTES
    wr32(kOA + 0x00, 0x30);          // Length
    wr64(kOA + 0x08, 0);             // RootDirectory
    wr64(kOA + 0x10, kUStr);         // ObjectName
    wr32(kOA + 0x18, 0x40);          // Attributes = OBJ_CASE_INSENSITIVE
    // UNICODE_STRING
    uint16_t pbytes = (uint16_t)(ntpath.size() * 2);
    wr16(kUStr + 0x00, pbytes);
    wr16(kUStr + 0x02, (uint16_t)(pbytes + 2));
    wr64(kUStr + 0x08, kPath);
    for (size_t i = 0; i < ntpath.size(); ++i) wr16(kPath + i*2, (uint16_t)ntpath[i]);
    // payload
    CopyMemory(g + kData, payload.data(), payload.size());
    // NtCreateFile stack args (5..11) at kS1+0x20..
    wr64(kS1 + 0x20, 0);       // AllocationSize (NULL)
    wr64(kS1 + 0x28, 0x80);    // FileAttributes = NORMAL
    wr64(kS1 + 0x30, 0);       // ShareAccess
    wr64(kS1 + 0x38, 5);       // CreateDisposition = FILE_OVERWRITE_IF
    wr64(kS1 + 0x40, 0x40);    // CreateOptions = FILE_NON_DIRECTORY_FILE
    wr64(kS1 + 0x48, 0);       // EaBuffer
    wr64(kS1 + 0x50, 0);       // EaLength
    // NtWriteFile stack args (5..9) at kS2+0x20..
    wr64(kS2 + 0x20, kIosbW);             // IoStatusBlock
    wr64(kS2 + 0x28, kData);              // Buffer
    wr64(kS2 + 0x30, payload.size());     // Length
    wr64(kS2 + 0x38, 0);                  // ByteOffset
    wr64(kS2 + 0x40, 0);                  // Key

    // --- guest code: set RSP + registers, call each real stub ---
    std::vector<uint8_t> c; std::vector<std::pair<size_t,uint64_t>> patches;
    auto e   = [&](std::initializer_list<uint8_t> b){ for (auto x:b) c.push_back(x); };
    auto e32 = [&](uint32_t v){ for (int i=0;i<4;i++) c.push_back((v>>(8*i))&0xFF); };
    auto callStub = [&](uint64_t a){ e({0xB8}); patches.push_back({c.size(),a}); e32(0); e({0xFF,0xD0}); };
    auto setEsp = [&](uint32_t s){ e({0xBC}); e32(s); };
    auto loadFH = [&]{ e({0x48,0x8B,0x0C,0x25}); e32((uint32_t)kFH); };   // mov rcx,[kFH]
    // NtCreateFile(&FH, GENERIC_WRITE, &OA, &IOSB)
    setEsp((uint32_t)kS1);
    e({0xB9}); e32((uint32_t)kFH);                 // mov ecx, &FileHandle
    e({0xBA}); e32(0x40000000);                    // mov edx, GENERIC_WRITE
    e({0x41,0xB8}); e32((uint32_t)kOA);            // mov r8d, &OA
    e({0x41,0xB9}); e32((uint32_t)kIosbC);         // mov r9d, &IoStatusBlock
    callStub(aCreate);
    // NtWriteFile(FH, 0,0,0, ...stack...)
    setEsp((uint32_t)kS2);
    loadFH();
    e({0x31,0xD2}); e({0x45,0x31,0xC0}); e({0x45,0x31,0xC9});   // xor edx; xor r8d; xor r9d
    callStub(aWrite);
    // NtClose(FH)
    setEsp((uint32_t)kS3);
    loadFH();
    callStub(aClose);
    // NtTerminateProcess(-1, 0)
    setEsp((uint32_t)kS3);
    e({0x48,0xC7,0xC1,0xFF,0xFF,0xFF,0xFF}); e({0x31,0xD2});    // mov rcx,-1; xor edx,edx
    callStub(aTerm);
    e({0xF4});
    for (auto& p : patches) for (int i=0;i<4;i++) c[p.first+i] = (uint8_t)(p.second >> (8*i));
    CopyMemory(g + kEntry, c.data(), c.size());

    MiniNt kernel;
    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(kEntry, kStack, /*cpl3*/ true)) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }
    vm.MapPage2MB(kKusdVa, kKusdBack);
    std::printf("  real ntdll mapped; calling NtCreateFile/NtWriteFile/NtClose stubs...\n");
    std::printf("  ------------------------------------------------------\n");

    if (!vm.Run()) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("%s", kernel.Report().Render().c_str());
    std::printf("  captured virtual files (real disk untouched):\n");
    for (auto& kv : kernel.Fs().Files()) {
        std::string content(kv.second.begin(), kv.second.end());
        std::printf("    %s  ->  \"%s\"\n", kv.first.c_str(), content.c_str());
    }

    auto it = kernel.Fs().Files().find("C:\\out\\dropped.bin");
    bool ok = kernel.ExitCode() == 0 && it != kernel.Fs().Files().end() &&
              std::string(it->second.begin(), it->second.end()) == payload;
    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", ok
        ? "the real ntdll file stubs ran and dropped a file through the mini\n"
          "          NT file subsystem -- captured virtually, real disk untouched."
        : "the real-ntdll file path did not complete as expected.");
    return ok ? 0 : 1;
}
