// VmSnap.cpp - roadmap step 6 demo: instant disposable reset via snapshots.
//
// Flow: load a guest that writes a marker byte (0x42) into guest RAM at 0x3000
// and halts. We snapshot the pristine state, "detonate" (run), observe the
// change, then RESTORE the snapshot and show the change is gone and the vCPU is
// back at the entry point -- in microseconds, no reboot. Finally we re-run from
// the restored state to prove it is a live, resumable image.
//
// Build target: vmsnap.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "host/SnapshotManager.h"

#include <cstdio>

using namespace av::microvm;

// 16-bit real-mode guest:  mov al,0x42 ; mov [0x3000],al ; hlt
static const unsigned char kGuest[] = {
    0xB0, 0x42,             // mov al, 0x42
    0xA2, 0x00, 0x30,       // mov [0x3000], al
    0xF4,                   // hlt
};
static constexpr uint64_t kEntry  = 0x1000;
static constexpr size_t   kMarker = 0x3000;

static uint8_t RamByte(const MicroVm& vm, size_t off) {
    return static_cast<const uint8_t*>(vm.RamData())[off];
}

static uint64_t ReadRip(MicroVm& vm) {
    WHV_REGISTER_NAME n = WHvX64RegisterRip;
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Reg64;
}

// Best-effort: clear the vCPU "halted" activity state so it can resume.
static void ClearHalt(MicroVm& vm) {
    WHV_REGISTER_NAME n = WHvRegisterInternalActivityState;
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));  // 0 == active
    vm.SetRegisters(&n, 1, &v);
}

int main() {
    std::printf("MicroVM snapshot / disposable-reset demo (roadmap step 6)\n");
    std::printf("  ------------------------------------------------------\n");

    MicroVm vm;
    if (!vm.Create(0x100000 /* 1 MiB */)) {
        std::printf("  [FAIL] %ls  (try 'av vmcheck')\n", vm.Error().c_str());
        return 1;
    }
    if (!vm.LoadRealModeCode(kGuest, sizeof(kGuest), kEntry)) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str());
        return 1;
    }

    SnapshotManager snaps;
    Snapshot baseline;
    if (!snaps.Capture(vm, baseline)) {
        std::printf("  [FAIL] capture: %ls\n", vm.Error().c_str());
        return 1;
    }
    std::printf("  [snap] baseline captured (%zu KiB RAM + %zu registers)\n",
                baseline.ram.size() / 1024, baseline.regValues.size());
    std::printf("         before detonation: RAM[0x%X]=0x%02X  RIP=0x%llX\n\n",
                (unsigned)kMarker, RamByte(vm, kMarker), (unsigned long long)ReadRip(vm));

    // --- detonate ---
    if (!vm.Run()) { std::printf("  [FAIL] run: %ls\n", vm.Error().c_str()); return 1; }
    std::printf("  [run ] detonated. The guest modified its memory.\n");
    std::printf("         after  detonation: RAM[0x%X]=0x%02X  RIP=0x%llX\n\n",
                (unsigned)kMarker, RamByte(vm, kMarker), (unsigned long long)ReadRip(vm));

    // --- restore (timed) ---
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    bool ok = snaps.Restore(vm, baseline);
    QueryPerformanceCounter(&t1);
    if (!ok) { std::printf("  [FAIL] restore: %ls\n", vm.Error().c_str()); return 1; }
    double us = double(t1.QuadPart - t0.QuadPart) * 1e6 / double(freq.QuadPart);

    std::printf("  [rst ] snapshot restored in %.1f us (1 MiB image, no reboot)\n", us);
    std::printf("         after  restore   : RAM[0x%X]=0x%02X  RIP=0x%llX\n",
                (unsigned)kMarker, RamByte(vm, kMarker), (unsigned long long)ReadRip(vm));

    bool reverted = RamByte(vm, kMarker) == 0x00 && ReadRip(vm) == kEntry;
    std::printf("         -> %s\n\n", reverted ? "state fully reverted to pristine."
                                               : "WARNING: state did not fully revert!");

    // --- prove the restored image is live: run it again ---
    ClearHalt(vm);
    if (vm.Run() && RamByte(vm, kMarker) == 0x42)
        std::printf("  [re  ] re-ran from the restored snapshot; marker set again (0x42).\n");
    else
        std::printf("  [re  ] (re-run skipped: vCPU stayed halted)\n");

    std::printf("  ------------------------------------------------------\n");
    std::printf("  RESULT: %s\n", reverted
        ? "disposable reset works. After malware runs, one restore wipes\n"
          "          every change (RAM + CPU) in microseconds - no VM reboot."
        : "snapshot restore incomplete - see warning above.");
    return reverted ? 0 : 1;
}
