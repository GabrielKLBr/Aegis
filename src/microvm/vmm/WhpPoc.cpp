// WhpPoc.cpp - roadmap step 1: a minimal Windows Hypervisor Platform proof of
// concept for the MicroVM sandbox (see docs/ARCHITECTURE-microvm-sandbox.md).
//
// What it proves:
//   * we can create a hardware-isolated partition via WHP (the "MicroVM floor"),
//   * map guest RAM at a guest-physical address that is NOT host memory,
//   * run a virtual processor, and
//   * intercept the guest's device I/O as VM-exits.
//
// The guest is 13 bytes of 16-bit real-mode code that writes 'O', 'K', '!' to
// I/O port 0xE9 (each OUT traps to us = the seed of PvConsole/PvDisk) and then
// HLTs. Nothing the guest does can touch the host: its writes land in the
// partition's guest-physical space, intercepted here.
//
// On a machine where virtualization is OFF (run `av vmcheck` first), this stops
// cleanly at WHvSetupPartition with a clear message. After you enable the
// Windows hypervisor and reboot, it runs the guest to completion.
//
// Build target: vmpoc.exe  (links WinHvPlatform).

#include <windows.h>
#include <winhvplatform.h>

#include <cstdio>
#include <cstdint>

namespace {

// Guest physical address where we place the code and point CS:RIP.
constexpr UINT64 kCodeGpa = 0x1000;
constexpr size_t kRamSize = 0x1000;  // one 4 KiB page, must be page-aligned

// 16-bit real-mode program:
//   mov al,'O' ; out 0xE9,al ; mov al,'K' ; out 0xE9,al ; mov al,'!' ; out 0xE9,al ; hlt
const unsigned char kGuestCode[] = {
    0xB0, 0x4F,        // mov al, 'O'
    0xE6, 0xE9,        // out 0xE9, al
    0xB0, 0x4B,        // mov al, 'K'
    0xE6, 0xE9,        // out 0xE9, al
    0xB0, 0x21,        // mov al, '!'
    0xE6, 0xE9,        // out 0xE9, al
    0xF4,              // hlt
};

void PrintHr(const char* step, HRESULT hr) {
    std::printf("  [FAIL] %-26s hr=0x%08lX\n", step, (unsigned long)hr);
}

bool HypervisorRunning() {
    BOOL present = FALSE;
    UINT32 written = 0;
    HRESULT hr = WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &present,
                                  sizeof(present), &written);
    return SUCCEEDED(hr) && present;
}

// Builds a real-mode segment register value (code or data).
WHV_REGISTER_VALUE RealModeSegment(UINT16 attributes) {
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));
    v.Segment.Base = 0;
    v.Segment.Limit = 0xFFFF;
    v.Segment.Selector = 0;
    v.Segment.Attributes = attributes;
    return v;
}

} // namespace

