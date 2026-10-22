#include "guardian/Cpu.h"
#include <iostream>
#include <stdexcept>
using namespace guardian::cpu;
namespace { void Check(bool b, const char* s) { if (!b) throw std::runtime_error(s); } }
int main() {
    try {
        const Boot original{BootMagic, Version, sizeof(Boot), 42, ClockGpa, ReportGpa, PeriodNs, TimerPort, TimerVector, {0,0,0,0,0}};
        Check(ValidBoot(original, 42), "Normal CPU boot rejected");
        for (unsigned field = 0; field < 10; ++field) {
            auto b = original;
            switch (field) {
            case 0: b.magic = 0; break; case 1: ++b.version; break; case 2: --b.bytes; break;
            case 3: b.bootId = 0; break; case 4: ++b.clockGpa; break; case 5: ++b.reportGpa; break;
            case 6: ++b.periodNs; break; case 7: ++b.timerPort; break; case 8: ++b.timerVector; break;
            default: b.reserved[4] = 1;
            }
            Check(!ValidBoot(b, 42), "Invalid CPU boot accepted");
        }
        Report normal{}; normal.magic = ReportMagic; normal.version = Version; normal.bytes = sizeof(Report); normal.bootId = 42;
        Check(ValidReport(normal, 42), "Normal CPU report rejected");
        for (unsigned field = 0; field < 10; ++field) {
            auto r = normal;
            switch (field) {
            case 0: r.magic = 0; break; case 1: ++r.version; break; case 2: --r.bytes; break;
            case 3: r.bootId = 0; break; case 4: r.checks = 0x800; break; case 5: r.faultCount = 13; break;
            case 6: r.fatal = 2; break; case 7: r.reserved[3] = 1; break;
            case 8: r.faultCount = 1; r.faults[0].cpl = 2; break;
            default: r.fatal = 1; r.fatalFault.cpl = 2;
            }
            Check(!ValidReport(r, 42), "Invalid CPU report accepted");
        }
        std::cout << "CPU extension and report: version/session/reserved fields/bounds/CPL verified\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
