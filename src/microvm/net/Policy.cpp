#include "Network.h"
#include <algorithm>
#include <charconv>
namespace av::microvm::net {
namespace {
bool Name(const std::string &n, bool dots) {
    if (n.empty() || n.size() > 63)
        return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || (dots && c == '.')))
            return false;
    return n.front() != '.' && n.back() != '.' && n.find("..") == n.npos;
}
} // namespace
const char *Label(Mode m) {
    switch (m) {
    case Mode::Offline:
        return "offline";
    case Mode::Simulated:
        return "simulated";
    case Mode::Lab:
        return "lab";
    case Mode::Proxy:
        return "proxy";
    }
    return "invalid";
}
bool Public(uint8_t family, const Address &a) noexcept {
    const auto *p = a.bytes;
    if (family == 4) {
        for (int i = 4; i < 16; ++i)
            if (p[i])
                return false;
        if (!p[0] || p[0] >= 224 || p[0] == 10 || p[0] == 127 || p[0] == 192 || p[0] == 169 || p[0] == 0)
            return false;
        if (p[0] == 100 && p[1] >= 64 && p[1] <= 127)
            return false;
        if (p[0] == 172 && p[1] >= 16 && p[1] <= 31)
            return false;
        if (p[0] == 198 && (p[1] == 18 || p[1] == 19 || (p[1] == 51 && p[2] == 100)))
            return false;
        if (p[0] == 203 && p[1] == 0 && p[2] == 113)
            return false;
        return true;
    }
    if (family != 6 || (p[0] & 0xe0) != 0x20)
        return false;
    if (p[0] == 0x20 && p[1] == 1 && ((p[2] < 2) || (p[2] == 0x0d && p[3] == 0xb8)))
        return false;
    if (p[0] == 0x20 && p[1] == 2)
        return false;
    if (p[0] == 0x3f && (p[1] == 0xfe || p[1] == 0xff))
        return false;
    return true;
}
Address ParseAddress(uint8_t family, const std::string &s) {
    Address out{};
    // IPv6 policy literals use eight explicit hexadecimal groups. No zones,
    // names, mapped addresses, compressed ambiguity or OS name resolution.
    const char separator = family == 4 ? '.' : ':';
    const unsigned groups = family == 4 ? 4 : 8;
    state::Require(family == 4 || family == 6, "Unsupported address family");
    size_t start = 0;
    for (unsigned i = 0; i < groups; ++i) {
        auto end = s.find(separator, start);
        if (i + 1 == groups)
            end = s.size();
        state::Require(end != s.npos && end > start, "Invalid numeric address");
        unsigned n = 0;
        auto [p, ec] = std::from_chars(s.data() + start, s.data() + end, n, family == 4 ? 10 : 16);
        state::Require(ec == std::errc{} && p == s.data() + end && n <= (family == 4 ? 255u : 65535u),
                       "Numeric address out of range");
        if (family == 4)
            out.bytes[i] = uint8_t(n);
        else
            Write16(out.bytes + i * 2, uint16_t(n));
        start = end + 1;
    }
    state::Require(start == s.size() + 1, "Unexpected address suffix");
    return out;
}
void Validate(const Policy &p) {
    using state::Require;
    Require(uint32_t(p.mode) <= 3 && p.node >= 1 && p.node <= 254 && p.packetLimit >= 1 &&
                p.packetLimit <= 256 && p.byteLimit >= 1280 && p.byteLimit <= 262144,
            "Network resource/profile bounds rejected");
    Require(p.peers.size() <= MaxPeers && p.ports.size() <= 8 && p.destinations.size() <= 8,
            "Network rule quota rejected");
    for (size_t i = 0; i < p.peers.size(); ++i) {
        Require(p.peers[i] >= 1 && p.peers[i] <= 254 && p.peers[i] != p.node, "Invalid lab peer");
        for (size_t j = 0; j < i; ++j)
            Require(p.peers[i] != p.peers[j], "Duplicate lab peer");
    }
    for (size_t i = 0; i < p.ports.size(); ++i) {
        Require(p.ports[i] != 0, "Zero lab port");
        for (size_t j = 0; j < i; ++j)
            Require(p.ports[i] != p.ports[j], "Duplicate lab port");
    }
    if (p.mode == Mode::Lab)
        Require(Name(p.lab, false) && !p.peers.empty() && !p.ports.empty(),
                "Lab needs a name, peers and ports");
    else
        Require(p.lab.empty() && p.peers.empty() && p.ports.empty(),
                "Lab rules outside lab profile rejected");
    if (p.mode == Mode::Proxy) {
        Require(Public(4, p.relay) && p.relayPort && !p.destinations.empty(),
                "Proxy needs a public IPv4 relay and explicit destinations");
        for (size_t i = 0; i < p.destinations.size(); ++i) {
            const auto &d = p.destinations[i];
            Require(Name(d.name, true) && d.port && Public(d.family, d.address),
                    "Proxy destination/name rejected");
            for (size_t j = 0; j < i; ++j)
                Require(d.name != p.destinations[j].name || d.family != p.destinations[j].family,
                        "Duplicate DNS rule");
        }
    } else {
        Address zero{};
        Require(!p.relayPort && Equal(p.relay, zero) && p.destinations.empty(),
                "Proxy rules outside proxy profile rejected");
    }
}
std::vector<uint8_t> EncodePolicy(const Policy &p) {
    Validate(p);
    state::Writer w;
    w.Pod(uint32_t(1));
    w.Pod(uint32_t(p.mode));
    w.Pod(p.node);
    w.Text(p.lab);
    w.Pod(uint32_t(p.peers.size()));
    for (auto n : p.peers)
        w.Pod(n);
    w.Pod(uint32_t(p.ports.size()));
    for (auto n : p.ports)
        w.Pod(n);
    w.Pod(uint32_t(p.destinations.size()));
    for (const auto &d : p.destinations) {
        w.Text(d.name);
        w.Pod(d.family);
        w.Pod(d.address);
        w.Pod(d.port);
    }
    w.Pod(p.relay);
    w.Pod(p.relayPort);
    w.Pod(p.packetLimit);
    w.Pod(p.byteLimit);
    return w.data;
}
Policy DecodePolicy(std::span<const uint8_t> bytes) {
    state::Reader r(bytes);
    state::Require(r.Pod<uint32_t>() == 1, "Network policy version rejected");
    Policy p;
    p.mode = Mode(r.Pod<uint32_t>());
    p.node = r.Pod<uint32_t>();
    p.lab = r.Text(63);
    auto n = r.Pod<uint32_t>();
    state::Require(n <= 8, "Peer quota rejected");
    while (n--)
        p.peers.push_back(r.Pod<uint32_t>());
    n = r.Pod<uint32_t>();
    state::Require(n <= 8, "Port quota rejected");
    while (n--)
        p.ports.push_back(r.Pod<uint16_t>());
    n = r.Pod<uint32_t>();
    state::Require(n <= 8, "Destination quota rejected");
    while (n--) {
        Destination d;
        d.name = r.Text(63);
        d.family = r.Pod<uint8_t>();
        d.address = r.Pod<Address>();
        d.port = r.Pod<uint16_t>();
        p.destinations.push_back(d);
    }
    p.relay = r.Pod<Address>();
    p.relayPort = r.Pod<uint16_t>();
    p.packetLimit = r.Pod<uint32_t>();
    p.byteLimit = r.Pod<uint32_t>();
    r.End();
    Validate(p);
    return p;
}
} // namespace av::microvm::net
