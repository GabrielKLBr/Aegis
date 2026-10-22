#pragma once
// Real WHP tests of the existing MiniNt ABI and SLAT permission enforcement.
// The guest programs are generated here; no external sample is executed.
#include "../src/microvm/host/MiniNt.h"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace av::microvm;
using M = GuestVirtualMemory;
namespace {
void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
constexpr uint64_t Entry = 0x2000, Result = 0x5000, Stack = 0xEFFFF8;
constexpr uint64_t BaseSlot = 0x6000, SizeSlot = 0x6010, OldSlot = 0x6020, Info = 0x6100, ReturnLength = 0x6200;
constexpr uint64_t Marker = 0x123456789ABCDEF0ull;
class Watchdog {
    std::atomic<bool> done_{false}; std::thread thread_;
public:
    explicit Watchdog(MicroVm& vm) : thread_([this, &vm] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!done_) {
            if (std::chrono::steady_clock::now() >= deadline) vm.Cancel();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }) {}
    ~Watchdog() { done_ = true; thread_.join(); }
};
struct Fixture {
    MicroVm vm; MiniNt kernel; GuestMemory memory{nullptr, 0};
    Fixture() {
        if (!vm.Create(0x1000000, (1ull << 6) | (1ull << 13) | (1ull << 14))) {
            std::wcerr << vm.Error() << '\n';
            throw std::runtime_error("WHP creation failed");
        }
        memory = GuestMemory(vm.RamData(), vm.RamSize());
        check(vm.SetupLongMode(Entry, Stack, true), "ring-3 setup failed");
        vm.OnSyscall([&](MicroVm& current) { kernel.Dispatch(current); });
    }
    template<class T = uint64_t> T Read(uint64_t p) { T value{}; check(memory.Read(p, value), "fixture read outside RAM"); return value; }
    static void U32(std::vector<uint8_t>& c, uint32_t v) { for (int i = 0; i < 4; ++i) c.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    static void U64(std::vector<uint8_t>& c, uint64_t v) { for (int i = 0; i < 8; ++i) c.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    uint64_t Run(std::vector<uint8_t> code, std::array<uint64_t, 4> arguments = {},
                 std::vector<uint64_t> stack = {}, int denied = -1, uint64_t deniedAddress = 0) {
        code.insert(code.end(), {0x48, 0xA3}); U64(code, Result);
        code.insert(code.end(), {0x48, 0xC7, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0x31, 0xD2, 0xB8});
        const auto terminate = kernel.SsnOf("NtTerminateProcess"); check(terminate >= 0, "termination SSN unavailable");
        U32(code, static_cast<uint32_t>(terminate)); code.insert(code.end(), {0x0F, 0x05, 0x0F, 0x0B});
        check(memory.Contains(Entry, code.size()), "test code outside RAM");
        std::memcpy(static_cast<uint8_t*>(vm.RamData()) + Entry, code.data(), code.size());
        memory.Write(Result, uint64_t(0));
        for (unsigned i = 0; i < 8; ++i) memory.Write(Stack + 0x28 + i * 8, i < stack.size() ? stack[i] : uint64_t(0));
        const WHV_REGISTER_NAME names[]{WHvX64RegisterRip, WHvX64RegisterRsp, WHvX64RegisterRflags,
            WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterR8, WHvX64RegisterR9};
        WHV_REGISTER_VALUE values[7]{};
        values[0].Reg64 = Entry; values[1].Reg64 = Stack; values[2].Reg64 = 2;
        for (size_t i = 0; i < 4; ++i) values[i + 3].Reg64 = arguments[i];
        check(vm.SetRegisters(names, 7, values), "register reset failed");
        kernel.ResetTransient(); bool ran = false;
        { Watchdog watchdog(vm); ran = vm.Run(100); }
        if (denied >= 0) {
            check(!ran && vm.LastMemoryAccessFault(), "CPU access was not blocked by WHP");
            check(vm.LastMemoryAccessType() == denied && vm.LastMemoryAccessGpa() == deniedAddress, "unexpected denied access context");
            std::cout << "  blocked access=" << denied << " GPA=0x" << std::hex << deniedAddress << std::dec << '\n';
            return 0;
        }
        if (!ran) { std::wcerr << vm.Error() << '\n'; throw std::runtime_error("guest did not complete"); }
        check(kernel.ExitCode() == 0 && !vm.LastMemoryAccessFault(), "guest termination/state incorrect");
        return Read(Result);
    }
    uint32_t Call(const char* name, std::array<uint64_t, 4> arguments, std::vector<uint64_t> stack = {}) {
        const auto number = kernel.SsnOf(name); check(number >= 0, "required SSN unavailable");
        std::vector<uint8_t> code{0xB8}; U32(code, static_cast<uint32_t>(number)); code.insert(code.end(), {0x0F, 0x05});
        return static_cast<uint32_t>(Run(std::move(code), arguments, std::move(stack)));
    }
    uint32_t Allocate(uint64_t address, uint64_t bytes, uint32_t flags, uint32_t protect) {
        memory.Write(BaseSlot, address); memory.Write(SizeSlot, bytes);
        return Call("NtAllocateVirtualMemory", {UINT64_MAX, BaseSlot, 0, SizeSlot}, {flags, protect});
    }
    uint32_t Protect(uint64_t address, uint64_t bytes, uint32_t protect) {
        memory.Write(BaseSlot, address); memory.Write(SizeSlot, bytes);
        return Call("NtProtectVirtualMemory", {UINT64_MAX, BaseSlot, SizeSlot, protect}, {OldSlot});
    }
    uint32_t Free(uint64_t address, uint64_t bytes, uint32_t flags) {
        memory.Write(BaseSlot, address); memory.Write(SizeSlot, bytes);
        return Call("NtFreeVirtualMemory", {UINT64_MAX, BaseSlot, SizeSlot, flags});
    }
    M::BasicInformation Query(uint64_t address) {
        check(Call("NtQueryVirtualMemory", {UINT64_MAX, address, 0, Info}, {48, ReturnLength}) == 0, "query failed");
        check(Read(ReturnLength) == 48, "query return length incorrect");
        return Read<M::BasicInformation>(Info);
    }
    uint64_t Probe(uint64_t address, int kind, int denied = -1) {
        std::vector<uint8_t> code;
        if (kind == 2) {
            code = {0x48, 0xB8}; U64(code, address); code.insert(code.end(), {0xFF, 0xD0});
        } else {
            code = {0x49, 0xBB}; U64(code, address);
            if (kind == 1) { code.insert(code.end(), {0x49, 0xBA}); U64(code, Marker); code.insert(code.end(), {0x4D, 0x89, 0x13}); }
            code.insert(code.end(), {0x49, 0x8B, 0x03});
        }
        return Run(std::move(code), {}, {}, denied, address);
    }
};
}
