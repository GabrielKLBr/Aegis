#include "../src/microvm/net/Network.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <csignal>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace av::microvm;
using namespace av::microvm::net;
namespace {
uint32_t checks = 0;
void Check(bool ok) {
    ++checks;
    if (!ok)
        throw std::runtime_error("Network check " + std::to_string(checks));
}
template <class F> void Reject(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    Check(caught);
}
Frame PacketTo(uint8_t family, uint32_t node, uint32_t target, uint16_t port = 7) {
    static const uint8_t p[] = {1, 2, 3, 4, 5};
    Frame f{};
    Check(Build({family, Local(family, node), Local(family, target), 40000, port, sizeof(p), p}, f));
    return f;
}
void Reply(Backend &b, uint8_t family) {
    Frame f{};
    Check(b.Receive(f) == Status::Ok);
    Datagram d{};
    Check(Parse(f, d) == Status::Ok && d.family == family && d.payloadBytes == 5 && d.payload[4] == 5);
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 2);
        const auto root = std::filesystem::absolute(argv[1]) / state::Token();
        std::filesystem::create_directories(root);
        state::AtomicText(root / ".owner", "GuardianVM catalog\n");
        Policy offline;
        Check(DecodePolicy(EncodePolicy(offline)).mode == Mode::Offline);
        Backend off(1, offline);
        for (uint8_t family = 4; family <= 6; family += 2) {
            auto f = PacketTo(family, 1, 254);
            Check(off.Send(f) == Status::Denied);
            Check(off.Receive(f) == Status::Denied);
        }
        Check(off.Sent() == 0);
        for (auto text :
             {"127.0.0.1", "10.1.2.3", "172.16.0.1", "192.168.1.2", "169.254.169.254", "100.64.0.1",
              "198.18.0.1", "198.51.100.1", "203.0.113.2", "224.0.0.1", "255.255.255.255", "0.0.0.0"})
            Check(!Public(4, ParseAddress(4, text)));
        for (auto text :
             {"0000:0000:0000:0000:0000:0000:0000:0001", "0000:0000:0000:0000:0000:ffff:7f00:0001",
              "fd42:6761:0000:0000:0000:0000:0000:0001", "fe80:0000:0000:0000:0000:0000:0000:0001",
              "ff02:0000:0000:0000:0000:0000:0000:0001", "2001:0000:0000:0000:0000:0000:0000:0001",
              "2002:7f00:0001:0000:0000:0000:0000:0001", "2001:0db8:0000:0000:0000:0000:0000:0001"})
            Check(!Public(6, ParseAddress(6, text)));
        Check(Public(4, ParseAddress(4, "8.8.8.8")));
        Check(Public(6, ParseAddress(6, "2606:4700:4700:0000:0000:0000:0000:1111")));
        for (auto text : {"1.2.3.4.5", "localhost", "127.1", "-1.2.3.4", "1.2.3.256", "1.2..3"})
            Reject([&] { ParseAddress(4, text); });
        Reject([&] { ParseAddress(6, "::1%lo"); });
        Policy simulated;
        simulated.mode = Mode::Simulated;
        Backend sim(2, simulated, {}, root);
        Datagram d{};
        for (uint8_t family = 4; family <= 6; family += 2) {
            auto f = PacketTo(family, 1, 254);
            Check(sim.Send(f) == Status::Ok);
            Reply(sim, family);
            auto altered = f;
            altered.data[family == 4 ? 9 : 6] = 6;
            Check(sim.Send(altered) != Status::Ok);
            altered = f;
            altered.data[altered.bytes - 1] ^= 1;
            Check(sim.Send(altered) == Status::Malformed);
            altered = f;
            altered.bytes = Mtu + 1;
            Check(sim.Send(altered) == Status::Malformed);
            for (uint32_t target : {0u, 1u, 2u, 253u})
                Check(sim.Send(PacketTo(family, 1, target)) == Status::Denied);
            Check(sim.Send(PacketTo(family, 2, 254)) == Status::Denied);
            Check(sim.Send(PacketTo(family, 1, 254, 53)) == Status::Denied);
            uint8_t query[128]{};
            auto n = DnsQuery("echo.guardian.test", family == 4 ? 1 : 28, 0x4211, query, sizeof(query));
            Check(n > 12);
            Frame dns{};
            Check(Build({family, Local(family, 1), Local(family, 253), 41000, 53, uint16_t(n), query}, dns));
            Check(sim.Send(dns) == Status::Ok);
            Frame answer{};
            Check(sim.Receive(answer) == Status::Ok && Parse(answer, d) == Status::Ok && d.payload[7] == 1 &&
                  Read16(d.payload) == 0x4211);
            auto q = DnsQuery("private.invalid", 1, 1, query, sizeof(query));
            Check(Build({family, Local(family, 1), Local(family, 253), 41000, 53, uint16_t(q), query}, dns));
            Check(sim.Send(dns) == Status::Ok);
            Check(sim.Receive(answer) == Status::Ok && Parse(answer, d) == Status::Ok &&
                  d.payload[3] == 0x83 && !d.payload[7]);
            query[12] = 0xc0;
            Check(Build({family, Local(family, 1), Local(family, 253), 41000, 53, uint16_t(q), query}, dns));
            Check(sim.Send(dns) == Status::Malformed);
        }
        // Frozen independently generated wire vectors, not a reciprocal parser test.
        {
            const std::string golden = "45000021000000004011eda8c6120001c61200fe9c400007000dcd620102030405";
            auto f = PacketTo(4, 1, 254);
            Check(f.bytes * 2 == golden.size());
            constexpr char digits[] = "0123456789abcdef";
            for (uint32_t i = 0; i < f.bytes; ++i)
                Check(golden[i * 2] == digits[f.data[i] >> 4] && golden[i * 2 + 1] == digits[f.data[i] & 15]);
        }
        {
            const std::string golden = "60000000000d1140fd426761000000000000000000000001fd4267610000000000000"
                                       "000000000fe9c400007000d903f0102030405";
            auto f = PacketTo(6, 1, 254);
            Check(f.bytes * 2 == golden.size());
            constexpr char digits[] = "0123456789abcdef";
            for (uint32_t i = 0; i < f.bytes; ++i)
                Check(golden[i * 2] == digits[f.data[i] >> 4] && golden[i * 2 + 1] == digits[f.data[i] & 15]);
        }
        // Mutated IPv4 fragmentation and IPv6 extensions/tunnels are refused.
        auto fragmented = PacketTo(4, 1, 254);
        fragmented.data[6] = 0x20;
        fragmented.data[10] = fragmented.data[11] = 0;
        Write16(fragmented.data + 10, Checksum(fragmented.data, 20));
        Check(Parse(fragmented, d) == Status::Unsupported);
        for (uint8_t next : {uint8_t(0), uint8_t(43), uint8_t(44), uint8_t(50), uint8_t(51), uint8_t(60),
                             uint8_t(4), uint8_t(41), uint8_t(58)}) {
            auto f = PacketTo(6, 1, 254);
            f.data[6] = next;
            Check(Parse(f, d) == Status::Unsupported);
        }
        // Save a queued receive and prove exact repeatability and policy binding.
        Backend queue(3, simulated);
        Check(queue.Send(PacketTo(4, 1, 254)) == Status::Ok);
        state::Writer saved;
        queue.Save(saved);
        Backend restored(3, simulated);
        state::Reader reader(saved.data);
        restored.Load(reader);
        reader.End();
        state::Writer duplicate;
        restored.Save(duplicate);
        Check(saved.data == duplicate.data);
        Reply(queue, 4);
        Reply(restored, 4);
        Check(queue.Capture() == restored.Capture());
        Policy changed = simulated;
        changed.node = 2;
        Backend wrong(3, changed);
        Reject([&] {
            state::Reader r(saved.data);
            wrong.Load(r);
        });
        for (size_t cut : {size_t(0), size_t(4), saved.data.size() - 1})
            Reject([&] {
                Backend b(3, simulated);
                state::Reader r(std::span(saved.data).first(cut));
                b.Load(r);
            });
        Backend full(4, simulated);
        for (unsigned i = 0; i < Depth; ++i)
            Check(full.Send(PacketTo(4, 1, 254)) == Status::Ok);
        Check(full.Send(PacketTo(4, 1, 254)) == Status::Full);
        Policy quota = simulated;
        quota.packetLimit = 1;
        Backend limited(5, quota);
        Check(limited.Send(PacketTo(4, 1, 254)) == Status::Ok);
        Frame empty{};
        Check(limited.Receive(empty) == Status::Quota);
        Check(limited.Send(PacketTo(6, 1, 254)) == Status::Quota);
        guardian::net::Packet envelope{Magic,       5,          1, Version, sizeof(guardian::net::Packet),
                                       Op::Receive, Status::Ok, 0, 0,       {}};
        Check(limited.Exchange(envelope));
        Check(!limited.Exchange(envelope));
        envelope.boot = 999;
        Check(!limited.Exchange(envelope));
        // Actual separate mailbox endpoints; symmetric rules required on BOTH VMs.
        Policy a;
        a.mode = Mode::Lab;
        a.lab = "isolated";
        a.node = 1;
        a.peers = {2};
        a.ports = {40000};
        Policy b = a;
        b.node = 2;
        b.peers = {1};
        Options oa;
        oa.policy = a;
        oa.catalog = root;
        Options ob = oa;
        ob.policy = b;
        auto ta = LabTransport(oa), tb = LabTransport(ob);
        // A durable publication can hold the switch for over 100 ms. A healthy
        // sender must wait for that publication, while a stuck lock stays bounded.
        auto heldFrame=PacketTo(4,1,2,40000);Frame heldReply{};
        {
            auto held=std::make_unique<state::Lease>(root/".lab-isolated"/".bus.lock");
            std::atomic<bool> entered{false};Status sent=Status::Malformed;
            std::jthread sender([&]{entered=true;Frame unused{};sent=ta(&heldFrame,unused);});
            while(!entered)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::this_thread::sleep_for(std::chrono::milliseconds(250));held.reset();sender.join();
            Check(sent==Status::Ok&&tb(nullptr,heldReply)==Status::Ok&&heldReply.bytes==heldFrame.bytes);
        }
        {
            state::Lease held(root/".lab-isolated"/".bus.lock");const auto begin=std::chrono::steady_clock::now();Frame unused{};
            const auto blocked=ta(&heldFrame,unused);const auto duration=std::chrono::steady_clock::now()-begin;
            Check(blocked==Status::Unavailable&&duration>=std::chrono::milliseconds(1900)&&duration<std::chrono::seconds(3));
        }
        Backend ba(11, a, ta), bb(12, b, tb);
        Reject([&] { LabTransport(oa); });
        for (uint8_t family = 4; family <= 6; family += 2) {
            Check(ba.Send(PacketTo(family, 1, 2, 40000)) == Status::Ok);
            Reply(bb, family);
            Check(bb.Send(PacketTo(family, 2, 1, 40000)) == Status::Ok);
            Reply(ba, family);
        }
        Check(ba.Send(PacketTo(4, 1, 3, 40000)) == Status::Denied);
        Check(ba.Send(PacketTo(6, 1, 2, 7)) == Status::Denied);
        Policy other = b;
        other.lab = "other";
        Options oc = ob;
        oc.policy = other;
        auto tc = LabTransport(oc);
        Backend bc(13, other, tc);
        Check(bc.Receive(empty) == Status::Empty);
        for (unsigned i = 0; i < Depth; ++i)
            Check(ba.Send(PacketTo(4, 1, 2, 40000)) == Status::Ok);
        Check(ba.Send(PacketTo(4, 1, 2, 40000)) == Status::Full);
        // Re-registration erases stale packets and requires a new receiver epoch.
        bb = Backend(14, b);
        tb = {};
        ob.policy = b;
        auto fresh = LabTransport(ob);
        Backend freshB(15, b, fresh);
        Check(freshB.Receive(empty) == Status::Empty);
        Policy bad = a;
        bad.peers = {1};
        Reject([&] { Validate(bad); });
        bad = a;
        bad.ports = {0};
        Reject([&] { Validate(bad); });
        bad = a;
        bad.lab = "../escape";
        Reject([&] { Validate(bad); });
        Policy proxy;
        proxy.mode = Mode::Proxy;
        proxy.relay = ParseAddress(4, "8.8.8.8");
        proxy.relayPort = 9000;
        proxy.destinations = {{"allowed.test", 4, ParseAddress(4, "1.1.1.1"), 7}};
        proxy.destinations.push_back(
            {"allowed.test", 6, ParseAddress(6, "2606:4700:4700:0000:0000:0000:0000:1111"), 7});
        Validate(proxy);
        unsigned transportCalls = 0;
        Backend px(16, proxy, [&](const Frame *, Frame &) {
            ++transportCalls;
            return Status::Unavailable;
        });
        for (auto text : {"127.0.0.1", "192.168.1.1", "169.254.169.254", "9.9.9.9"}) {
            auto f = PacketTo(4, 1, 254);
            Datagram req{};
            Check(Parse(f, req) == Status::Ok);
            req.target = ParseAddress(4, text);
            Check(Build(req, f));
            Check(px.Send(f) == Status::Denied);
        }
        Check(transportCalls == 0);
        auto f = PacketTo(4, 1, 254);
        Check(Parse(f, d) == Status::Ok);
        d.target = proxy.destinations[0].address;
        Check(Build(d, f));
        Check(px.Send(f) == Status::Unavailable && transportCalls == 1);
        for (auto text : {"127.0.0.1", "192.168.1.1", "0.0.0.0"}) {
            auto p = proxy;
            p.relay = ParseAddress(4, text);
            Reject([&] { Validate(p); });
        }
        auto p = proxy;
        p.destinations[0].address = ParseAddress(4, "10.0.0.1");
        Reject([&] { Validate(p); });
        // Exercise the production relay envelope and response tuple checks with
        // a controlled public relay adapter; no internet service is contacted.
        bool corrupt = false;
        auto relay = ProxyTransport(proxy, [&](const Address &address, uint16_t port,
                                               std::span<const uint8_t> request,
                                               std::vector<uint8_t> &response) {
            Check(Equal(address, proxy.relay) && port == 9000 && request.size() >= 32);
            Frame in{};
            in.bytes = uint32_t(request.size() - 32);
            std::memcpy(in.data, request.data() + 32, in.bytes);
            Datagram q{};
            Check(Parse(in, q) == Status::Ok);
            Frame answer{};
            Check(Build({q.family, q.target, q.source, q.targetPort, q.sourcePort, q.payloadBytes, q.payload},
                        answer));
            response.assign(request.begin(), request.begin() + 32);
            Write16(response.data() + 24, uint16_t(answer.bytes));
            response.insert(response.end(), answer.data, answer.data + answer.bytes);
            if (corrupt)
                response[8] ^= 1;
            return Status::Ok;
        });
        Backend realProxy(17, proxy, relay);
        Check(realProxy.Send(f) == Status::Ok);
        Reply(realProxy, 4);
        auto ipv6 = PacketTo(6, 1, 254);
        Datagram v6{};
        Check(Parse(ipv6, v6) == Status::Ok);
        v6.target = proxy.destinations[1].address;
        Check(Build(v6, ipv6));
        Check(realProxy.Send(ipv6) == Status::Ok);
        Reply(realProxy, 6);
        corrupt = true;
        Check(realProxy.Send(f) == Status::Malformed);
        Check(realProxy.Receive(empty) == Status::Empty);
        // Native UDP sockets: reply from an unrelated source is ignored by the
        // connected socket, then the pinned relay response arrives.
#ifdef _WIN32
        WSADATA ws{};
        Check(WSAStartup(MAKEWORD(2, 2), &ws) == 0);
        using TestSocket = SOCKET;
        auto closeSocket = [](SOCKET x) { closesocket(x); };
#else
        using TestSocket = int;
        auto closeSocket = [](int x) { close(x); };
#endif
        TestSocket server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Check(server != TestSocket(-1));
        sockaddr_in bindAddress{};
        bindAddress.sin_family = AF_INET;
        bindAddress.sin_addr.s_addr = htonl(0x7f000001);
        Check(bind(server, reinterpret_cast<sockaddr *>(&bindAddress), sizeof(bindAddress)) == 0);
#ifdef _WIN32
        int socketBytes = sizeof(bindAddress);
        DWORD timeout = 2000;
        setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout),
                   sizeof(timeout));
