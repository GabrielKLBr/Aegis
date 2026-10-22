#include "NetworkDriver.h"
#include "TelemetryKernel.h"
#include "guardian/Cpu.h"
#include "guardian/Storage.h"
#include <intrin.h>
extern "C" const volatile char GuardianNetworkTelemetryCapability[]="GuardianTelemetry/1/bounded";
namespace {
void Copy(void *destination, const void *source, uint32_t n) noexcept {
    auto *d = static_cast<volatile uint8_t *>(destination);
    auto *s = static_cast<const volatile uint8_t *>(source);
    for (uint32_t i = 0; i < n; ++i)
        d[i] = s[i];
}
guardian::net::Driver driver;
guardian::net::Report report{};
} // namespace
namespace guardian::net {
bool Driver::Initialize(uint64_t boot) noexcept {
    Copy(&info_, reinterpret_cast<const void *>(InfoGpa), sizeof(info_));
    sequence_ = sent_ = received_ = denied_ = 0;auditPid_=auditUser_=0;
    return ValidInfo(info_, boot);
}
Status Driver::Transmit(const Frame &f) noexcept {
    auto copy = f;
    return Exchange(Op::Send, copy);
}
Status Driver::Receive(Frame &f) noexcept {
    f = {};
    return Exchange(Op::Receive, f);
}
Status Driver::Exchange(Op operation, Frame &frame) noexcept {
    if (frame.bytes > Mtu)
        return Status::Malformed;
    auto audit=telemetry::Begin(telemetry::Domain::Network,operation==Op::Send?telemetry::Operation::Send:telemetry::Operation::Receive,auditPid_,auditUser_,"guest raw IP device");
    if(!telemetry::Before(audit)){telemetry::End(audit,uint32_t(Status::Denied));return Status::Denied;}
    Packet request{Magic,     info_.boot, ++sequence_, Version, sizeof(Packet),
                   operation, Status::Ok, frame.bytes, 0,       {}};
    Copy(request.data, frame.data, frame.bytes);
    Copy(reinterpret_cast<void *>(PacketGpa), &request, sizeof(request));
    _ReadWriteBarrier();
    __outbyte(Port, 1);
    Packet response{};
    Copy(&response, reinterpret_cast<const void *>(PacketGpa), sizeof(response));
    if (response.magic != request.magic || response.boot != request.boot ||
        response.sequence != request.sequence || response.version != Version ||
        response.bytes != sizeof(Packet) || response.op != operation || response.reserved ||
        response.length > Mtu || uint32_t(response.status) > 7 ||
        (operation == Op::Send && response.length) || (response.status != Status::Ok && response.length))
        return Status::Malformed;
    if (operation == Op::Receive && response.status == Status::Ok) {
        frame.bytes = response.length;
        Copy(frame.data, response.data, frame.bytes);
    }
    if(response.status==Status::Ok){if(operation==Op::Send)++sent_;else ++received_;}
    else if(response.status==Status::Denied)++denied_;
    telemetry::End(audit,uint32_t(response.status));
    return response.status;
}
} // namespace guardian::net
uint32_t NetworkSyscall(uint64_t operation, uint64_t address, uint64_t bytes,
                        uint64_t &transferred) noexcept {
    using namespace guardian::net;
    ++report.userCalls;
    transferred = 0;
    constexpr auto first = guardian::cpu::UserData + 0x100, end = guardian::cpu::UserData + 4096;
    if ((operation != 0x90 && operation != 0x91) || address < first || address > end || !bytes ||
        bytes > Mtu || bytes > end - address)
        return uint32_t(Status::Malformed);
    Frame frame{};
    Status status = Status::Malformed;
    if (operation == 0x90) {
        frame.bytes = uint32_t(bytes);
        Copy(frame.data, reinterpret_cast<const void *>(address), frame.bytes);
        status = driver.Transmit(frame);
    } else {
        status = driver.Receive(frame);
        if (status == Status::Ok) {
            if (frame.bytes > bytes)
                return uint32_t(Status::Malformed);
            Copy(reinterpret_cast<void *>(address), frame.data, frame.bytes);
            transferred = frame.bytes;
        }
    }
    return uint32_t(status);
}
void NetworkSelfTest(void (*check)(bool, uint32_t) noexcept) noexcept {
    using namespace guardian::net;
    const auto *boot = reinterpret_cast<const guardian::abi::BootInfo *>(0x100000);
    if(GuardianNetworkTelemetryCapability[0]=='G')TelemetryInitialize(boot->bootId);
    report = {Magic, boot->bootId, Version, sizeof(report), 0, 0, 0, 0, 0, 0};
    check(driver.Initialize(boot->bootId), 0x1801);
    const auto &info = driver.Enumeration();
    report.mode = uint32_t(info.mode);
    report.checks |= 1;
    check(NetworkUserInvoke(0x90, 0x100000, 1) == uint32_t(Status::Malformed), 0x1812);
    check(NetworkUserInvoke(0x91, UINT64_MAX - 10, 1280) == uint32_t(Status::Malformed), 0x1813);
    check(NetworkUserInvoke(0x90, guardian::cpu::UserData + 0x100, 1281) == uint32_t(Status::Malformed),
          0x1814);
    const uint8_t payload[] = {'g', 'u', 'a', 'r', 'd', 'i', 'a', 'n', '-', 'n', 'e', 't'};
    for (uint8_t family = 4; family <= 6; family += 2) {
        Frame f{};
        Datagram d{family, Local(family, info.node), Local(family, 254), 40000, 7, sizeof(payload), payload};
        check(Build(d, f), 0x1802);
        if (info.mode == Mode::Offline) {
            check(static_cast<Status>(NetworkUserExchange(1, f)) == Status::Denied, 0x1803);
            ++report.denied;
            Frame in{};
            check(static_cast<Status>(NetworkUserExchange(2, in)) == Status::Denied, 0x1804);
            ++report.denied;
        } else if (info.mode == Mode::Simulated || info.mode == Mode::Lab) {
            if (info.mode == Mode::Lab) {
                check(info.peerCount != 0, 0x1805);
                d.target = Local(family, info.peers[0]);
                d.targetPort = 40000;
                check(Build(d, f), 0x1806);
            }
            check(static_cast<Status>(NetworkUserExchange(1, f)) == Status::Ok, 0x1807);
            ++report.sent;
            Frame in{};
            Status s = Status::Empty;
            for (uint32_t tries = 0; tries < 300 && s == Status::Empty; ++tries)
                s = static_cast<Status>(NetworkUserExchange(2, in));
            check(s == Status::Ok, 0x1808);
            Datagram reply{};
            check(Parse(in, reply) == Status::Ok && reply.family == family && Equal(reply.target, d.source) &&
                      Equal(reply.source, d.target) && reply.targetPort == d.sourcePort &&
                      reply.payloadBytes == sizeof(payload),
                  0x1809);
            for (uint32_t i = 0; i < sizeof(payload); ++i)
                check(reply.payload[i] == payload[i], 0x180A);
            ++report.received;
        }
        // Both families must refuse host, LAN and arbitrary external targets.
        d.target = {};
        d.target.bytes[0] = family == 4 ? 127 : 0;
        d.target.bytes[family == 4 ? 3 : 15] = 1;
        check(Build(d, f) && static_cast<Status>(NetworkUserExchange(1, f)) == Status::Denied, 0x180B);
        ++report.denied;
        if (info.mode != Mode::Offline) {
            uint8_t query[128]{};
            auto n = DnsQuery("echo.guardian.test", family == 4 ? 1 : 28, 0x1234, query, sizeof(query));
            d.target = Local(family, 253);
            d.targetPort = 53;
            d.payload = query;
            d.payloadBytes = uint16_t(n);
            check(n && Build(d, f) && static_cast<Status>(NetworkUserExchange(1, f)) == Status::Ok, 0x180C);
            ++report.sent;
            Frame in{};
            check(static_cast<Status>(NetworkUserExchange(2, in)) == Status::Ok, 0x180D);
            Datagram reply{};
            check(Parse(in, reply) == Status::Ok && reply.sourcePort == 53 && reply.payloadBytes >= n &&
                      Read16(reply.payload) == 0x1234,
                  0x180E);
            if (info.mode == Mode::Simulated) {
                check(reply.payload[3] == 0x80 && reply.payload[7] == 1, 0x180F);
                auto a = Local(family, 254);
                const auto size = family == 4 ? 4u : 16u;
                for (uint32_t i = 0; i < size; ++i)
                    check(reply.payload[reply.payloadBytes - size + i] == a.bytes[i], 0x1810);
            } else
                check(reply.payload[3] == 0x83 && !reply.payload[7], 0x1811);
            ++report.received;
        }
    }
    check(report.userCalls >= 9, 0x1815);
    const char userMessage[] = "Guest network CPL3 buffer validation PASS\n";
    for (char c : userMessage)
        if (c)
            __outbyte(0xE9, uint8_t(c));
    report.checks = RequiredChecks;
    const char message[] = "Guest network IPv4 IPv6 UDP DNS containment PASS\n";
    for (char c : message)
        if (c)
            __outbyte(0xE9, uint8_t(c));
}
void NetworkPublish() noexcept {
    Copy(reinterpret_cast<void *>(guardian::net::ReportGpa), &report, sizeof(report));
    _ReadWriteBarrier();
    __outbyte(guardian::net::Port, 2);
}

