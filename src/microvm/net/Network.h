#pragma once
#include "../state/Files.h"
#include "guardian/Network.h"
#include <array>
#include <functional>
namespace av::microvm::net {
using namespace guardian::net;
struct Destination {
    std::string name;
    uint8_t family = 4;
    Address address{};
    uint16_t port = 0;
};
struct Policy {
    Mode mode = Mode::Offline;
    uint32_t node = 1;
    std::string lab;
    std::vector<uint32_t> peers;
    std::vector<uint16_t> ports;
    std::vector<Destination> destinations;
    Address relay{};
    uint16_t relayPort = 0;
    uint32_t packetLimit = 128, byteLimit = 131072;
};
std::vector<uint8_t> EncodePolicy(const Policy &);
Policy DecodePolicy(std::span<const uint8_t>);
void Validate(const Policy &);
bool Public(uint8_t, const Address &) noexcept;
bool LocalEndpoint(const Address &);
Address ParseAddress(uint8_t, const std::string &);
const char *Label(Mode);
// Invoked only after policy and frame validation. Lab transports move raw IP;
// proxy transports move UDP payload to one fixed external relay.
using Transport = std::function<Status(const Frame *, Frame &)>;
struct Options {
    Policy policy{};
    std::filesystem::path catalog, capture;
    std::string vm;
    Transport transport;
};
class Backend {
  public:
    Backend(uint64_t, const Policy &, Transport = {}, std::filesystem::path capture = {});
    const Info &Enumeration() const { return info_; }
    bool Exchange(Packet &);
    Status Send(const Frame &);
    Status Receive(Frame &);
    void Save(state::Writer &) const;
    void Load(state::Reader &);
    void Export() const;
    uint64_t Sent() const { return sent_; }
    uint64_t Received() const { return received_; }
    uint64_t Denied() const { return denied_; }
    std::string Events() const;
    const std::vector<uint8_t> &Capture() const { return pcap_; }

  private:
    Status Record(bool, const Frame &, Status);
    Status Enqueue(const Frame &);
    Status Dns(const Datagram &);
    Policy policy_;
    Info info_{};
    Transport transport_;
    std::filesystem::path capture_;
    uint64_t sequence_ = 0, sent_ = 0, received_ = 0, denied_ = 0, traffic_ = 0;
    std::array<Frame, Depth> queue_{};
    uint32_t head_ = 0, count_ = 0;
    struct Event {
        uint32_t direction, status, bytes, family, port;
    };
    std::vector<Event> events_;
    std::vector<uint8_t> pcap_;
};
Transport LabTransport(const Options &);
using RelayExchange =
    std::function<Status(const Address &, uint16_t, std::span<const uint8_t>, std::vector<uint8_t> &)>;
Transport ProxyTransport(const Policy &, RelayExchange = {});
// Bounded native UDP request/reply used by the relay transport, no resolver.
Status UdpExchange(const Address &, uint16_t, std::span<const uint8_t>, std::vector<uint8_t> &);
} // namespace av::microvm::net
