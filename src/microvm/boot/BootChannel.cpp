#include "BootChannel.h"
#include <cstring>
namespace av::microvm::boot {
bool BootChannel::Consume(std::span<const uint8_t> packet) {
    using namespace guardian::abi;
    if (!error_.empty()) return false;
    if (stopped_) return Fail("Message after terminal stop");
    if (packet.size() < sizeof(MessageHeader) || packet.size() > MaxMessageBytes) return Fail("Invalid channel packet extent");
    MessageHeader h{}; std::memcpy(&h, packet.data(), sizeof(h));
    if (ValidateMessage(h, packet.size(), bootId_, next_) != Result::Ok) return Fail("Channel envelope/version/session/sequence rejected");
    if (++messages_ > 128 || next_ == UINT64_MAX) return Fail("Channel message budget exhausted");
    const auto* payload = packet.data() + sizeof(h);
    if (h.kind == MessageKind::Hello) {
        if (hello_) return Fail("Duplicate Hello");
        HelloPayload hello{}; std::memcpy(&hello, payload, sizeof(hello));
        if (hello.machine != X64 || hello.vcpuCount != 1 || hello.capabilities) return Fail("Unsupported kernel capabilities");
        hello_ = true;
    } else {
        if (!hello_) return Fail("Hello required before diagnostic or stop");
        if (h.kind == MessageKind::Diagnostic) {
            if (h.payloadBytes > 16384 - diagnostic_.size()) return Fail("Diagnostic byte budget exhausted");
            for (uint32_t i = 0; i < h.payloadBytes; ++i)
                if ((payload[i] < 32 && payload[i] != '\n' && payload[i] != '\t') || payload[i] > 126) return Fail("Diagnostic is not bounded ASCII text");
            diagnostic_.append(reinterpret_cast<const char*>(payload), h.payloadBytes);
        } else {
            StopPayload stop{}; std::memcpy(&stop, payload, sizeof(stop));
            if (stop.reserved) return Fail("Nonzero reserved stop field");
            status_ = stop.status; stopped_ = true;
        }
    }
    ++next_; return true;
}
}
