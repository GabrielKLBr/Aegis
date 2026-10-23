// MicroSandbox.h - one-call facade over the whole NT-personality pipeline.
//
// Detonate() wires together: a hardware-isolated WHP MicroVm (ring-3, exception
// trapping) + PeLoader + the synthetic NtKernel (VirtualFs, VirtualRegistry,
// SystemProfile, ReportBuilder). Point it at a CRT-free PE and get back a
// structured behavior report -- nothing touches the real disk/registry.
#pragma once

#include "host/NtKernel.h"
#include "host/ReportBuilder.h"

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace av {
namespace microvm {

struct MicroSandboxOptions {
    SystemProfile profile = SystemProfile::Win11();
    size_t        ramBytes = 0x1000000;  // 16 MiB
    // Guest-visible identity must not contain the host's private spool path.
    std::wstring guestCommandLine = L"C:\\Samples\\sample.exe";
};

struct MicroSandboxResult {
    bool        loaded = false;   // PE parsed + mapped
    bool        ran = false;      // guest executed
    int         exitCode = -1;    // from ExitProcess
    bool        faulted = false;  // guest hit an un-handled exception (#GP, ...)
    uint8_t     faultVector = 0;
    uint64_t    faultRip = 0;
    std::string error;            // setup/load failure (empty on success)

    std::vector<std::string> imports;          // DLLs the PE imported from
    std::vector<std::string> resolved;         // functions we serviced
    std::vector<std::string> unresolved;       // functions we don't implement yet
    std::vector<ReportBuilder::Event> events;  // behavior timeline
    std::map<std::string, std::vector<uint8_t>> files;                       // virtual FS
    std::map<std::string, std::map<std::string, std::string>> registry;      // virtual registry
    std::string console;          // stdout/stderr the sample wrote
    std::string reportText;       // rendered timeline + summary
};

class MicroSandbox {
public:
    MicroSandboxResult Detonate(const std::wstring& pePath,
                                const MicroSandboxOptions& opt = {});
};

} // namespace microvm
} // namespace av
