#include "../src/microvm/boot/KernelBoot.h"
#include "../src/microvm/state/Files.h"
#include "fixtures/phase9/SnapshotProbe.h"
#include <cstring>
#include <iostream>
#include <thread>
using namespace av::microvm;
using namespace av::microvm::boot;
namespace {
unsigned checks = 0;
void Check(bool ok) {
    ++checks;
    if (!ok)
        throw std::runtime_error("Snapshot check " + std::to_string(checks));
}
bool ProbeRun(MicroVm &vm) {
    std::jthread watchdog([&](std::stop_token stop) {
        for (unsigned n = 0; n < 200 && !stop.stop_requested(); ++n)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!stop.stop_requested())
            vm.Cancel();
    });
    const auto ok = vm.Run(100);
    watchdog.request_stop();
    return ok;
}
template <class F> void Reject(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    Check(rejected);
}
void IoWidthAndMemoryIsolation() {
    MicroVm first,second;
    Check(first.Create(16<<20) && second.Create(16<<20));
    constexpr uint64_t marker=0x1122334455667788ull;
    auto* a=static_cast<uint8_t*>(first.RamData());auto* b=static_cast<uint8_t*>(second.RamData());
    std::memcpy(a+0x300000,&marker,8);const auto other=~marker;std::memcpy(b+0x300000,&other,8);
    Check(std::memcmp(a+0x300000,b+0x300000,8)!=0);
    Check(second.SetMemoryAccess(0x300000,4096,1));
    Check(first.CanAccessMemory(0x300000,8,3) && !second.CanAccessMemory(0x300000,8,2));
    // AL and AX deliberately retain nonzero high bits in RAX. OUT transfers
    // only the selected width; device handlers must never see those high bits.
    const uint8_t program[]{0x48,0xB8,0x11,0x00,0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,
        0xE6,0xEA,0x66,0xB8,0x33,0x22,0x66,0xE7,0xEB,
        0xB8,0x77,0x66,0x55,0x44,0xE7,0xEC,0xF4};
    std::memcpy(a+0x200000,program,sizeof(program));Check(first.SetupLongMode(0x200000,0x500000));
    unsigned seen=0;for(uint16_t port=0xEA;port<=0xEC;++port)first.MapIoTrapDetailed(port,[&](auto& vm,const auto& io){
        const uint32_t expected[]{0x11,0x2233,0x44556677};const uint8_t widths[]{1,2,4};
        Check(seen<3 && uint32_t(io.port)==0xEAu+seen && io.bytes==widths[seen] && io.value==expected[seen] && io.write && !io.string && !io.repeat);++seen;
        // HLT waits for interrupts on KVM. Finish this finite probe explicitly
        // after observing all three transfers on either backend.
        if(seen==3)vm.RequestStop();
    });
    Check(ProbeRun(first) && seen==3);uint64_t after=0;std::memcpy(&after,b+0x300000,8);Check(after==other);
    std::cout<<"Scalar byte word dword I/O and independent VM RAM permissions PASS\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 3);
        IoWidthAndMemoryIsolation();
        std::vector<BootResult> completed;
        const auto image = KernelImage::Read(argv[1]);
        const auto temp = std::filesystem::absolute(argv[2]) / state::Token();
        std::filesystem::create_directories(temp);
        CpuOptions options{true, true, false, false, true, true, true};
        {
            state::MachineState saved;
            {
                MicroVm vm;
                Check(vm.Create(16 << 20));
                Check(vm.SetupLongMode(0x200000, 0x500000));
                auto *ram = static_cast<uint8_t *>(vm.RamData());
                std::memcpy(ram + 0x200000, guardian::test::SnapshotProbe,
                            sizeof(guardian::test::SnapshotProbe));
                const uint64_t handler = 0x200000 + guardian::test::SnapshotIrqOffset;
                const uint64_t gate[2] = {(handler & 65535) | (uint64_t(8) << 16) | (uint64_t(0x8E) << 40) |
                                              ((handler & 0xFFFF0000ull) << 32),
                                          handler >> 32};
                std::memcpy(ram + 0x310000 + 0x40 * 16, gate, sizeof(gate));
                const uint16_t idtLimit = 4095, gdtLimit = 23;
                const uint64_t idtBase = 0x310000, gdtBase = 0x310900;
                std::memcpy(ram + 0x310800, &idtLimit, 2);
                std::memcpy(ram + 0x310802, &idtBase, 8);
                std::memcpy(ram + 0x310810, &gdtLimit, 2);
                std::memcpy(ram + 0x310812, &gdtBase, 8);
                const uint64_t gdt[3] = {0, 0x00AF9A000000FFFFull, 0x00CF92000000FFFFull};
                std::memcpy(ram + gdtBase, gdt, sizeof(gdt));
                for (uint64_t i = 0; i < 3; ++i) {
                    const uint64_t marker = 0xABC000 + i;
                    std::memcpy(ram + 0x320000 + i * 4096, &marker, 8);
                }
                Check(vm.SetMemoryAccess(0, 16 << 20, 3) && vm.SetMemoryAccess(0x200000, 4096, 5) &&
                      vm.SetMemoryAccess(0x800000, 4096, 0) && vm.SetMemoryAccess(0x801000, 4096, 1));
                vm.MapIoTrapDetailed(0xEE, [](auto &machine, const auto &) { machine.RequestPause(); });
                const auto ok = ProbeRun(vm);
                if (!ok || !vm.Paused())
                    std::wcerr << L"Architectural probe: " << vm.Error() << L" RIP " << vm.LastExceptionRip()
                               << L"\n";
                Check(ok && vm.Paused());
#if defined(__linux__)
                Check(vm.TimerInterrupt(true));
#else
                WHV_REGISTER_VALUE irq{};
                irq.PendingInterruption.InterruptionPending = 1;
                irq.PendingInterruption.InterruptionType = WHvX64PendingInterrupt;
                irq.PendingInterruption.InterruptionVector = 0x40;
                const auto name = WHvRegisterPendingInterruption;
                const bool pending = vm.SetRegisters(&name, 1, &irq);
                if (!pending) std::wcerr << L"Set pending snapshot IRQ: " << vm.Error() << L'\n';
                Check(pending);
#endif
                Check(vm.Capture(saved));
            }
#ifdef _WIN32
            {
                // Read-only Hyper-V activity summaries must be validated, not
                // blindly written back or silently dropped from checkpoints.
                auto unsupported=saved;uint32_t count=0;std::memcpy(&count,unsupported.processor.data()+8,4);
                Check(count>0 && 12+uint64_t(count)*sizeof(WHV_REGISTER_VALUE)<=unsupported.processor.size());
                const uint64_t halted=2;std::memcpy(unsupported.processor.data()+12+(count-1)*sizeof(WHV_REGISTER_VALUE),&halted,8);
                MicroVm rejected;Check(rejected.Create(16<<20));Reject([&]{rejected.Restore(unsupported);});
            }
#endif
            MicroVm resumed;
            const bool restored=resumed.Create(16 << 20) && resumed.Restore(saved);
            if(!restored)std::wcerr<<L"Restore architectural CPU: "<<resumed.Error()<<L'\n';
            Check(restored);
            resumed.MapIoTrapDetailed(0xEF, [](auto &vm, const auto &) { vm.RequestStop(); });
            const auto ok = ProbeRun(resumed);
            if (!ok)
                std::wcerr << L"Restored architectural probe: " << resumed.Error() << L" RIP "
                           << resumed.LastExceptionRip() << L"\n";
            Check(ok);
            const auto *output = static_cast<const uint8_t *>(resumed.RamData()) + 0x300000;
            const uint64_t expected[] = {0x1122334455667788ull,
                                         0x2233445566778899ull,
                                         0x33445566778899AAull,
                                         0x445566778899AABBull,
                                         0x500000,
                                         0x5566778899AABBCCull,
                                         0x66778899AABBCCDDull,
                                         0x778899AABBCCDDEEull,
                                         0x8899AABBCCDDEEFFull,
                                         0x99AABBCCDDEEFF00ull,
                                         0xAABBCCDDEEFF0011ull,
                                         0x300000,
                                         0xBBCCDDEEFF001122ull,
                                         0xCCDDEEFF00112233ull,
                                         0xDDEEFF0011223344ull,
                                         0xEEFF001122334455ull};
            for (uint32_t i = 0; i < 16; ++i) {
                uint64_t value = 0;
                std::memcpy(&value, output + i * 8, 8);
                Check(value == expected[i]);
            }
            for (uint32_t i = 0; i < 16; ++i)
                Check(output[144 + i] == i + 1 && output[160 + i] == i + 1);
            uint64_t flags = 0, fp = 0, irq = 0;
            uint32_t mxcsr = 0;
            std::memcpy(&flags, output + 128, 8);
            std::memcpy(&fp, output + 176, 8);
            std::memcpy(&mxcsr, output + 188, 4);
            std::memcpy(&irq, output + 216, 8);
            Check((flags & 1) && fp == 0x3FF0000000000000ull && mxcsr == 0x1F80 && irq == 1);
            for (uint64_t i = 0; i < 3; ++i) {
                uint64_t value = 0;
                std::memcpy(&value, output + 192 + i * 8, 8);
                Check(value == 0xABC000 + i);
            }
            uint32_t first = 0, second = 0;
            std::memcpy(&first, output + 224, 4);
            std::memcpy(&second, output + 240, 4);
            Check(first == second);
            std::memcpy(&first, output + 232, 4);
            std::memcpy(&second, output + 248, 4);
            Check(first == second);
            state::MachineState after;
            Check(resumed.Capture(after) && after.access == saved.access);
            std::cout << "x64 GPR XMM x87 FS GS CPUID pending IRQ and physical mappings restored PASS\n"
                      << std::flush;
        }
        for (const uint64_t ramMiB : {uint64_t(16), uint64_t(32), uint64_t(64)}) {
            const auto pointDir = temp / std::to_string(ramMiB);
            std::filesystem::create_directories(pointDir);
            for (uint64_t checkpoint : {uint64_t(100), uint64_t(180), uint64_t(230)}) {
                SessionSnapshot saved;
                {
                    KernelBootSession s;
                    s.Prepare(image, ramMiB << 20, 700 + checkpoint, options,
                              {pointDir / ("base" + std::to_string(checkpoint)),
                               pointDir / ("overlay" + std::to_string(checkpoint)), true});
                    s.PauseAtIoRequest(checkpoint);
                    const auto r = s.Run(std::chrono::seconds(10));
                    completed.push_back(r);
                    if (!r.paused)
                        std::cerr << r.error << '\n';
                    Check(r.paused && !r.success && !r.timedOut && r.error.empty());
                    saved = s.Capture();
                }
                const auto encoded = saved.Encode();
                auto decoded = SessionSnapshot::Decode(encoded);
                Check(decoded.metadata == saved.metadata && decoded.disk == saved.disk &&
                      decoded.machine.ram == saved.machine.ram);
                for (size_t at :
                     {size_t(0), size_t(8), size_t(16), size_t(28), encoded.size() / 2, encoded.size() - 1}) {
                    auto bad = encoded;
                    bad[at] ^= 1;
                    Reject([&] { SessionSnapshot::Decode(bad); });
                }
                auto truncated = encoded;
                truncated.pop_back();
                Reject([&] { SessionSnapshot::Decode(truncated); });
                for (uint32_t branch = 0; branch < 2; ++branch) {
                    const auto overlay =
                        pointDir / ("branch-" + std::to_string(checkpoint) + "-" + std::to_string(branch));
                    state::AtomicWrite(overlay, saved.disk);
                    KernelBootSession s;
                    s.Prepare(image, ramMiB << 20, saved.BootId(), options,
                              {pointDir / ("base" + std::to_string(checkpoint)), overlay, false});
                    s.Restore(decoded);
                    const auto same = s.Capture();
                    Check(same.machine.ram == saved.machine.ram &&
                          same.machine.access == saved.machine.access && same.metadata == saved.metadata &&
                          same.disk == saved.disk);
                    const auto r = s.Run(std::chrono::seconds(10));
                    completed.push_back(r);
                    if (!r.success)
                        std::cerr << r.error << '\n';
                    Check(r.success && !r.paused && !r.processes.liveObjects && !r.processes.liveHandles &&
                          !r.processes.liveReferences);
                    Check(r.console.find("PE dynamic DLL TLS references rollback unload PASS") !=
                              std::string::npos &&
                          r.storage.sequenceAfter > r.storage.sequenceBefore);
                }
                const auto cold = pointDir / ("cold-" + std::to_string(checkpoint));
                state::AtomicWrite(cold, saved.disk);
                {
                    KernelBootSession s;
                    s.Prepare(image, ramMiB << 20, saved.BootId() + 10000, options,
                              {pointDir / ("base" + std::to_string(checkpoint)), cold, false});
                    const auto r = s.Run(std::chrono::seconds(10));
                    completed.push_back(r);
                    if (!r.success)
                        std::cerr << r.error << '\n';
                    Check(r.success);
                    Check(r.storage.sequenceAfter > r.storage.sequenceBefore);
                }
                Check(state::ReadFile(pointDir / ("base" + std::to_string(checkpoint))) ==
                      std::vector<uint8_t>(65536));
            }
        }
        const auto durable = temp / "durable";
        const std::vector<uint8_t> old{1, 2, 3}, fresh{4, 5, 6};
        state::AtomicWrite(durable, old);
        for (int step = 0; step < 4; ++step) {
            state::FailAfter(step);
            Reject([&] { state::AtomicWrite(durable, fresh); });
            state::FailAfter(-1);
            Check(state::ReadFile(durable) == old);
        }
        state::AtomicWrite(durable, fresh);
        Check(state::ReadFile(durable) == fresh);
        {
            state::Lease first(temp / "lease");
            state::Lease second(temp / "lease", true);
            Check(first.Held() && !second.Held());
        }
        {
            state::Lease third(temp / "lease", true);
            Check(third.Held());
        }
        WriteBootReport(std::filesystem::absolute(argv[2]) / "snapshot-boots.json", completed);
        std::filesystem::remove_all(temp);
        std::cout << checks
                  << " complete session snapshot, disk branching, corruption and transaction checks PASS\n";
        return 0;
    } catch (const std::exception &e) {
        state::FailAfter(-1);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
