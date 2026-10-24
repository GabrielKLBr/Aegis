#include "GraphicsDriver.h"
#include "guardian/Cpu.h"
#include <intrin.h>
namespace {
using namespace guardian::graphics;
Desktop desktop;
Info info{};
Report report{};
uint64_t sequence = 0;
void Copy(void *to, const void *from, uint32_t n) noexcept {
    auto *d = static_cast<volatile uint8_t *>(to);
    auto *s = static_cast<const volatile uint8_t *>(from);
    for (uint32_t i = 0; i < n; ++i)
        d[i] = s[i];
}
Status Exchange(Op op, Event &event) noexcept {
    Packet p{};
    p.boot = info.boot;
    p.sequence = ++sequence;
    p.op = op;
    if (op == Op::Present) {
        desktop.Render(reinterpret_cast<uint32_t *>(PixelsGpa));
        p.frame = desktop.Metadata();
    }
    Copy(reinterpret_cast<void *>(PacketGpa), &p, sizeof(p));
    _ReadWriteBarrier();
    __outbyte(Port, 1);
    Packet r{};
    Copy(&r, reinterpret_cast<const void *>(PacketGpa), sizeof(r));
    if (r.magic != Magic || r.boot != info.boot || r.version != Version || r.bytes != sizeof(r) ||
        r.sequence != sequence || r.op != op || r.reserved[0] || r.reserved[1] || uint32_t(r.status) > 6)
        return Status::Invalid;
    if (r.status == Status::Ok) {
        if (op == Op::Present)
            ++report.frames;
        else {
            event = r.event;
            ++report.inputs;
            if (!ValidInput(event))
                return Status::Invalid;
        }
    }
    return r.status;
}
} // namespace
uint32_t GraphicsExecute(guardian::graphics::Command& c,guardian::graphics::Event& e) noexcept {
    using namespace guardian::graphics;
    ++report.calls;
    Status result;
    if (c.op == Op::Poll) {
        result = desktop.Execute(c, e);
        if (result == Status::Empty) {
            Event raw{};
            result = Exchange(Op::Poll, raw);
            if (result == Status::Ok) {
                result = desktop.Inject(raw);
                if (result == Status::Ok)
                    result = desktop.Execute(c, e);
                else if (result == Status::Denied)
                    result = Status::Empty;
            }
        }
    } else if (c.op == Op::Present)
        result = Exchange(Op::Present, e);
    else
        result = desktop.Execute(c, e);
    if (result == Status::Denied)
        ++report.denied;
    return uint32_t(result);
}
uint32_t GraphicsSyscall(uint64_t command, uint64_t address) noexcept {
    using namespace guardian::graphics;
    constexpr auto begin = guardian::cpu::UserData + 0x100, end = guardian::cpu::UserData + 4096;
    if (command != 0xA0 || address < begin || address > end ||
        sizeof(Command) + sizeof(Event) > end - address){
        ++report.calls;
        return uint32_t(Status::Invalid);
    }
    Command c{};Event e{};
    Copy(&c,reinterpret_cast<const void*>(address),sizeof(c));
    const auto result=GraphicsExecute(c,e);
    Copy(reinterpret_cast<void *>(address), &c, sizeof(c));
    Copy(reinterpret_cast<void *>(address + sizeof(c)), &e, sizeof(e));
    return result;
}
void GraphicsInitialize() noexcept {
    using namespace guardian::graphics;
    Copy(&info,reinterpret_cast<const void*>(InfoGpa),sizeof(info));
    desktop={};sequence=0;
    report={Magic,info.boot,Version,sizeof(report),0,0,0,0,0,0,0};
}
void GraphicsComplete() noexcept {
    report.checks=1;report.applications=1;
    report.pixelHash=guardian::graphics::PixelHash(reinterpret_cast<const uint32_t*>(guardian::graphics::PixelsGpa));
}
void GraphicsSelfTest(void (*check)(bool, uint32_t) noexcept) noexcept {
    using namespace guardian::graphics;
    const auto *boot = reinterpret_cast<const guardian::abi::BootInfo *>(0x100000);
    Copy(&info, reinterpret_cast<const void *>(InfoGpa), sizeof(info));
    check(info.magic == Magic && info.boot == boot->bootId && info.version == Version &&
              info.bytes == sizeof(info) && info.width == Width && info.height == Height &&
              !info.permissions && info.interactive <= 1,
          0x1901);
    report = {Magic, info.boot, Version, sizeof(report), 0, 0, 0, 0, 0, 0, 0};
    *reinterpret_cast<volatile uint64_t *>(guardian::cpu::UserData + 0x60) = info.interactive;
    check(GraphicsRunUser() == 0, 0x1902);
    check(report.calls >= 20 && report.frames >= 2 && report.denied >= 6, 0x1903);
    const auto f = desktop.Metadata();
    check(f.count == 2 && ValidFrame(f), 0x1904);
    report.pixelHash = PixelHash(reinterpret_cast<const uint32_t *>(PixelsGpa));
    report.applications = 2;
    report.checks = RequiredChecks;
    const char text[] = "Guest graphics CPL3 windows paint input focus dialog DPI PASS\n";
    for (char c : text)
        if (c)
            __outbyte(0xE9, uint8_t(c));
}
void GraphicsPublish() noexcept {
    Copy(reinterpret_cast<void *>(guardian::graphics::ReportGpa), &report, sizeof(report));
    _ReadWriteBarrier();
    __outbyte(guardian::graphics::Port, 2);
}