int main() {
    std::printf("MicroVM WHP proof of concept (roadmap step 1)\n");
    std::printf("  ------------------------------------------------------\n");

    if (!HypervisorRunning()) {
        std::printf("  [BLOCKED] The Windows hypervisor is not running on this machine,\n");
        std::printf("            so no partition can be created. Run 'av vmcheck' for the\n");
        std::printf("            exact steps to enable it, then reboot and re-run vmpoc.\n");
        return 2;
    }

    HRESULT hr;
    WHV_PARTITION_HANDLE partition = nullptr;
    void* ram = nullptr;
    int rc = 1;

    // 1) Create the partition (the isolated MicroVM container).
    hr = WHvCreatePartition(&partition);
    if (FAILED(hr)) { PrintHr("WHvCreatePartition", hr); goto cleanup; }
    std::printf("  [ ok ] partition created\n");

    // 2) Ask for exactly one virtual processor.
    {
        UINT32 cpuCount = 1;
        hr = WHvSetPartitionProperty(partition, WHvPartitionPropertyCodeProcessorCount,
                                     &cpuCount, sizeof(cpuCount));
        if (FAILED(hr)) { PrintHr("SetProperty(CpuCount)", hr); goto cleanup; }
    }

    // 3) Finalize the partition. *** This is the step that needs the hypervisor. ***
    hr = WHvSetupPartition(partition);
    if (FAILED(hr)) { PrintHr("WHvSetupPartition", hr); goto cleanup; }
    std::printf("  [ ok ] partition set up (EPT/SLAT-isolated)\n");

    // 4) Allocate host backing memory and map it into guest-physical space.
    ram = VirtualAlloc(nullptr, kRamSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!ram) { std::printf("  [FAIL] VirtualAlloc\n"); goto cleanup; }
    CopyMemory(ram, kGuestCode, sizeof(kGuestCode));
    hr = WHvMapGpaRange(partition, ram, kCodeGpa, kRamSize,
                        WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite |
                        WHvMapGpaRangeFlagExecute);
    if (FAILED(hr)) { PrintHr("WHvMapGpaRange", hr); goto cleanup; }
    std::printf("  [ ok ] %zu bytes mapped at guest-physical 0x%llX\n",
                kRamSize, (unsigned long long)kCodeGpa);

    // 5) Create the virtual processor.
    hr = WHvCreateVirtualProcessor(partition, 0, 0);
    if (FAILED(hr)) { PrintHr("WHvCreateVirtualProcessor", hr); goto cleanup; }

    // 6) Program real-mode segments + entry point (CS.Base=0, RIP=kCodeGpa).
    {
        const WHV_REGISTER_NAME names[] = {
            WHvX64RegisterCs, WHvX64RegisterDs, WHvX64RegisterEs,
            WHvX64RegisterSs, WHvX64RegisterRip, WHvX64RegisterRflags,
        };
        WHV_REGISTER_VALUE values[6];
        ZeroMemory(values, sizeof(values));
        values[0] = RealModeSegment(0x9B);      // CS: present, code, 16-bit
        values[1] = RealModeSegment(0x93);      // DS: present, data, rw
        values[2] = RealModeSegment(0x93);      // ES
        values[3] = RealModeSegment(0x93);      // SS
        values[4].Reg64 = kCodeGpa;             // RIP
        values[5].Reg64 = 0x2;                  // RFLAGS (reserved bit 1 set)

        hr = WHvSetVirtualProcessorRegisters(partition, 0, names, 6, values);
        if (FAILED(hr)) { PrintHr("SetRegisters", hr); goto cleanup; }
    }

    std::printf("  [ ok ] vCPU 0 ready; entering guest...\n");
    std::printf("  ------------------------------------------------------\n");

    // 7) Run loop: dispatch VM-exits until the guest halts.
    for (;;) {
        WHV_RUN_VP_EXIT_CONTEXT exit;
        ZeroMemory(&exit, sizeof(exit));
        hr = WHvRunVirtualProcessor(partition, 0, &exit, sizeof(exit));
        if (FAILED(hr)) { PrintHr("WHvRunVirtualProcessor", hr); goto cleanup; }

        if (exit.ExitReason == WHvRunVpExitReasonX64IoPortAccess) {
            UINT16 port = exit.IoPortAccess.PortNumber;
            UINT8 ch = (UINT8)(exit.IoPortAccess.Rax & 0xFF);
            std::printf("  [VM-EXIT] guest OUT port 0x%04X = 0x%02X ('%c')\n",
                        port, ch, (ch >= 32 && ch < 127) ? ch : '.');

            // WHP does not auto-advance RIP on I/O; we step past the OUT.
            WHV_REGISTER_NAME ripName = WHvX64RegisterRip;
            WHV_REGISTER_VALUE ripVal;
            ZeroMemory(&ripVal, sizeof(ripVal));
            ripVal.Reg64 = exit.VpContext.Rip + exit.VpContext.InstructionLength;
            hr = WHvSetVirtualProcessorRegisters(partition, 0, &ripName, 1, &ripVal);
            if (FAILED(hr)) { PrintHr("AdvanceRip", hr); goto cleanup; }
            continue;
        }

        if (exit.ExitReason == WHvRunVpExitReasonX64Halt) {
            std::printf("  [VM-EXIT] guest executed HLT - finished cleanly.\n");
            rc = 0;
            break;
        }

        std::printf("  [VM-EXIT] unexpected reason 0x%08lX (rip=0x%llX) - stopping.\n",
                    (unsigned long)exit.ExitReason,
                    (unsigned long long)exit.VpContext.Rip);
        break;
    }

    std::printf("  ------------------------------------------------------\n");
    if (rc == 0)
        std::printf("  RESULT: PASS - the MicroVM floor works. Guest I/O was intercepted\n"
                    "          on the host without ever touching real hardware.\n");

cleanup:
    if (partition) {
        WHvDeleteVirtualProcessor(partition, 0);
        if (ram) WHvUnmapGpaRange(partition, kCodeGpa, kRamSize);
        WHvDeletePartition(partition);
    }
    if (ram) VirtualFree(ram, 0, MEM_RELEASE);
    return rc;
}