#else
        socklen_t socketBytes = sizeof(bindAddress);
        timeval timeout{2, 0};
        setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
        Check(getsockname(server, reinterpret_cast<sockaddr *>(&bindAddress), &socketBytes) == 0);
#ifndef _WIN32
        struct sigaction action{}, previousAction{};
        action.sa_handler = +[](int) {};
        sigemptyset(&action.sa_mask);
        Check(sigaction(SIGUSR1, &action, &previousAction) == 0);
        const auto receiver = pthread_self();
#endif
        bool serverOk = false;
        std::thread relayThread([&] {
            char buffer[64]{};
            sockaddr_in client{};
#ifdef _WIN32
            int bytes = sizeof(client);
#else
            socklen_t bytes = sizeof(client);
#endif
            auto n =
                recvfrom(server, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&client), &bytes);
            if (n > 0) {
                auto stranger = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                sendto(stranger, "bad", 3, 0, reinterpret_cast<sockaddr *>(&client), bytes);
                closeSocket(stranger);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
#ifndef _WIN32
                pthread_kill(receiver, SIGUSR1);
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                serverOk =
                    sendto(server, buffer, int(n), 0, reinterpret_cast<sockaddr *>(&client), bytes) == n;
            }
        });
        const uint8_t payload[] = {9, 8, 7};
        std::vector<uint8_t> rawResponse;
        auto rawStatus =
            UdpExchange(ParseAddress(4, "127.0.0.1"), ntohs(bindAddress.sin_port), payload, rawResponse);
        relayThread.join();
        closeSocket(server);
