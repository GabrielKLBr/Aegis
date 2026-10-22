// NtKernel.cpp - syscall dispatch for the synthetic kernel.
#include "NtKernel.h"

#include <windows.h>
#include <vector>
#include <cwctype>
#include <thread>

namespace av {
namespace microvm {

// Win32-ish syscall numbers (the stubs put these in EAX).
enum : uint64_t {
    SYS_CreateFileA = 1,
    SYS_WriteFile   = 2,
    SYS_ReadFile    = 3,
    SYS_CloseHandle = 4,
    SYS_ExitProcess = 5,
    SYS_GetLastError = 6,
    SYS_RtlGetVersion = 7,
    SYS_RegSetValueA  = 8,   // simplified ABI: RCX=key, RDX=name, R8=data, R9=len
    SYS_RegQueryValueA = 9,  // simplified ABI: RCX=key, RDX=name, R8=out, R9=cap
    SYS_GetProcessHeap = 10,
    SYS_HeapAlloc      = 11, // RCX=hHeap, RDX=flags, R8=size
    SYS_VirtualAlloc   = 12, // RCX=addr, RDX=size, R8=type, R9=protect
    // CRT startup surface
    SYS_GetSystemTimeAsFileTime = 13, // RCX=LPFILETIME
    SYS_QueryPerformanceCounter = 14, // RCX=LARGE_INTEGER*
    SYS_GetCurrentProcessId     = 15,
    SYS_GetCurrentThreadId      = 16,
    SYS_GetTickCount            = 17,
    SYS_GetCommandLineW         = 18,
    SYS_GetModuleHandleW        = 19, // RCX=name (NULL = self)
    SYS_GetStartupInfoW         = 20, // RCX=LPSTARTUPINFOW
    SYS_TlsAlloc                = 21,
    SYS_TlsSetValue             = 22, // RCX=index, RDX=value
    SYS_TlsGetValue             = 23, // RCX=index
    SYS_GetStdHandle            = 24, // RCX=nStdHandle
    // GUI: proxied to the real host user32 (the "render natively" path)
    SYS_MessageBoxW             = 25, // RDX=text, R8=caption, R9=type
    SYS_MessageBoxA             = 26,
};

// Fake std handles (GetStdHandle returns these; WriteFile routes them to console).
static constexpr uint64_t kStdOut = 0x13, kStdErr = 0x15;

// Canonical CurrentVersion key the OS-info lives under.
static const char* kCurrentVersionKey =
    "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

namespace {
uint64_t Reg(MicroVm& vm, WHV_REGISTER_NAME n) {
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Reg64;
}
void SetReg(MicroVm& vm, WHV_REGISTER_NAME n, uint64_t val) {
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    v.Reg64 = val;
    vm.SetRegisters(&n, 1, &v);
}
// The current TEB = the GS segment base (set by MicroVm::SetGsBase).
uint64_t TebOf(MicroVm& vm) {
    WHV_REGISTER_NAME n = WHvX64RegisterGs;
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Segment.Base;
}
uint64_t Rd64(MicroVm& vm, uint64_t gva) {
    uint64_t v = 0;
    if (gva + 8 <= vm.RamSize()) CopyMemory(&v, static_cast<uint8_t*>(vm.RamData()) + gva, 8);
    return v;
}
// The n-th argument (1-based) under the Microsoft x64 ABI: args 1-4 are in
// RCX/RDX/R8/R9; args >=5 live on the caller's stack. At our SYSCALL trap RSP
// still points at the return address the `call` pushed, so arg5 is at [RSP+0x28].
uint64_t StackArg(MicroVm& vm, int n) {
    uint64_t rsp = Reg(vm, WHvX64RegisterRsp);
    return Rd64(vm, rsp + 0x28 + (uint64_t)(n - 5) * 8);
}
std::wstring ReadWStr(MicroVm& vm, uint64_t gva) {
    std::wstring s;
    const uint8_t* g = static_cast<const uint8_t*>(vm.RamData());
    for (uint64_t i = gva; i + 1 < vm.RamSize(); i += 2) {
        uint16_t c; CopyMemory(&c, g + i, 2); if (!c) break; s.push_back((wchar_t)c);
    }
    return s;
}
std::string Narrow(const std::wstring& w) {
    std::string s; for (wchar_t c : w) s.push_back(c < 128 ? (char)c : '?'); return s;
}
// Proxy a GUI dialog to the real host user32, but auto-dismiss it after it
// appears so a detonation never blocks. (A product would render into a
// sandbox-owned surface, not the real desktop.)
int HostMessageBox(const std::wstring& text, const std::wstring& caption) {
    std::thread watchdog([caption] {
        for (int i = 0; i < 60; ++i) {
            Sleep(50);
            HWND h = FindWindowW(nullptr, caption.c_str());
            if (h) { PostMessageW(h, WM_CLOSE, 0, 0); break; }
        }
    });
    int rc = ::MessageBoxW(nullptr, text.c_str(), caption.c_str(), MB_OK | MB_TOPMOST);
    watchdog.join();
    return rc;
}
} // namespace

SystemProfile SystemProfile::Win7()  { return {"Windows 7",  6, 1, 7601,  "Windows 7 Professional", "NVIDIA GeForce GTX 580"}; }
SystemProfile SystemProfile::Win10() { return {"Windows 10", 10, 0, 19045, "Windows 10 Pro",         "Intel(R) UHD Graphics 630"}; }
SystemProfile SystemProfile::Win11() { return {"Windows 11", 10, 0, 22631, "Windows 11 Pro",         "NVIDIA GeForce RTX 4070"}; }

NtKernel::NtKernel(SystemProfile profile) : profile_(std::move(profile)) {
    table_ = {
        {"CreateFileA", SYS_CreateFileA}, {"CreateFileW", SYS_CreateFileA},
        {"WriteFile", SYS_WriteFile},     {"ReadFile", SYS_ReadFile},
        {"CloseHandle", SYS_CloseHandle}, {"ExitProcess", SYS_ExitProcess},
        {"GetLastError", SYS_GetLastError},
        {"RtlGetVersion", SYS_RtlGetVersion},
        {"RegSetValueA", SYS_RegSetValueA},
        {"RegQueryValueA", SYS_RegQueryValueA},
        {"GetProcessHeap", SYS_GetProcessHeap},
        {"HeapAlloc", SYS_HeapAlloc}, {"RtlAllocateHeap", SYS_HeapAlloc},
        {"VirtualAlloc", SYS_VirtualAlloc},
        {"GetSystemTimeAsFileTime", SYS_GetSystemTimeAsFileTime},
        {"QueryPerformanceCounter", SYS_QueryPerformanceCounter},
        {"GetCurrentProcessId", SYS_GetCurrentProcessId},
        {"GetCurrentThreadId", SYS_GetCurrentThreadId},
        {"GetTickCount", SYS_GetTickCount},
        {"GetCommandLineW", SYS_GetCommandLineW},
        {"GetModuleHandleW", SYS_GetModuleHandleW},
        {"GetStartupInfoW", SYS_GetStartupInfoW},
        {"TlsAlloc", SYS_TlsAlloc}, {"TlsSetValue", SYS_TlsSetValue},
        {"TlsGetValue", SYS_TlsGetValue},
        {"GetStdHandle", SYS_GetStdHandle},
        {"MessageBoxW", SYS_MessageBoxW}, {"MessageBoxA", SYS_MessageBoxA},
    };
    SeedRegistry();
}

void NtKernel::SeedRegistry() {
    reg_.SetValue(kCurrentVersionKey, "ProductName", profile_.productName);
    reg_.SetValue(kCurrentVersionKey, "CurrentBuild", std::to_string(profile_.build));
    reg_.SetValue("HKLM\\HARDWARE\\DEVICEMAP\\VIDEO\\\\Device\\Video0",
                  "DriverDesc", profile_.gpu);
}

int NtKernel::SyscallFor(const std::string& fn) const {
    auto it = table_.find(fn);
    return it == table_.end() ? -1 : it->second;
}

std::string NtKernel::ReadCStr(MicroVm& vm, uint64_t gva) const {
    std::string s;
    const uint8_t* g = static_cast<const uint8_t*>(vm.RamData());
    for (uint64_t i = gva; i < vm.RamSize() && g[i]; ++i) s.push_back((char)g[i]);
    return s;
}

void NtKernel::Dispatch(MicroVm& vm) {
    uint64_t sys = Reg(vm, WHvX64RegisterRax);
    switch (sys) {
        case SYS_CreateFileA: {                       // CreateFileA(name, ..., disp=arg5, ...)
            std::string name = ReadCStr(vm, Reg(vm, WHvX64RegisterRcx));
            uint64_t disp = StackArg(vm, 5);          // dwCreationDisposition (5th arg)
            auto r = fs_.Open(name);
            const char* dn = disp == 1 ? "CREATE_NEW" : disp == 2 ? "CREATE_ALWAYS"
                           : disp == 3 ? "OPEN_EXISTING" : disp == 4 ? "OPEN_ALWAYS"
                           : disp == 5 ? "TRUNCATE_EXISTING" : nullptr;
            report_.Add(ReportBuilder::Cat::File, r.created ? "create" : "open", name,
                        dn ? std::string("disp=") + dn : std::string());
            SetReg(vm, WHvX64RegisterRax, r.handle);  // return HANDLE
            break;
        }
        case SYS_MessageBoxW: {                         // MessageBoxW(hWnd, text, caption, type)
            std::wstring text = ReadWStr(vm, Reg(vm, WHvX64RegisterRdx));
            std::wstring cap  = ReadWStr(vm, Reg(vm, WHvX64RegisterR8));
            report_.Add(ReportBuilder::Cat::Device, "gui MessageBox",
                        Narrow(cap), Narrow(text));
            int rc = HostMessageBox(text, cap);          // rendered natively on the host
            SetReg(vm, WHvX64RegisterRax, (uint64_t)rc);
            break;
        }
        case SYS_MessageBoxA: {                         // MessageBoxA(hWnd, text, caption, type)
            std::string text = ReadCStr(vm, Reg(vm, WHvX64RegisterRdx));
            std::string cap  = ReadCStr(vm, Reg(vm, WHvX64RegisterR8));
            report_.Add(ReportBuilder::Cat::Device, "gui MessageBox", cap, text);
            std::wstring wt(text.begin(), text.end()), wc(cap.begin(), cap.end());
            int rc = HostMessageBox(wt, wc);
            SetReg(vm, WHvX64RegisterRax, (uint64_t)rc);
            break;
        }
        case SYS_GetStdHandle:                          // GetStdHandle(nStdHandle=RCX)
            SetReg(vm, WHvX64RegisterRax,
                   Reg(vm, WHvX64RegisterRcx) == (uint64_t)(int64_t)-12 ? kStdErr : kStdOut);
            break;
        case SYS_WriteFile: {                          // WriteFile(h, buf, len, *written)
            uint64_t h    = Reg(vm, WHvX64RegisterRcx);
            uint64_t buf  = Reg(vm, WHvX64RegisterRdx);
            uint64_t len  = Reg(vm, WHvX64RegisterR8);
            uint64_t wptr = Reg(vm, WHvX64RegisterR9);
            const uint8_t* g = static_cast<const uint8_t*>(vm.RamData());
            std::string data = (buf + len <= vm.RamSize()) ? std::string((const char*)g + buf, (size_t)len) : "";
            if (h == kStdOut || h == kStdErr) {        // console output
                console_ += data;
                report_.Add(ReportBuilder::Cat::Process, "console-write", "",
                            std::to_string(len) + " bytes");
            } else {
                fs_.Write(h, reinterpret_cast<const uint8_t*>(data.data()), data.size());
                report_.Add(ReportBuilder::Cat::File, "write", fs_.PathOf(h),
                            std::to_string(len) + " bytes");
            }
            if (wptr && wptr + 4 <= vm.RamSize()) {
                uint32_t n = (uint32_t)len;
                CopyMemory(static_cast<uint8_t*>(vm.RamData()) + wptr, &n, 4);
            }
            SetReg(vm, WHvX64RegisterRax, 1);
            break;
        }
        case SYS_ReadFile: {                           // ReadFile(h, buf, len, *read)
            uint64_t h    = Reg(vm, WHvX64RegisterRcx);
            uint64_t buf  = Reg(vm, WHvX64RegisterRdx);
            uint64_t len  = Reg(vm, WHvX64RegisterR8);
            uint64_t rptr = Reg(vm, WHvX64RegisterR9);
            size_t got = 0;
            if (buf + len <= vm.RamSize())
                got = fs_.Read(h, static_cast<uint8_t*>(vm.RamData()) + buf, (size_t)len);
            report_.Add(ReportBuilder::Cat::File, "read", fs_.PathOf(h),
                        std::to_string(got) + " bytes");
            if (rptr && rptr + 4 <= vm.RamSize()) {
                uint32_t n = (uint32_t)got;
                CopyMemory(static_cast<uint8_t*>(vm.RamData()) + rptr, &n, 4);
            }
            SetReg(vm, WHvX64RegisterRax, 1);
            break;
        }
        case SYS_CloseHandle: {                        // CloseHandle(h=RCX)
            uint64_t h = Reg(vm, WHvX64RegisterRcx);
            std::string path = fs_.PathOf(h);
            bool ok = fs_.Close(h);
            report_.Add(ReportBuilder::Cat::File, "close", path);
            SetReg(vm, WHvX64RegisterRax, ok ? 1 : 0);
            break;
        }
        case SYS_GetLastError:
            SetReg(vm, WHvX64RegisterRax, 0);          // ERROR_SUCCESS
            break;
        case SYS_GetProcessHeap:
            SetReg(vm, WHvX64RegisterRax, heapBase_);   // a non-NULL heap handle
            break;
        case SYS_HeapAlloc: {                           // HeapAlloc(hHeap, flags, size)
            uint64_t flags = Reg(vm, WHvX64RegisterRdx);
            uint64_t size  = Reg(vm, WHvX64RegisterR8);
            uint64_t aligned = (size + 15) & ~15ull;
            uint64_t ptr = 0;
            if (heapNext_ + aligned <= heapEnd_) {
                ptr = heapNext_;
                heapNext_ += aligned;
                if (flags & 0x8)  // HEAP_ZERO_MEMORY
                    ZeroMemory(static_cast<uint8_t*>(vm.RamData()) + ptr, (size_t)size);
            }
            report_.Add(ReportBuilder::Cat::Process, "heap-alloc", "",
                        std::to_string(size) + " bytes");
            SetReg(vm, WHvX64RegisterRax, ptr);
            break;
        }
        case SYS_VirtualAlloc: {                        // VirtualAlloc(addr, size, type, prot)
            uint64_t addr = Reg(vm, WHvX64RegisterRcx);
            uint64_t size = Reg(vm, WHvX64RegisterRdx);
            uint64_t ptr  = addr;
            if (addr == 0) {
                uint64_t aligned = (size + 0xFFF) & ~0xFFFull;  // page-granular
                if (heapNext_ + aligned <= heapEnd_) { ptr = heapNext_; heapNext_ += aligned; }
                else ptr = 0;
            }
            report_.Add(ReportBuilder::Cat::Process, "virtual-alloc", "",
                        std::to_string(size) + " bytes");
            SetReg(vm, WHvX64RegisterRax, ptr);
            break;
        }
        case SYS_RtlGetVersion: {                      // RtlGetVersion(RCX=POSVERSIONINFOW)
            uint64_t p = Reg(vm, WHvX64RegisterRcx);
            uint8_t* g = static_cast<uint8_t*>(vm.RamData());
            auto wr = [&](uint64_t off, uint32_t v) {
                if (p + off + 4 <= vm.RamSize()) CopyMemory(g + p + off, &v, 4);
            };
            wr(4, profile_.major);   // dwMajorVersion
            wr(8, profile_.minor);   // dwMinorVersion
            wr(12, profile_.build);  // dwBuildNumber
            wr(16, 2);               // dwPlatformId = VER_PLATFORM_WIN32_NT
            report_.Add(ReportBuilder::Cat::Device, "query", "OS version",
                        profile_.name + " (build " + std::to_string(profile_.build) + ")");
            SetReg(vm, WHvX64RegisterRax, 0);          // STATUS_SUCCESS
            break;
        }
        case SYS_RegSetValueA: {                       // RCX=key RDX=name R8=data R9=len
            std::string key  = ReadCStr(vm, Reg(vm, WHvX64RegisterRcx));
            std::string name = ReadCStr(vm, Reg(vm, WHvX64RegisterRdx));
            uint64_t dptr = Reg(vm, WHvX64RegisterR8), len = Reg(vm, WHvX64RegisterR9);
            const char* g = static_cast<const char*>(vm.RamData());
            std::string data = (dptr + len <= vm.RamSize()) ? std::string(g + dptr, len) : "";
            reg_.SetValue(key, name, data);
            report_.Add(ReportBuilder::Cat::Registry, "set", key + " \\ " + name, data);
            SetReg(vm, WHvX64RegisterRax, 0);
            break;
        }
        case SYS_RegQueryValueA: {                     // RCX=key RDX=name R8=out R9=cap
            std::string key  = ReadCStr(vm, Reg(vm, WHvX64RegisterRcx));
            std::string name = ReadCStr(vm, Reg(vm, WHvX64RegisterRdx));
            uint64_t out = Reg(vm, WHvX64RegisterR8), cap = Reg(vm, WHvX64RegisterR9);
            std::string val;
            bool hit = reg_.QueryValue(key, name, val);
            if (hit && out && out + val.size() + 1 <= vm.RamSize() && val.size() < cap) {
                uint8_t* g = static_cast<uint8_t*>(vm.RamData());
                CopyMemory(g + out, val.data(), val.size());
                g[out + val.size()] = 0;
            }
            report_.Add(ReportBuilder::Cat::Registry, hit ? "query" : "query (miss)",
                        key + " \\ " + name, hit ? val : "");
            SetReg(vm, WHvX64RegisterRax, hit ? (uint64_t)val.size() : 0);
            break;
        }
        case SYS_GetSystemTimeAsFileTime: {            // RCX = LPFILETIME
            uint64_t p = Reg(vm, WHvX64RegisterRcx);
            uint64_t fake = 0x01DA000000000000ull;     // a plausible FILETIME
            if (p + 8 <= vm.RamSize()) CopyMemory(static_cast<uint8_t*>(vm.RamData()) + p, &fake, 8);
            break;
        }
        case SYS_QueryPerformanceCounter: {            // RCX = LARGE_INTEGER*
            uint64_t p = Reg(vm, WHvX64RegisterRcx);
            uint64_t ctr = 0x0000000123456789ull;
            if (p + 8 <= vm.RamSize()) CopyMemory(static_cast<uint8_t*>(vm.RamData()) + p, &ctr, 8);
            SetReg(vm, WHvX64RegisterRax, 1);
            break;
        }
        case SYS_GetCurrentProcessId:                  // TEB->ClientId.UniqueProcess
            SetReg(vm, WHvX64RegisterRax, Rd64(vm, TebOf(vm) + 0x40));
            break;
        case SYS_GetCurrentThreadId:                   // TEB->ClientId.UniqueThread
            SetReg(vm, WHvX64RegisterRax, Rd64(vm, TebOf(vm) + 0x48));
            break;
        case SYS_GetTickCount:
            SetReg(vm, WHvX64RegisterRax, 0x0012D4F0);
            break;
        case SYS_GetCommandLineW: {                    // -> PEB->...->CommandLine.Buffer
            uint64_t peb = Rd64(vm, TebOf(vm) + 0x60);
            uint64_t params = Rd64(vm, peb + 0x20);
            SetReg(vm, WHvX64RegisterRax, Rd64(vm, params + 0x78));
            break;
        }
        case SYS_GetModuleHandleW: {                   // RCX = name (NULL = self)
            uint64_t name = Reg(vm, WHvX64RegisterRcx);
            uint64_t peb = Rd64(vm, TebOf(vm) + 0x60);
            if (name == 0) { SetReg(vm, WHvX64RegisterRax, Rd64(vm, peb + 0x10)); break; }
            // walk PEB->Ldr->InLoadOrderModuleList comparing BaseDllName
            const uint8_t* g = static_cast<const uint8_t*>(vm.RamData());
            std::wstring want;
            for (uint64_t i = name; i + 1 < vm.RamSize(); i += 2) {
                uint16_t c; CopyMemory(&c, g + i, 2); if (!c) break;
                want.push_back((wchar_t)towlower(c));
            }
            uint64_t ldr = Rd64(vm, peb + 0x18), head = ldr + 0x10, result = 0;
            for (uint64_t cur = Rd64(vm, head); cur && cur != head; cur = Rd64(vm, cur)) {
                uint16_t blen; CopyMemory(&blen, g + cur + 0x58, 2);
                uint64_t bbuf = Rd64(vm, cur + 0x60);
                std::wstring got;
                for (int i = 0; i < blen / 2; ++i) { uint16_t c; CopyMemory(&c, g + bbuf + i * 2, 2); got.push_back((wchar_t)towlower(c)); }
                if (got == want) { result = Rd64(vm, cur + 0x30); break; }
            }
            SetReg(vm, WHvX64RegisterRax, result);
            break;
        }
        case SYS_GetStartupInfoW: {                    // RCX = LPSTARTUPINFOW
            uint64_t p = Reg(vm, WHvX64RegisterRcx);
            if (p + 104 <= vm.RamSize()) {
                ZeroMemory(static_cast<uint8_t*>(vm.RamData()) + p, 104);
                uint32_t cb = 104; CopyMemory(static_cast<uint8_t*>(vm.RamData()) + p, &cb, 4);
            }
            break;
        }
        case SYS_TlsAlloc:
            SetReg(vm, WHvX64RegisterRax, tlsNext_ < 64 ? (uint64_t)tlsNext_++ : 0xFFFFFFFFull);
            break;
        case SYS_TlsSetValue: {                        // RCX=index, RDX=value
            uint64_t idx = Reg(vm, WHvX64RegisterRcx), val = Reg(vm, WHvX64RegisterRdx);
            if (idx < 64) {
                uint64_t slot = TebOf(vm) + 0x1480 + idx * 8;
                if (slot + 8 <= vm.RamSize()) CopyMemory(static_cast<uint8_t*>(vm.RamData()) + slot, &val, 8);
            }
            SetReg(vm, WHvX64RegisterRax, 1);
            break;
        }
        case SYS_TlsGetValue: {                        // RCX=index
            uint64_t idx = Reg(vm, WHvX64RegisterRcx);
            SetReg(vm, WHvX64RegisterRax, idx < 64 ? Rd64(vm, TebOf(vm) + 0x1480 + idx * 8) : 0);
            break;
        }
        case SYS_ExitProcess:                          // ExitProcess(code=RCX)
            exitCode_ = (int)Reg(vm, WHvX64RegisterRcx);
            report_.Add(ReportBuilder::Cat::Process, "exit", "",
                        std::to_string(exitCode_));
            vm.RequestStop();
            break;
        default:
            report_.Add(ReportBuilder::Cat::Other, "unimplemented syscall",
                        std::to_string(sys));
            SetReg(vm, WHvX64RegisterRax, 0);
            break;
    }
}

} // namespace microvm
} // namespace av
