// Hardware integration for MiniNt's existing syscall path. Only our tiny,
// generated test programs execute; no external executable or malware is loaded.
#include "../src/microvm/host/MiniNt.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace av::microvm;
namespace {
void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
constexpr uint64_t Entry = 0x2000, Result = 0x5000, Stack = 0xEFFFF8;
constexpr uint64_t Heap = 0xA00000, End = 0xE00000;
constexpr uint64_t Marker = 0x123456789ABCDEF0ull;
class Watchdog {
public:
    explicit Watchdog(MicroVm& vm) : worker_([this, &vm] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!done_) {
            if (std::chrono::steady_clock::now() >= deadline) vm.Cancel();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }) {}
    ~Watchdog() { done_ = true; worker_.join(); }
private:
    std::atomic<bool> done_{false};
    std::thread worker_;
};
struct Fixture {
    MicroVm vm;
    MiniNt kernel;
    GuestMemory memory{nullptr, 0};
    Fixture() {
        check(vm.Create(0x1000000, (1ull << 6) | (1ull << 13) | (1ull << 14)), "WHP VM creation failed");
        memory = GuestMemory(vm.RamData(), vm.RamSize());
        check(vm.SetupLongMode(Entry, Stack, true), "ring-3 setup failed");
        vm.OnSyscall([&](MicroVm& m) { kernel.Dispatch(m); });
        check(kernel.SsnOf("NtTerminateProcess") >= 0, "termination syscall missing from this host ntdll");
    }
    static void append32(std::vector<uint8_t>& c, uint32_t value) {
        for (int i = 0; i < 4; ++i) c.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
    static void append64(std::vector<uint8_t>& c, uint64_t value) {
        for (int i = 0; i < 8; ++i) c.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
    uint64_t Run(std::vector<uint8_t> code, uint64_t rcx = 0, uint64_t rdx = 0, uint64_t r8 = 0, uint64_t r9 = 0) {
        code.insert(code.end(), {0x48, 0xA3}); append64(code, Result); // mov [Result],rax
        code.insert(code.end(), {0x48, 0xC7, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0x31, 0xD2, 0xB8});
        append32(code, static_cast<uint32_t>(kernel.SsnOf("NtTerminateProcess")));
        code.insert(code.end(), {0x0F, 0x05, 0x0F, 0x0B}); // terminate via syscall; ud2 if it returns
        check(memory.Contains(Entry, code.size()), "test code outside guest RAM");
        std::memcpy(static_cast<uint8_t*>(vm.RamData()) + Entry, code.data(), code.size());
        memory.Write(Result, uint64_t(0));
        const WHV_REGISTER_NAME names[]{WHvX64RegisterRip, WHvX64RegisterRsp, WHvX64RegisterRflags,
            WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterR8, WHvX64RegisterR9};
        WHV_REGISTER_VALUE values[7]{};
        const uint64_t numbers[]{Entry, Stack, 2, rcx, rdx, r8, r9};
        for (size_t i = 0; i < 7; ++i) values[i].Reg64 = numbers[i];
        check(vm.SetRegisters(names, 7, values), "test registers failed");
        kernel.ResetTransient();
        {
            Watchdog watchdog(vm);
            if (!vm.Run(100)) {
                std::wcerr << vm.Error() << '\n';
                throw std::runtime_error("guest test did not complete");
            }
        }
        check(kernel.ExitCode() == 0, "guest did not terminate successfully");
        uint64_t value = 0; check(memory.Read(Result, value), "result not in guest RAM"); return value;
    }
    uint64_t Call(uint32_t op, uint64_t handle, uint64_t flags, uint64_t operand, uint64_t bytes = 0) {
        std::vector<uint8_t> code{0xB8}; append32(code, op);
        code.insert(code.end(), {0x0F, 0x05});
        return Run(std::move(code), handle, flags, operand, bytes);
    }
    uint64_t GuestValue(uint64_t pointer, bool write = false) {
        std::vector<uint8_t> code{0x49, 0xBB}; append64(code, pointer); // mov r11,pointer
        if (write) {
            code.insert(code.end(), {0x49, 0xBA}); append64(code, Marker);
            code.insert(code.end(), {0x4D, 0x89, 0x13}); // mov [r11],r10
        }
        code.insert(code.end(), {0x49, 0x8B, 0x03}); // mov rax,[r11]
        return Run(std::move(code));
    }
};
}
int main() {
    BOOL available = FALSE; UINT32 written = 0;
    if (FAILED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &available, sizeof(available), &written)) || !available) {
        std::cout << "SKIP: Windows Hypervisor Platform is unavailable; no settings changed\n"; return 77;
    }
    try {
        Fixture f;
        check(f.Call(0xF0000003, 0, 0, 0) == 0, "unconfigured heap returned a fabricated handle");
        f.kernel.SetHeapRegion(Heap, End);
        check(f.Call(0xF0000003, 0, 0, 0) == Heap, "configured heap handle mismatch");
        const auto a = f.Call(0xF0000001, Heap, 8, 64);
        const auto blocker = f.Call(0xF0000001, Heap, 0, 64);
        check(a == Heap + 0x1000 && blocker == a + 64, "guest allocation layout mismatch");
        check(f.GuestValue(a) == 0 && f.GuestValue(a, true) == Marker, "guest could not access allocated memory");
        check(f.Call(0xF0000004, Heap, 0x10, a, 256) == 0, "in-place-only moved an allocation");
        check(f.Call(0xF0000004, Heap, 8, a, UINT64_MAX) == 0 && f.GuestValue(a) == Marker, "overflow damaged original allocation");
        const auto resized = f.Call(0xF0000004, Heap, 8, a, 256);
        check(resized && resized != a && f.GuestValue(resized) == Marker, "guest resize did not preserve data");
        check(f.GuestValue(resized + 64) == 0, "guest resized tail was not zeroed");
        check(f.Call(0xF0000005, Heap, 0, resized) == 256, "guest heap size mismatch");
        check(f.Call(0xF0000002, Heap, 0, a) == 0, "moved old block still allocated");
        check(f.Call(0xF0000002, Heap + 16, 0, resized) == 0, "wrong heap handle freed a block");
        check(f.Call(0xF0000002, Heap, 0, resized + 1) == 0, "interior pointer freed a block");
        check(f.Call(0xF0000002, Heap, 0, resized) == 1 && f.Call(0xF0000002, Heap, 0, resized) == 0, "guest double free not rejected");
        check(f.Call(0xF0000005, Heap, 0, resized) == UINT64_MAX, "freed size returned success");
        check(f.Call(0xF0000002, Heap, 0, blocker) == 1, "blocker not released");
        const auto full = f.Call(0xF0000001, Heap, 0, End - Heap - 0x1000);
        check(full == Heap + 0x1000, "guest free/coalescing did not recover arena");
        check(f.Call(0xF0000001, Heap, 0, 16) == 0, "guest over-allocated arena");
        check(f.Call(0xF0000002, Heap, 0, full) == 1, "guest full allocation not freed");
        std::cout << "PASS: ring-3 code -> SYSCALL/#UD -> existing MiniNt -> GuestHeap; guest writes/reads, reuse, ownership, free, resize and overflow rejection; WHP only, no QEMU\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
