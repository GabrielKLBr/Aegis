// VmSyscall.cpp - the first brick of the "Wine in reverse" NT personality.
//
// A 64-bit guest runs *natively* inside the WHP partition and makes a "syscall"
// by writing to a magic doorbell port. The host catches the VM-exit and SERVICES
// the syscall in its own synthetic kernel -- reading the guest's registers and
// memory, doing the work (here: NtWriteConsole), and returning. No real Windows
// kernel is involved; we are the kernel. This is the mechanism the whole
// compatibility layer is built on: every Win32/NT call a PE makes will arrive
// here for us to implement against virtual files, registry and fake devices.
//
// Note: this PoC runs the guest at CPL0 and uses a port doorbell instead of the
// real SYSCALL instruction (which we will trap via LSTAR later). The contract --
// "guest code runs native, the kernel side is ours" -- is the same.
//
// Build target: vmsyscall.exe  (links WinHvPlatform).

#include "vmm/MicroVmMonitor.h"

#include <cstdio>
#include <string>

using namespace av::microvm;

// Guest-physical layout (identity-mapped, so guest VA == GPA).
static constexpr uint64_t kEntry = 0x2000;  // code
static constexpr uint64_t kData  = 0x3000;  // string argument
static constexpr uint64_t kStack = 0x6FF00; // below the page tables at 0x70000
static constexpr uint16_t kSyscallPort = 0x7F;

// Our tiny syscall numbers (the seed of the NT syscall table).
enum : uint64_t { SYS_WriteConsole = 1 };

static uint64_t Reg(MicroVm& vm, WHV_REGISTER_NAME n) {
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Reg64;
}

int main() {
    std::printf("MicroVM NT-personality demo (facing the wall: \"Wine in reverse\")\n");
    std::printf("  ------------------------------------------------------\n");

    const std::string msg =
        "hello from a 64-bit guest, serviced entirely by our synthetic NT kernel";

    MicroVm vm;
    if (!vm.Create(0x100000 /* 1 MiB */)) {
        std::printf("  [FAIL] %ls  (try 'av vmcheck')\n", vm.Error().c_str());
        return 1;
    }

    auto* mem = static_cast<uint8_t*>(vm.RamData());
    CopyMemory(mem + kData, msg.data(), msg.size());

    // 64-bit guest:  rax=SYS_WriteConsole; rdi=ptr; rsi=len; out 0x7F,al; hlt
    const uint32_t len = static_cast<uint32_t>(msg.size());
    const unsigned char code[] = {
        0xB8, (unsigned char)SYS_WriteConsole, 0x00, 0x00, 0x00,        // mov eax, 1
        0xBF, (unsigned char)(kData & 0xFF), (unsigned char)((kData >> 8) & 0xFF), 0x00, 0x00, // mov edi, 0x3000
        0xBE, (unsigned char)(len & 0xFF), (unsigned char)((len >> 8) & 0xFF), 0x00, 0x00,     // mov esi, len
        0xE6, kSyscallPort,                                              // out 0x7F, al
        0xF4,                                                           // hlt
    };
    CopyMemory(mem + kEntry, code, sizeof(code));

    // The "kernel": service the syscall doorbell.
    int serviced = 0;
    std::string captured;
    vm.MapIoTrap(kSyscallPort, [&](MicroVm& m) {
        uint64_t sysno = Reg(m, WHvX64RegisterRax);
        uint64_t ptr   = Reg(m, WHvX64RegisterRdi);
        uint64_t n     = Reg(m, WHvX64RegisterRsi);
        std::printf("  [trap ] guest syscall #%llu  (rdi=0x%llX, rsi=%llu)\n",
                    (unsigned long long)sysno, (unsigned long long)ptr,
                    (unsigned long long)n);
        switch (sysno) {
            case SYS_WriteConsole: {
                const char* base = static_cast<const char*>(m.RamData());
                if (ptr + n <= m.RamSize()) captured.assign(base + ptr, n);
                std::printf("  [kern ] NtWriteConsole serviced from guest memory\n");
                ++serviced;
                // Return value -> RAX (0 = STATUS_SUCCESS).
                WHV_REGISTER_NAME rn = WHvX64RegisterRax;
                WHV_REGISTER_VALUE rv; ZeroMemory(&rv, sizeof(rv));
                m.SetRegisters(&rn, 1, &rv);
                break;
            }
            default:
                std::printf("  [kern ] unimplemented syscall #%llu\n",
                            (unsigned long long)sysno);
        }
    });

    if (!vm.SetupLongMode(kEntry, kStack)) {
        std::printf("  [FAIL] %ls\n", vm.Error().c_str());
        return 1;
    }
    std::printf("  [ ok  ] vCPU is in 64-bit long mode; entry at 0x%llX\n",
                (unsigned long long)kEntry);
    std::printf("  ------------------------------------------------------\n");

    bool halted = vm.Run();
    std::printf("  ------------------------------------------------------\n");
    if (!halted) { std::printf("  [FAIL] %ls\n", vm.Error().c_str()); return 1; }

    std::printf("  guest halted after %llu syscall(s)\n", (unsigned long long)vm.ExitCount());
    std::printf("  kernel captured  : \"%s\"\n\n", captured.c_str());

    bool ok = serviced == 1 && captured == msg;
    std::printf("  RESULT: %s\n", ok
        ? "the guest ran native 64-bit code and called into OUR kernel.\n"
          "          This is the syscall seam the whole NT personality grows from."
        : "syscall path did not complete as expected.");
    return ok ? 0 : 1;
}
