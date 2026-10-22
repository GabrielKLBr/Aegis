// SnapshotManager.cpp - capture/restore of MicroVm state.
#include "SnapshotManager.h"

#include <windows.h>

namespace av {
namespace microvm {

const std::vector<WHV_REGISTER_NAME>& SnapshotManager::RegisterNames() {
    // General-purpose + segment + instruction pointer/flags + control registers.
    // Enough to deterministically resume the guest from the captured point.
    static const std::vector<WHV_REGISTER_NAME> names = {
        WHvX64RegisterRax, WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterRbx,
        WHvX64RegisterRsp, WHvX64RegisterRbp, WHvX64RegisterRsi, WHvX64RegisterRdi,
        WHvX64RegisterR8,  WHvX64RegisterR9,  WHvX64RegisterR10, WHvX64RegisterR11,
        WHvX64RegisterR12, WHvX64RegisterR13, WHvX64RegisterR14, WHvX64RegisterR15,
        WHvX64RegisterRip, WHvX64RegisterRflags,
        WHvX64RegisterCs,  WHvX64RegisterSs,  WHvX64RegisterDs,  WHvX64RegisterEs,
        WHvX64RegisterFs,  WHvX64RegisterGs,
        WHvX64RegisterCr0, WHvX64RegisterCr2, WHvX64RegisterCr3, WHvX64RegisterCr4,
        WHvX64RegisterEfer,
    };
    return names;
}

bool SnapshotManager::Capture(MicroVm& vm, Snapshot& out) {
    const auto& names = RegisterNames();

    out.ram.resize(vm.RamSize());
    CopyMemory(out.ram.data(), vm.RamData(), vm.RamSize());

    out.regValues.resize(names.size());
    if (!vm.GetRegisters(names.data(), static_cast<uint32_t>(names.size()),
                         out.regValues.data()))
        return false;

    out.valid = true;
    return true;
}

bool SnapshotManager::Restore(MicroVm& vm, const Snapshot& snap) {
    if (!snap.valid || snap.ram.size() != vm.RamSize()) return false;

    CopyMemory(vm.RamData(), snap.ram.data(), vm.RamSize());

    const auto& names = RegisterNames();
    return vm.SetRegisters(names.data(), static_cast<uint32_t>(names.size()),
                           snap.regValues.data());
}

} // namespace microvm
} // namespace av
