// MicroSandbox.cpp - implementation of the detonation facade.
#include "MicroSandbox.h"

#include "vmm/MicroVmMonitor.h"
#include "host/PeLoader.h"
#include "host/ProcessEnv.h"

#include <vector>

namespace av {
namespace microvm {

// Guest layout (matches the loader/stub conventions of the demos).
static constexpr uint64_t kLoadGpa  = 0x200000;
static constexpr uint64_t kStubGpa  = 0x100000;
static constexpr uint64_t kStack    = 0xEFFFF8;  // 16-aligned minus 8 (x64 ABI at entry)
static constexpr uint64_t kExcBitmap = (1ull << 6) | (1ull << 13);  // #UD | #GP

MicroSandboxResult MicroSandbox::Detonate(const std::wstring& pePath,
                                          const MicroSandboxOptions& opt) {
    MicroSandboxResult r;
    if (opt.guestCommandLine.empty() || opt.guestCommandLine.size() > 2047 ||
        opt.guestCommandLine.find(L'\0') != std::wstring::npos) {
        r.error = "Guest command line outside the process-parameter buffer";
        return r;
    }

    MicroVm vm;
    if (!vm.Create(opt.ramBytes, kExcBitmap)) {
        r.error = "MicroVM could not start (is the hypervisor running? run 'av vmcheck'). " +
                  std::string("[") + std::string(vm.Error().begin(), vm.Error().end()) + "]";
        return r;
    }

    NtKernel kernel(opt.profile);

    // Resolver: a hardened SYSCALL stub per supported import.
    uint64_t stubNext = kStubGpa;
    ImportResolver resolver = [&](const std::string&, const std::string& fn) -> uint64_t {
        int sys = kernel.SyscallFor(fn);
        if (sys < 0) return 0;
        uint64_t gpa = stubNext; stubNext += 8;
        uint8_t* s = static_cast<uint8_t*>(vm.RamData()) + gpa;
        s[0]=0xB8; s[1]=(uint8_t)sys; s[2]=0; s[3]=0; s[4]=0;  // mov eax, sys#
        s[5]=0x0F; s[6]=0x05; s[7]=0xC3;                       // syscall ; ret
        return gpa;
    };

    PeLoader loader;
    PeImage img;
    if (!loader.Load(vm, pePath, kLoadGpa, resolver, img)) {
        r.error = std::string(loader.Error().begin(), loader.Error().end());
        return r;
    }
    r.loaded = true;
    r.imports = img.importedDlls;
    r.resolved = img.resolvedFns;
    r.unresolved = img.missingFns;

    // Give the guest a process environment (TEB/PEB/Ldr) so it can read its
    // ImageBase, command line and module list like real ntdll/CRT code.
    ProcessEnv env = BuildProcessEnv(vm, 0x300000, img.imageBase, kStack, kStack - 0x100000,
                                     0x1A4, 0x1A8, opt.guestCommandLine);
    std::vector<ModuleSpec> mods = {
        {L"sample.exe",   img.imageBase, (uint32_t)img.sizeOfImage},
        {L"ntdll.dll",    0x180000000ull, 0x200000},
        {L"kernel32.dll", 0x190000000ull, 0x100000},
    };
    BuildLoaderData(vm, env, 0x310000, mods);

    vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
    if (!vm.SetupLongMode(img.entryPoint, kStack, /*cpl3*/ true)) {
        r.error = std::string(vm.Error().begin(), vm.Error().end());
        return r;
    }
    if (!vm.SetGsBase(env.teb)) {
        r.error = std::string(vm.Error().begin(), vm.Error().end());
        return r;
    }

    bool clean = vm.Run();
    r.ran = true;
    if (!clean) {
        if (vm.LastExceptionVector() != 0) {
            r.faulted = true;
            r.faultVector = vm.LastExceptionVector();
            r.faultRip = vm.LastExceptionRip();
        } else {
            r.error = std::string(vm.Error().begin(), vm.Error().end());
        }
    }

    r.exitCode = kernel.ExitCode();
    r.events = kernel.Report().Events();
    r.files = kernel.Fs().Files();
    r.registry = kernel.Registry().Keys();
    r.console = kernel.Console();
    r.reportText = kernel.Report().Render();
    return r;
}

} // namespace microvm
} // namespace av
