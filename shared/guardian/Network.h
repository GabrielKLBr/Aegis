#pragma once
#include "Abi.h"
// Point-to-point raw IP device: no host pointers, Ethernet, routing or OS ABI.
namespace guardian::net {
inline constexpr uint64_t Magic = 0x3154454E415647ull, InfoGpa = 0x132000, PacketGpa = 0x133000,
                          ReportGpa = 0x134000;
inline constexpr uint32_t Version = 1, Mtu = 1280, Depth = 8, MaxPeers = 8, RequiredChecks = 0x3F;
inline constexpr uint16_t Port = 0xF2;
enum class Mode : uint32_t { Offline, Simulated, Lab, Proxy };
enum class Op : uint32_t { Send = 1, Receive };
enum class Status : uint32_t { Ok, Empty, Denied, Malformed, Unsupported, Full, Quota, Unavailable };
struct Address {
    uint8_t bytes[16]{};
};
struct Frame {
    uint32_t bytes = 0;
    uint8_t data[Mtu]{};
};
struct Info {
    uint64_t magic, boot;
    uint32_t version, bytes, mtu, depth;
    Mode mode;
    uint32_t node, peerCount, peers[MaxPeers], reserved;
};
struct Packet {
    uint64_t magic, boot, sequence;
    uint32_t version, bytes;
    Op op;
    Status status;
    uint32_t length, reserved;
    uint8_t data[Mtu];
};
struct Report {
    uint64_t magic, boot;
    uint32_t version, bytes, checks, mode;
    uint64_t sent, received, denied, userCalls;
};
struct Datagram {
    uint8_t family = 0;
    Address source{}, target{};
    uint16_t sourcePort = 0, targetPort = 0, payloadBytes = 0;
    const uint8_t *payload = nullptr;
};
Address Local(uint8_t family, uint32_t node) noexcept;
bool Equal(const Address &, const Address &) noexcept;
uint16_t Read16(const uint8_t *) noexcept;
void Write16(uint8_t *, uint16_t) noexcept;
uint16_t Checksum(const uint8_t *, uint32_t, uint32_t seed = 0) noexcept;
Status Parse(const Frame &, Datagram &) noexcept;
bool Build(const Datagram &, Frame &) noexcept;
uint32_t DnsQuery(const char *, uint16_t, uint16_t, uint8_t *, uint32_t) noexcept;
bool DnsName(const uint8_t *, uint32_t, char *, uint32_t, uint16_t &) noexcept;
bool ValidInfo(const Info &, uint64_t) noexcept;
static_assert(sizeof(Packet) == 1328 && sizeof(Report) == 64 && sizeof(Info) == 80);
} // namespace guardian::net
