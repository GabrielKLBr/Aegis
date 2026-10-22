// WinLoader.h - our OWN Windows image loader (the Wine/ReactOS approach).
//
// Instead of running ntdll's LdrpInitialize (which couples to multi-threaded
// internal state -- see vmldr), we bring up DLLs ourselves: map each as a
// SEC_IMAGE (headers + sections by RVA + DIR64 relocations), recursively load
// its dependencies, resolve imports across modules (including API set names),
// patch the IAT, and optionally run DllMain. This is how every working
// Windows-compat layer loads modules. It reuses everything proven in vmntimage.
#pragma once

#include "../vmm/MicroVmMonitor.h"

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace av {
namespace microvm {

class WinLoader {
public:
    // Images are placed into [regionBase, regionEnd) in guest RAM, 64 KiB-aligned.
    WinLoader(MicroVm& vm, uint64_t regionBase, uint64_t regionEnd)
        : vm_(vm), regionNext_(regionBase), regionEnd_(regionEnd) {}

    // Supply a DLL's raw bytes by name (for test DLLs not on the real disk).
    void Provide(const std::string& name, std::vector<uint8_t> bytes);
    // Where to find real system DLLs (defaults to System32).
    void SetSystemDir(const std::wstring& dir) { systemDir_ = dir; }

    // Load a module and all its dependencies. Returns the guest base, or 0.
    // Linking only (map + relocate + patch IAT). Running DllMain / exported code
    // is the harness's job (it owns the vm.Run loop + the MiniNt syscall handler).
    uint64_t Load(const std::string& name);
    // Resolve an exported function to its guest address (handles forwarders).
    uint64_t Resolve(const std::string& dll, const std::string& fn);
    // Enumerate a loaded module's exported names (for automated coverage).
    std::vector<std::string> Exports(const std::string& dll);

    struct Module {
        std::string name; uint64_t base = 0; uint32_t size = 0;
        std::vector<uint8_t> file; bool linked = false; uint64_t entry = 0;
    };
    const std::map<std::string, Module>& Modules() const { return mods_; }
    const std::string& Error() const { return error_; }
    int Resolved() const { return resolved_; }
    int Missing()  const { return missing_; }
    const std::vector<std::string>& MissingNames() const { return missingNames_; }

private:
    std::vector<uint8_t> FindFile(const std::string& name);
    uint64_t Place(const std::vector<uint8_t>& file, Module& m);     // layout into guest
    bool ProcessImports(Module& m);
    uint64_t ExportAddr(Module& m, const std::string& fn, uint16_t ordinal,
                        bool byOrdinal, int depth);
    uint64_t ResolveImport(const std::string& dll, const std::string& fn,
                           uint16_t ordinal, bool byOrdinal);

    MicroVm& vm_;
    uint64_t regionNext_, regionEnd_;
    std::wstring systemDir_ = L"C:\\Windows\\System32";
    std::map<std::string, std::vector<uint8_t>> provided_;  // lower name -> bytes
    std::map<std::string, Module> mods_;                    // lower name -> module
    std::string error_;
    int resolved_ = 0, missing_ = 0;
    std::vector<std::string> missingNames_;
};

// Hot-patch ntdll's heap + lock primitives to sidestep ntdll's uninitialized
// runtime (the Wine technique): Rtl*Heap -> `mov eax,<op>; syscall; ret` (MiniNt
// services <op> as a bump allocation), and the SRW/critical-section locks -> a
// no-op return (safe single-threaded). This lights up the heap/lock-dependent
// APIs without running ntdll's LdrpInitialize. Requires MiniNt as the syscall
// handler with a heap region set (SetHeapRegion). Returns how many were patched.
int ApplyWinePatches(MicroVm& vm, WinLoader& ld);

} // namespace microvm
} // namespace av