#ifndef _WIN32
        Check(sigaction(SIGUSR1, &previousAction, nullptr) == 0);
#endif
        Check(rawStatus == Status::Ok && serverOk && rawResponse == std::vector<uint8_t>({9, 8, 7}));
        // An unresponsive relay must retain one deadline even when many VM
        // wake-like signals arrive. It must neither fail early nor wait anew.
        TestSocket silent = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Check(silent != TestSocket(-1));
        bindAddress.sin_port = 0;
        Check(bind(silent, reinterpret_cast<sockaddr *>(&bindAddress), sizeof(bindAddress)) == 0);
        Check(getsockname(silent, reinterpret_cast<sockaddr *>(&bindAddress), &socketBytes) == 0);
#ifndef _WIN32
        Check(sigaction(SIGUSR1, &action, &previousAction) == 0);
        std::thread wakes([&] {
            for (unsigned i = 0; i < 20; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                pthread_kill(receiver, SIGUSR1);
            }
        });
#endif
        const auto started = std::chrono::steady_clock::now();
        auto unavailable =
            UdpExchange(ParseAddress(4, "127.0.0.1"), ntohs(bindAddress.sin_port), payload, rawResponse);
        const auto elapsed = std::chrono::steady_clock::now() - started;
#ifndef _WIN32
        wakes.join();
        Check(sigaction(SIGUSR1, &previousAction, nullptr) == 0);
