// SnapshotManager.h - point-in-time snapshots of a MicroVm (roadmap step 6).
//
// A snapshot is the guest's full RAM image plus the vCPU's architectural
// register state. Capturing right after boot/load and restoring after a
// detonation resets the sandbox to an identical state in microseconds (a memcpy
// + a register write) instead of rebooting a guest. This is the engine half of
// the "disposable / fast reset" requirement.
#pragma once

#include "../vmm/MicroVmMonitor.h"

#include <cstdint>
#include <vector>

namespace av {
namespace microvm {

struct Snapshot {
    std::vector<uint8_t> ram;                  // full guest RAM (GPA 0..)
    std::vector<WHV_REGISTER_VALUE> regValues; // parallel to SnapshotManager::RegisterNames()
    bool valid = false;
};

class SnapshotManager {
public:
    // The fixed set of architectural registers a snapshot captures/restores.
    static const std::vector<WHV_REGISTER_NAME>& RegisterNames();

    // Capture RAM + registers into `out`. Returns false on failure (vm.Error()).
    bool Capture(MicroVm& vm, Snapshot& out);

    // Restore a previously captured snapshot. Returns false on failure.
    bool Restore(MicroVm& vm, const Snapshot& snap);
};

} // namespace microvm
} // namespace av
