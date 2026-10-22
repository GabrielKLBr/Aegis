// ProcessEnv.h - lay out a minimal TEB / PEB / process parameters in guest RAM.
//
// This is the foundation for running real CRT binaries: ntdll and the C runtime
// read their world through GS (GS:[0x30] = TEB self, GS:[0x60] = PEB) long before
// main(). BuildProcessEnv writes those structures; load the returned .teb into
// GS via MicroVm::SetGsBase and the guest can navigate TEB -> PEB ->
// ProcessParameters -> CommandLine exactly like the real loader expects.
#pragma once

#include "../vmm/MicroVmMonitor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace av {
namespace microvm {

struct ProcessEnv {
    uint64_t teb = 0;
    uint64_t peb = 0;
    uint64_t params = 0;
};

struct ModuleSpec {
    std::wstring name;   // BaseDllName, e.g. "ntdll.dll"
    uint64_t     base;   // DllBase
    uint32_t     size;   // SizeOfImage
};

// Writes the structures into guest RAM starting at `gpaBase` (uses ~0x5000
// bytes of scratch there). The rest are values to publish to the guest.
ProcessEnv BuildProcessEnv(MicroVm& vm, uint64_t gpaBase, uint64_t imageBase,
                           uint64_t stackBase, uint64_t stackLimit,
                           uint32_t pid, uint32_t tid, const std::wstring& commandLine);

// Lays out a PEB_LDR_DATA + a circular module list (InLoadOrder / InMemoryOrder)
// of LDR_DATA_TABLE_ENTRY at `gpaBase`, and links PEB->Ldr. This is what
// GetModuleHandle / GetProcAddress and CRT startup walk to find modules.
void BuildLoaderData(MicroVm& vm, const ProcessEnv& env, uint64_t gpaBase,
                     const std::vector<ModuleSpec>& modules);

} // namespace microvm
} // namespace av
