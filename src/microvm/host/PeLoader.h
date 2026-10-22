// PeLoader.h - minimal PE (PE32+) loader for the NT-personality MicroVM.
//
// Reads a real .exe from disk, maps its headers + sections into guest RAM,
// applies base relocations, and rewrites the Import Address Table so each
// imported function points at a host-provided stub (which rings the syscall
// doorbell). After Load(), point the vCPU at PeImage::entryPoint and run: the
// PE's calls into kernel32/ntdll arrive at our synthetic kernel.
//
// Scope of v1: 64-bit PE32+, import-by-name, DIR64 relocations. No TLS, no
// delay-imports, no forwarders, no CRT/PEB bring-up (use a CRT-free PE).
#pragma once

#include "../vmm/MicroVmMonitor.h"

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace av {
namespace microvm {

struct PeImage {
    uint64_t imageBase   = 0;   // GPA the image was loaded at
    uint64_t entryPoint  = 0;   // absolute GPA of the entry point
    uint64_t sizeOfImage = 0;
    bool     relocated   = false;
    std::vector<std::string> importedDlls;
    std::vector<std::string> resolvedFns;
    std::vector<std::string> missingFns;
};

// Maps (dllName, functionName) -> the guest address to store in the IAT, or 0
// if the function is not implemented by the synthetic kernel.
using ImportResolver = std::function<uint64_t(const std::string& dll, const std::string& fn)>;

class PeLoader {
public:
    bool Load(MicroVm& vm, const std::wstring& path, uint64_t loadGpa,
              const ImportResolver& resolve, PeImage& out);
    const std::wstring& Error() const { return error_; }

private:
    std::wstring error_;
};

} // namespace microvm
} // namespace av