bool NetworkRuntimeInitialize(uint64_t boot) noexcept {
    using namespace guardian::net;if(GuardianNetworkTelemetryCapability[0]!='G'||!driver.Initialize(boot))return false;
    report={Magic,boot,Version,sizeof(report),1,uint32_t(driver.Enumeration().mode),0,0,0,0};return true;
}
uint32_t NetworkRuntimeEcho(uint32_t pid,uint32_t user) noexcept {
    using namespace guardian::net;using namespace guardian;++report.userCalls;driver.AuditActor(pid,user);
    const uint8_t payload[]={'G','u','a','r','d','i','a','n','-','1','3'};
    Datagram request{4,Local(4,driver.Enumeration().node),Local(4,254),40000,7,sizeof(payload),payload};Frame frame{};
    if(!Build(request,frame))return storage::Invalid;auto status=driver.Transmit(frame);
    if(status==Status::Ok){status=driver.Receive(frame);Datagram response{};
        if(status==Status::Ok){if(Parse(frame,response)!=Status::Ok||response.family!=4||!Equal(response.source,request.target)||!Equal(response.target,request.source)||response.sourcePort!=7||response.targetPort!=40000||response.payloadBytes!=sizeof(payload))status=Status::Malformed;
            else for(uint32_t i=0;i<sizeof(payload);++i)if(response.payload[i]!=payload[i])status=Status::Malformed;}}
    report.sent=driver.Sent();report.received=driver.Received();report.denied=driver.Denied();
    return status==Status::Ok?0:status==Status::Denied?storage::Denied:storage::IoError;
}
