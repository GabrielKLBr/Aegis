#include "Network.h"
#include <algorithm>
#include <cstring>
namespace av::microvm::net {
namespace {
void Le32(std::vector<uint8_t> &b, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i)
        b.push_back(uint8_t(x >> (i * 8)));
}
bool Has(const std::vector<uint16_t> &v, uint16_t n) { return std::find(v.begin(), v.end(), n) != v.end(); }
} // namespace
Backend::Backend(uint64_t boot, const Policy &policy, Transport transport, std::filesystem::path capture)
    : policy_(policy), transport_(std::move(transport)), capture_(std::move(capture)) {
    Validate(policy_);
    state::Require(boot != 0, "Network boot ID missing");
    info_ = {Magic,
             boot,
             Version,
             sizeof(Info),
             Mtu,
             Depth,
             policy.mode,
             policy.node,
             uint32_t(policy.peers.size()),
             {},
             0};
    for (size_t i = 0; i < policy.peers.size(); ++i)
        info_.peers[i] = policy.peers[i];
    Le32(pcap_, 0xa1b2c3d4);
    Le32(pcap_, 0x00040002);
    Le32(pcap_, 0);
    Le32(pcap_, 0);
    Le32(pcap_, Mtu);
    Le32(pcap_, 101);
    if (!capture_.empty())
        state::AtomicWrite(capture_ / "policy.gnp", EncodePolicy(policy_));
    Export();
}
Status Backend::Record(bool incoming, const Frame &f, Status result) {
    if (events_.size() >= 1024)
        return Status::Quota;
    if (f.bytes <= Mtu && f.bytes && pcap_.size() + 16 + f.bytes > 524288)
        result = Status::Quota;
    Datagram d{};
    const auto parsed = Parse(f, d);
    events_.push_back({uint32_t(incoming), uint32_t(result), f.bytes, parsed == Status::Ok ? d.family : 0u,
                       parsed == Status::Ok ? d.targetPort : 0u});
    if (result != Status::Ok)
        ++denied_;
    if (f.bytes && f.bytes <= Mtu && pcap_.size() + 16 + f.bytes <= 524288) {
        Le32(pcap_, 0);
        Le32(pcap_, uint32_t(events_.size()));
        Le32(pcap_, f.bytes);
        Le32(pcap_, f.bytes);
        pcap_.insert(pcap_.end(), f.data, f.data + f.bytes);
    }
    Export();
    return result;
}
std::string Backend::Events() const {
    std::string s = "{\"schema\":10,\"mode\":\"" + std::string(Label(policy_.mode)) +
                    "\",\"bootId\":" + std::to_string(info_.boot) +
                    ",\"policyHash\":" + std::to_string(state::Hash(EncodePolicy(policy_))) +
                    ",\"captureHash\":" + std::to_string(state::Hash(pcap_)) +
                    ",\"acceptedTx\":" + std::to_string(sent_) +
                    ",\"received\":" + std::to_string(received_) + ",\"denied\":" + std::to_string(denied_) +
                    ",\"events\":[";
    bool comma = false;
    for (const auto &e : events_) {
        if (comma)
            s += ',';
        comma = true;
        s += "{\"direction\":" + std::to_string(e.direction) + ",\"status\":" + std::to_string(e.status) +
             ",\"bytes\":" + std::to_string(e.bytes) + ",\"family\":" + std::to_string(e.family) +
             ",\"port\":" + std::to_string(e.port) + "}";
    }
    return s + "]}\n";
}
void Backend::Export() const {
    if (capture_.empty())
        return;
    // Parent is host-owned. Guest supplies no filename. Atomic replacement does
    // not follow a pre-existing symlink at either output filename.
    state::AtomicWrite(capture_ / "network.pcap", pcap_);
    state::AtomicText(capture_ / "network.json", Events());
}
Status Backend::Enqueue(const Frame &f) {
    if (count_ == Depth)
        return Status::Full;
    queue_[(head_ + count_) % Depth] = f;
    ++count_;
    return Status::Ok;
}
Status Backend::Dns(const Datagram &d) {
    char name[128]{};
    uint16_t type = 0;
    if (!DnsName(d.payload, d.payloadBytes, name, sizeof(name), type))
        return Status::Malformed;
    const uint8_t family = type == 1 ? 4 : 6;
    Address answer{};
    bool found = false;
    if (policy_.mode == Mode::Simulated && std::string(name) == "echo.guardian.test") {
        answer = Local(family, 254);
        found = true;
    }
    if (policy_.mode == Mode::Lab)
        for (auto n : policy_.peers)
            if (std::string(name) == "node-" + std::to_string(n) + ".guardian.test") {
                answer = Local(family, n);
                found = true;
            }
    if (policy_.mode == Mode::Proxy)
        for (const auto &rule : policy_.destinations)
            if (rule.name == name && rule.family == family) {
                answer = rule.address;
                found = true;
            }
    uint8_t response[512]{};
    if (uint32_t(d.payloadBytes) + 28u > sizeof(response))
        return Status::Malformed;
    std::memcpy(response, d.payload, d.payloadBytes);
    response[2] = 0x81;
    response[3] = found ? 0x80 : 0x83;
    uint16_t bytes = d.payloadBytes;
    if (found) {
        response[7] = 1;
        auto *p = response + bytes;
        p[0] = 0xc0;
        p[1] = 0x0c;
        Write16(p + 2, type);
        Write16(p + 4, 1);
        p[9] = 60;
        const uint16_t n = family == 4 ? 4 : 16;
        Write16(p + 10, n);
        std::memcpy(p + 12, answer.bytes, n);
        bytes = uint16_t(bytes + 12 + n);
    }
    Datagram reply{d.family, d.target, d.source, d.targetPort, d.sourcePort, bytes, response};
    Frame out{};
    return Build(reply, out) ? Enqueue(out) : Status::Malformed;
}
Status Backend::Send(const Frame &frame) {
    if (events_.size() >= 1024)
        return Status::Quota;
    Datagram d{};
    auto result = Parse(frame, d);
    if (result != Status::Ok)
        return Record(false, frame, result);
    if (policy_.mode == Mode::Offline)
        return Record(false, frame, Status::Denied);
    if (!Equal(d.source, Local(d.family, policy_.node)))
        return Record(false, frame, Status::Denied);
    if (sent_ + received_ >= policy_.packetLimit || traffic_ + frame.bytes > policy_.byteLimit)
        return Record(false, frame, Status::Quota);
    const bool dns = Equal(d.target, Local(d.family, 253)) && d.targetPort == 53;
    bool allowed = dns;
    if (policy_.mode == Mode::Simulated)
        allowed = allowed || (Equal(d.target, Local(d.family, 254)) && d.targetPort == 7);
    if (policy_.mode == Mode::Lab)
        for (auto peer : policy_.peers)
            if (Equal(d.target, Local(d.family, peer)) && Has(policy_.ports, d.targetPort))
                allowed = true;
    if (policy_.mode == Mode::Proxy)
        for (const auto &dest : policy_.destinations)
            if (dest.family == d.family && Equal(dest.address, d.target) && dest.port == d.targetPort)
                allowed = true;
    if (!allowed)
        return Record(false, frame, Status::Denied);
    if ((dns || policy_.mode == Mode::Simulated) && count_ == Depth)
        return Record(false, frame, Status::Full);
    // Durable capture precedes any transport effect. An output failure aborts
    // the host session before traffic can leave it.
    if (Record(false, frame, Status::Ok) != Status::Ok)
        return Status::Quota;
    ++sent_;
    traffic_ += frame.bytes;
    if (dns)
        result = Dns(d);
    else if (policy_.mode == Mode::Simulated) {
        Frame reply{};
        Datagram reverse{d.family, d.target, d.source, d.targetPort, d.sourcePort, d.payloadBytes, d.payload};
        result = Build(reverse, reply) ? Enqueue(reply) : Status::Malformed;
    } else if (transport_) {
        Frame reply{};
        result = transport_(&frame, reply);
        if (result == Status::Ok && reply.bytes)
            result = Enqueue(reply);
    } else
        result = Status::Unavailable;
    if (result != Status::Ok)
        return Record(false, Frame{}, result);
    Export();
    return Status::Ok;
}
Status Backend::Receive(Frame &frame) {
    frame = {};
    if (events_.size() >= 1024)
        return Status::Quota;
    if (policy_.mode == Mode::Offline)
        return Record(true, frame, Status::Denied);
    if (!count_ && policy_.mode == Mode::Lab && transport_) {
        Frame incoming{};
        auto s = transport_(nullptr, incoming);
        if (s == Status::Ok && incoming.bytes) {
            Datagram d{};
            auto parsed = Parse(incoming, d);
            bool allowed = false;
            if (parsed == Status::Ok && Equal(d.target, Local(d.family, policy_.node)) &&
                Has(policy_.ports, d.targetPort))
                for (auto peer : policy_.peers)
                    if (Equal(d.source, Local(d.family, peer)))
                        allowed = true;
            if (!allowed)
                return Record(true, incoming, parsed == Status::Ok ? Status::Denied : parsed);
            Enqueue(incoming);
        } else if (s != Status::Empty)
            return Record(true, frame, s);
    }
    if (!count_)
        return Status::Empty;
    if (sent_ + received_ >= policy_.packetLimit || traffic_ + queue_[head_].bytes > policy_.byteLimit)
        return Record(true, Frame{}, Status::Quota);
    frame = queue_[head_];
    if (Record(true, frame, Status::Ok) != Status::Ok) {
        frame = {};
        return Status::Quota;
    }
    queue_[head_] = {};
    head_ = (head_ + 1) % Depth;
    --count_;
    ++received_;
    traffic_ += frame.bytes;
    Export();
    return Status::Ok;
}
bool Backend::Exchange(Packet &p) {
    if (p.magic != Magic || p.boot != info_.boot || p.version != Version || p.bytes != sizeof(p) ||
        p.sequence != sequence_ + 1 || p.reserved || p.status != Status::Ok || p.length > Mtu ||
        p.op < Op::Send || p.op > Op::Receive || sequence_ >= 1024)
        return false;
    if ((p.op == Op::Receive && p.length) || (p.op == Op::Send && !p.length))
        return false;
    ++sequence_;
    Frame f{};
    f.bytes = p.length;
    std::memcpy(f.data, p.data, f.bytes);
    p.status = p.op == Op::Send ? Send(f) : Receive(f);
    std::memset(p.data, 0, sizeof(p.data));
    p.length = 0;
    if (p.op == Op::Receive && p.status == Status::Ok) {
        p.length = f.bytes;
        std::memcpy(p.data, f.data, f.bytes);
    }
    return true;
}
void Backend::Save(state::Writer &w) const {
    w.Bytes(EncodePolicy(policy_));
    w.Pod(info_.boot);
    w.Pod(sequence_);
    w.Pod(sent_);
    w.Pod(received_);
    w.Pod(denied_);
    w.Pod(traffic_);
    w.Pod(head_);
    w.Pod(count_);
    for (const auto &f : queue_)
        w.Pod(f);
    w.Pod(uint32_t(events_.size()));
    for (const auto &e : events_)
        w.Pod(e);
    w.Bytes(pcap_);
}
void Backend::Load(state::Reader &r) {
    state::Require(r.Bytes(4096) == EncodePolicy(policy_) && r.Pod<uint64_t>() == info_.boot,
                   "Snapshot network policy/identity mismatch");
    sequence_ = r.Pod<uint64_t>();
    sent_ = r.Pod<uint64_t>();
    received_ = r.Pod<uint64_t>();
    denied_ = r.Pod<uint64_t>();
    traffic_ = r.Pod<uint64_t>();
    head_ = r.Pod<uint32_t>();
    count_ = r.Pod<uint32_t>();
    state::Require(sequence_ <= 1024 && sent_ <= policy_.packetLimit && received_ <= 1024 &&
                       denied_ <= 1024 && traffic_ <= policy_.byteLimit && head_ < Depth && count_ <= Depth,
                   "Network snapshot counters rejected");
    for (auto &f : queue_) {
        f = r.Pod<Frame>();
        state::Require(f.bytes <= Mtu, "Network snapshot frame rejected");
    }
    auto n = r.Pod<uint32_t>();
    state::Require(n <= 1024, "Network event quota rejected");
    events_.clear();
    while (n--) {
        auto e = r.Pod<Event>();
        state::Require(e.direction <= 1 && e.status <= 7 && e.bytes <= Mtu, "Network event rejected");
        events_.push_back(e);
    }
    pcap_ = r.Bytes(524288);
    state::Require(pcap_.size() >= 24, "Network capture rejected");
    Export();
}
} // namespace av::microvm::net
