// Only project-built CPU kernels and tiny instructions defined here execute.
#include "../src/microvm/boot/KernelBoot.h"
#include <cstring>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace av::microvm::boot;
namespace gc = guardian::cpu;
namespace {
void Check(bool b, const char* s) { if (!b) throw std::runtime_error(s); }
void Code(KernelBootSession& s, const KernelImage& image, std::initializer_list<uint8_t> code) {
    Check(code.size() < 64, "Test payload too large");
    std::memcpy(static_cast<uint8_t*>(s.Machine().RamData()) + image.Entry(), code.begin(), code.size());
}
}
int main(int argc, char** argv) {
    try {
        Check(argc == 5, "Normal, fatal, double-fault kernels and report path required");
        const auto image = KernelImage::Read(argv[1]), fatal = KernelImage::Read(argv[2]), df = KernelImage::Read(argv[3]);
        if (!WhpAvailable()) { std::cout << "WHP unavailable: CPU integration skipped\n"; return 77; }
        std::vector<BootResult> positive, negative; uint64_t id = 1;
        for (uint64_t ram : {16ull << 20, 64ull << 20}) for (unsigned n = 0; n < 20; ++n) {
            KernelBootSession s; s.Prepare(image, ram, id++, {true, true});
            const auto executionStart=std::chrono::steady_clock::now();const auto r=s.Run();
            const auto executionMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-executionStart).count();positive.push_back(r);
            if (!r.success) { WriteBootReport(argv[4], positive); throw std::runtime_error("CPU matrix failed: " + r.error); }
            const auto& c = r.cpu;
            Check(r.cpuReportValid && r.exceptionExitBitmap == 0 && c.checks == gc::RequiredChecks && c.userEntries == 10 &&
                c.userCompleted == 3 && c.userFaults == 6 && c.kernelFaults == 2 && c.returnRejects == 1 && c.faultCount == 9,
                "Guest CPU paths were not all exercised");
            Check(c.echoes == 3 && c.unknownSyscalls == 3 && c.syscalls >= 10 && c.timerTicks >= 4 && c.timerUserTicks >= 2 &&
                r.timerAcknowledged == c.timerTicks && r.timerQueued >= r.timerAcknowledged && r.timerQueued <= r.timerAcknowledged + 1,
                "Syscall or interrupt accounting mismatch");
            const bool clockValid=c.clockEndNs>c.clockStartNs&&c.deadlineObservedNs>=c.deadlineNs&&c.clockEndNs-c.clockStartNs>=4*gc::PeriodNs&&c.userProgress;
            if(!clockValid||r.timedOut||executionMs>=3000)WriteBootReport(argv[4],positive);
            Check(clockValid,"Monotonic time/deadline/forward progress failed");
            Check(!r.timedOut&&executionMs<3000,"CPU execution exceeded three-second budget (preparation excluded)");
            Check(c.faults[2].vector == 6 && c.faults[3].vector == 13 && c.faults[4].vector == 14 && c.faults[4].error == 5 &&
                c.faults[5].vector == 14 && c.faults[5].error == 6 && c.faults[6].vector == 13 && c.faults[7].error == 0x202 &&
                c.faults[8].vector == 0x100, "Guest fault cause/privilege/error code lost");
        }
        WriteBootReport(argv[4], positive);
        const auto scenario = [&](const char* name, const KernelImage& kernel, const std::function<void(KernelBootSession&)>& mutate,
                                  const std::function<bool(const BootResult&)>& verify, bool driveTimer = true) {
            KernelBootSession s; s.Prepare(kernel, 16ull << 20, id++, {true, driveTimer}); mutate(s);
            auto r = s.Run(driveTimer ? std::chrono::seconds(3) : std::chrono::milliseconds(80)); negative.push_back(r);
            if (r.success || !verify(r)) throw std::runtime_error(std::string("CPU failure case incorrect: ") + name + ": " + r.error);
            std::cout << "Contained " << name << ": " << r.error << '\n';
        };
        scenario("unhandled kernel #UD", fatal, [](auto&) {}, [](const auto& r) {
            return r.cpuReportValid && r.cpu.fatal && r.cpu.fatalFault.vector == 6 && r.cpu.fatalFault.cpl == 0 && r.stopStatus == 0xE006 && !r.timedOut;
        });
        scenario("double fault uses emergency IST", df, [](auto&) {}, [](const auto& r) {
            return r.cpuReportValid && r.cpu.fatal && r.cpu.fatalFault.vector == 8 && r.cpu.fatalFault.cpl == 0 && r.stopStatus == 0xE008 && !r.timedOut;
        });
        scenario("missing timer bounded by watchdog", image, [](auto&) {}, [](const auto& r) { return r.timedOut && !r.cpuReportValid; }, false);
        for (unsigned kind = 0; kind < 5; ++kind) {
            scenario("invalid CPU boot extension", image, [&](auto& s) {
                auto* b = reinterpret_cast<gc::Boot*>(static_cast<uint8_t*>(s.Machine().RamData()) + gc::BootGpa);
                if (kind == 0) b->magic = 0; else if (kind == 1) ++b->version; else if (kind == 2) b->bytes = 0;
                else if (kind == 3) b->reportGpa = UINT64_MAX; else b->reserved[0] = 1;
            }, [](const auto& r) { return !r.cpuReportValid && r.console.find("invalid CPU boot extension") != std::string::npos && !r.timedOut; });
        }
        scenario("write read-only clock", image, [&](auto& s) { Code(s, image, {0xB8,0,0x60,0x10,0,0xC6,0,1,0xF4}); },
            [](const auto& r) { return r.memoryFault && r.faultGpa == gc::ClockGpa; });
        for (uint8_t command : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(99)}) {
            scenario("CPU control state/format", image, [&](auto& s) { Code(s, image, {0xB0, command, 0xE6, 0xEC, 0xF4}); },
                [](const auto& r) { return !r.cpuReportValid && !r.timedOut && !r.error.empty(); });
        }
        scenario("CPU control IN rejected", image, [&](auto& s) { Code(s, image, {0xE4,0xEC,0xF4}); },
            [](const auto& r) { return r.error.find("scalar byte OUT") != std::string::npos; });
        scenario("CPU control wide OUT rejected", image, [&](auto& s) { Code(s, image, {0xE7,0xEC,0xF4}); },
            [](const auto& r) { return r.error.find("scalar byte OUT") != std::string::npos; });
        auto path = std::filesystem::path(argv[4]); path.replace_extension("negative.json"); WriteBootReport(path, negative);
        std::cout << "40 real guest CPU boots passed; " << negative.size() << " external negative scenarios contained\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
