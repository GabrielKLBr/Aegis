// VmDemo.cpp - roadmap step 2 demo: the paravirtual device layer in action.
//
// Runs a tiny 16-bit guest that (1) prints "MBR" to a PvConsole and (2) writes
// a 512-byte "boot sector" of 0xCC to LBA 0 through a PvDisk -- the same move
// MEMZ makes against \\.\PhysicalDrive0. Here it lands in a throwaway
// differential image; the host's real disk is never opened. This demonstrates,
// on real hardware-isolated WHP, why destructive malware only destroys the
// sandbox.
//
// Build target: vmdemo.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"
#include "vmm/devices/Devices.h"

#include <cstdio>
#include <string>

using namespace av::microvm;

// 16-bit real-mode guest:
//   mov al,'M'; out 0xE9      mov al,'B'; out 0xE9      mov al,'R'; out 0xE9
//   mov al,0;  out 0x71       ; select LBA 0 (the MBR)
//   mov cx,512
// loop:
//   mov al,0xCC; out 0x70     ; stream the boot sector to PvDisk
//   dec cx; jnz loop
//   hlt
static const unsigned char kGuest[] = {
    0xB0, 0x4D, 0xE6, 0xE9,         // 'M'
    0xB0, 0x42, 0xE6, 0xE9,         // 'B'
    0xB0, 0x52, 0xE6, 0xE9,         // 'R'
    0xB0, 0x00, 0xE6, 0x71,         // select LBA 0
    0xB9, 0x00, 0x02,               // mov cx, 512
    0xB0, 0xCC, 0xE6, 0x70,         // mov al,0xCC ; out 0x70  (loop body)
    0x49, 0x75, 0xF9,               // dec cx ; jnz loop
    0xF4,                           // hlt
};

static std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

int main() {
    std::printf("MicroVM device-layer demo (roadmap step 2)\n");
    std::printf("  ------------------------------------------------------\n");

    std::wstring diffPath = ExeDir() + L"\\sandbox-disk.diff";

    MicroVm vm;
    if (!vm.Create(0x100000 /* 1 MiB */)) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str());
        std::printf("  (Is the hypervisor running? Try 'av vmcheck'.)\n");
        return 1;
    }

    PvConsole console(0xE9);
    PvDisk    disk(diffPath, /*lbaPort*/ 0x71, /*dataPort*/ 0x70);
    vm.MapIoPort(0xE9, &console);
    vm.MapIoPort(0x71, &disk);
    vm.MapIoPort(0x70, &disk);

    if (!vm.LoadRealModeCode(kGuest, sizeof(kGuest))) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str());
        return 1;
    }

    std::printf("  [ ok ] guest loaded; detonating...\n");
    bool halted = vm.Run();
    std::printf("  ------------------------------------------------------\n");
    if (!halted) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str());
        return 1;
    }

    std::printf("  guest halted cleanly after %llu device VM-exits\n\n",
                (unsigned long long)vm.ExitCount());

    std::printf("  PvConsole output : \"%s\"\n\n", console.Text().c_str());

    std::printf("  PvDisk change report (-> differential image, NOT a real drive):\n");
    for (const auto& w : disk.Writes()) {
        const char* tag = (w.lba == 0) ? "  <-- boot sector / MBR" : "";
        std::printf("    LBA %-6u %u bytes%s\n", w.lba, w.bytes, tag);
    }
    std::printf("    total written  : %llu bytes\n", (unsigned long long)disk.BytesWritten());
    std::printf("    image file     : %ls\n", disk.ImagePath().c_str());
    std::printf("    host disk opened: %s\n\n", disk.HostDiskTouched() ? "YES (bug!)" : "no");

    std::printf("  RESULT: the guest overwrote 'sector 0' exactly like MEMZ would.\n");
    std::printf("          It hit the throwaway diff image. Delete that file and the\n");
    std::printf("          damage is gone; your real disk was never touched.\n");
    return 0;
}
