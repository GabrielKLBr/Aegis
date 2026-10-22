#include "guardian/Network.h"
namespace guardian::net {
namespace {
void Copy(uint8_t *d, const uint8_t *s, uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i)
        d[i] = s[i];
}
uint32_t Sum(const uint8_t *p, uint32_t n) noexcept {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i += 2)
        s += uint32_t(p[i]) << 8 | (i + 1 < n ? p[i + 1] : 0);
    return s;
}
uint32_t Pseudo(const Datagram &d, uint16_t n) noexcept {
    const auto bytes = d.family == 4 ? 4u : 16u;
    return Sum(d.source.bytes, bytes) + Sum(d.target.bytes, bytes) + 17 + n;
}
} // namespace
uint16_t Read16(const uint8_t *p) noexcept { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
void Write16(uint8_t *p, uint16_t n) noexcept {
    p[0] = uint8_t(n >> 8);
    p[1] = uint8_t(n);
}
uint16_t Checksum(const uint8_t *p, uint32_t n, uint32_t seed) noexcept {
    uint32_t s = seed + Sum(p, n);
    while (s >> 16)
        s = (s & 65535) + (s >> 16);
    return uint16_t(~s);
}
Address Local(uint8_t family, uint32_t node) noexcept {
    Address a{};
    if (family == 4) {
        a.bytes[0] = 198;
        a.bytes[1] = 18;
        a.bytes[3] = uint8_t(node);
    } else {
        a.bytes[0] = 0xfd;
        a.bytes[1] = 0x42;
        a.bytes[2] = 0x67;
        a.bytes[3] = 0x61;
        a.bytes[15] = uint8_t(node);
    }
    return a;
}
bool Equal(const Address &a, const Address &b) noexcept {
    for (uint32_t i = 0; i < 16; ++i)
        if (a.bytes[i] != b.bytes[i])
            return false;
    return true;
}
Status Parse(const Frame &f, Datagram &d) noexcept {
    d = {};
    if (f.bytes < 28 || f.bytes > Mtu)
        return Status::Malformed;
    const auto *p = f.data;
    uint32_t h = 0;
    if ((p[0] >> 4) == 4) {
        if (p[0] != 0x45)
            return Status::Unsupported;
        if (Read16(p + 2) != f.bytes || Checksum(p, 20) || !p[8])
            return Status::Malformed;
        if (Read16(p + 6) & 0xBFFF)
            return Status::Unsupported; // DF accepted; fragments/reserved rejected.
        if (p[9] != 17)
            return Status::Unsupported;
        d.family = 4;
        h = 20;
        Copy(d.source.bytes, p + 12, 4);
        Copy(d.target.bytes, p + 16, 4);
    } else if ((p[0] >> 4) == 6) {
        if (f.bytes < 48 || uint32_t(Read16(p + 4)) + 40 != f.bytes || !p[7])
            return Status::Malformed;
        if (p[6] != 17)
            return Status::Unsupported; // includes fragments, tunnels and every extension header.
        d.family = 6;
        h = 40;
        Copy(d.source.bytes, p + 8, 16);
        Copy(d.target.bytes, p + 24, 16);
    } else
        return Status::Unsupported;
    const auto n = Read16(p + h + 4);
    if (n < 8 || h + n != f.bytes)
        return Status::Malformed;
    d.sourcePort = Read16(p + h);
    d.targetPort = Read16(p + h + 2);
    if (!d.sourcePort || !d.targetPort || !Read16(p + h + 6) || Checksum(p + h, n, Pseudo(d, n)))
        return Status::Malformed;
    d.payload = p + h + 8;
    d.payloadBytes = uint16_t(n - 8);
    return Status::Ok;
}
bool Build(const Datagram &d, Frame &f) noexcept {
    const uint32_t h = d.family == 4 ? 20u : 40u, n = uint32_t(d.payloadBytes) + 8;
    if ((d.family != 4 && d.family != 6) || h + n > Mtu || (!d.payload && d.payloadBytes) || !d.sourcePort ||
        !d.targetPort)
        return false;
    Frame result{};
    result.bytes = h + n;
    auto *p = result.data;
    if (d.family == 4) {
        p[0] = 0x45;
        Write16(p + 2, uint16_t(result.bytes));
        p[8] = 64;
        p[9] = 17;
        Copy(p + 12, d.source.bytes, 4);
        Copy(p + 16, d.target.bytes, 4);
        Write16(p + 10, Checksum(p, 20));
    } else {
        p[0] = 0x60;
        Write16(p + 4, uint16_t(n));
        p[6] = 17;
        p[7] = 64;
        Copy(p + 8, d.source.bytes, 16);
        Copy(p + 24, d.target.bytes, 16);
    }
    Write16(p + h, d.sourcePort);
    Write16(p + h + 2, d.targetPort);
    Write16(p + h + 4, uint16_t(n));
    Copy(p + h + 8, d.payload, d.payloadBytes);
    auto c = Checksum(p + h, n, Pseudo(d, uint16_t(n)));
    Write16(p + h + 6, c ? c : 65535);
    f = result;
    return true;
}
uint32_t DnsQuery(const char *name, uint16_t type, uint16_t id, uint8_t *out, uint32_t capacity) noexcept {
    if (!name || !out || capacity < 18 || (type != 1 && type != 28))
        return 0;
    for (uint32_t i = 0; i < capacity; ++i)
        out[i] = 0;
    Write16(out, id);
    out[2] = 1;
    out[5] = 1;
    uint32_t p = 12, start = 0;
    while (name[start]) {
        uint32_t n = 0;
        while (name[start + n] && name[start + n] != '.')
            ++n;
        if (!n || n > 63 || p + n + 6 > capacity)
            return 0;
        out[p++] = uint8_t(n);
        for (uint32_t i = 0; i < n; ++i) {
            const auto c = name[start + i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
                return 0;
            out[p++] = uint8_t(c);
        }
        start += n;
        if (name[start] == '.') {
            ++start;
            if (!name[start])
                return 0;
        }
    }
    if (p == 12)
        return 0;
    out[p++] = 0;
    Write16(out + p, type);
    Write16(out + p + 2, 1);
    return p + 4;
}
bool DnsName(const uint8_t *p, uint32_t bytes, char *name, uint32_t capacity, uint16_t &type) noexcept {
    if (!p || bytes < 18 || bytes > 512 || p[2] != 1 || p[3] || Read16(p + 4) != 1 || Read16(p + 6) ||
        Read16(p + 8) || Read16(p + 10))
        return false;
    uint32_t at = 12, used = 0;
    while (at < bytes && p[at]) {
        auto n = p[at++];
        if (n > 63 || n > bytes - at || used + n + 1 >= capacity)
            return false;
        if (used)
            name[used++] = '.';
        for (uint32_t i = 0; i < n; ++i) {
            auto c = p[at++];
            if (c >= 'A' && c <= 'Z')
                c = uint8_t(c + 32);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
                return false;
            name[used++] = char(c);
        }
    }
    if (!used || at + 5 != bytes || p[at] || Read16(p + at + 3) != 1)
        return false;
    name[used] = 0;
    type = Read16(p + at + 1);
    return type == 1 || type == 28;
}
bool ValidInfo(const Info &b, uint64_t boot) noexcept {
    return b.magic == Magic && b.boot == boot && b.version == Version && b.bytes == sizeof(b) &&
           b.mtu == Mtu && b.depth == Depth && uint32_t(b.mode) <= 3 && b.node >= 1 && b.node <= 254 &&
           b.peerCount <= MaxPeers && !b.reserved;
}
} // namespace guardian::net