#endif
        closeSocket(silent);
        Check(unavailable == Status::Unavailable && rawResponse.empty() &&
              elapsed >= std::chrono::milliseconds(75) && elapsed < std::chrono::milliseconds(250));
        // Deterministic malformed-input sweep through the production parser.
        uint32_t seed = 0x5610789;
        for (unsigned i = 0; i < 20000; ++i) {
            Frame fuzz{};
            seed = seed * 1664525 + 1013904223;
            fuzz.bytes = seed % (Mtu + 64);
            for (unsigned j = 0; j < Mtu; ++j) {
                seed = seed * 1664525 + 1013904223;
                fuzz.data[j] = uint8_t(seed >> 24);
            }
            Datagram parsed{};
            auto status = Parse(fuzz, parsed);
            if (status == Status::Ok)
                Check(parsed.payload >= fuzz.data && parsed.payload + parsed.payloadBytes <= fuzz.data + Mtu);
        }
        Check(std::filesystem::file_size(root / "network.pcap") >= 24);
        auto events = state::ReadFile(root / "network.json");
        Check(!events.empty());
        state::AtomicText(root / "network-tests.json",
                          "{\"schema\":10,\"passed\":true,\"checks\":" + std::to_string(checks) +
                              ",\"malformedInputs\":20000}\n");
        std::cout << checks << " network checks, 20000 malformed inputs PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
