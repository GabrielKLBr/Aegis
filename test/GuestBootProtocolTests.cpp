#include "../src/microvm/boot/BootChannel.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace guardian::abi;
using av::microvm::boot::BootChannel;
namespace {
void Check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
std::vector<uint8_t> Packet(MessageKind kind, const void* payload, uint32_t n, uint64_t seq = 1, uint64_t boot = 42) {
    MessageHeader h{MessageMagic, Major, Minor, kind, 0, sizeof(h), n, 0, seq, boot};
    std::vector<uint8_t> b(sizeof(h) + n); std::memcpy(b.data(), &h, sizeof(h)); if (n) std::memcpy(b.data() + sizeof(h), payload, n); return b;
}
}
int main() {
    try {
        const HelloPayload hello{X64, 1, 0}; const StopPayload stop{0, 0};
        const auto first = Packet(MessageKind::Hello, &hello, sizeof(hello));
        const auto diag = Packet(MessageKind::Diagnostic, "kernel self-test PASS", 21, 2);
        const auto last = Packet(MessageKind::StopResult, &stop, sizeof(stop), 3);
        BootChannel normal(42); Check(normal.Consume(first) && normal.Consume(diag) && normal.Consume(last) && normal.Stopped() && !normal.Status(), "Normal boot rejected");
        Check(!normal.Consume(last), "Post-stop message accepted");
        BootChannel early(42); Check(!early.Consume(Packet(MessageKind::StopResult, &stop, sizeof(stop))), "Stop before Hello accepted");
        BootChannel earlyDiag(42); Check(!earlyDiag.Consume(Packet(MessageKind::Diagnostic, "x", 1)), "Diagnostic before Hello accepted");
        BootChannel replay(42); Check(replay.Consume(first) && !replay.Consume(first) && !replay.Consume(diag), "Replay/error latch accepted");
        BootChannel duplicate(42); Check(duplicate.Consume(first) && !duplicate.Consume(Packet(MessageKind::Hello, &hello, sizeof(hello), 2)), "Duplicate Hello accepted");
        BootChannel wrongBoot(41); Check(!wrongBoot.Consume(first), "Wrong boot accepted");
        BootChannel zero(0); Check(!zero.Consume(first), "Zero boot accepted");
        for (const HelloPayload h : {HelloPayload{0x14C, 1, 0}, HelloPayload{X64, 2, 0}, HelloPayload{X64, 1, 1}}) {
            BootChannel c(42); Check(!c.Consume(Packet(MessageKind::Hello, &h, sizeof(h))), "Unsupported capability accepted");
        }
        for (size_t offset : {size_t(0), size_t(4), size_t(6), size_t(10), size_t(12), size_t(20), size_t(24), size_t(32)}) {
            auto b = first; b[offset] ^= 0x80; BootChannel c(42); Check(!c.Consume(b), "Corrupt envelope accepted");
        }
        for (size_t n : {size_t(0), size_t(39), first.size() - 1}) { BootChannel c(42); Check(!c.Consume(std::span(first).first(n)), "Truncation accepted"); }
        BootChannel huge(42); std::vector<uint8_t> big(4097); Check(!huge.Consume(big), "Oversize accepted");
        for (const uint8_t ch : {uint8_t(0), uint8_t(27), uint8_t(127), uint8_t(255)}) {
            BootChannel c(42); Check(c.Consume(first) && !c.Consume(Packet(MessageKind::Diagnostic, &ch, 1, 2)), "Invalid diagnostic text accepted");
        }
        BootChannel reserved(42); const StopPayload reservedStop{0, 1};
        Check(reserved.Consume(first) && !reserved.Consume(Packet(MessageKind::StopResult, &reservedStop, sizeof(stop), 2)), "Reserved stop accepted");
        BootChannel failed(42); const StopPayload failedStop{7, 0};
        Check(failed.Consume(first) && failed.Consume(Packet(MessageKind::StopResult, &failedStop, sizeof(stop), 2)) && failed.Status() == 7, "Failure status lost");
        BootChannel textBudget(42); Check(textBudget.Consume(first), "Hello failed"); const std::string text(1024, 'x');
        for (uint64_t seq = 2; seq <= 17; ++seq) Check(textBudget.Consume(Packet(MessageKind::Diagnostic, text.data(), 1024, seq)), "Text budget rejected early");
        Check(!textBudget.Consume(Packet(MessageKind::Diagnostic, "x", 1, 18)), "Text budget unbounded");
        BootChannel messageBudget(42); Check(messageBudget.Consume(first), "Hello failed");
        for (uint64_t seq = 2; seq <= 128; ++seq) Check(messageBudget.Consume(Packet(MessageKind::Diagnostic, "x", 1, seq)), "Message budget rejected early");
        Check(!messageBudget.Consume(Packet(MessageKind::StopResult, &stop, sizeof(stop), 129)), "Message budget unbounded");
        std::cout << "Protocol ordering, versions, bounds, replay, budgets and terminal result verified\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
