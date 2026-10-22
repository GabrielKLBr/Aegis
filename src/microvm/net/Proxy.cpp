#ifdef _WIN32
// Winsock must precede Windows, which supplies BCrypt/IP helper scalar types.
// clang-format off
#include <winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <bcrypt.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "Network.h"
#include <chrono>
#include <cstring>
namespace av::microvm::net {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket Invalid = INVALID_SOCKET;
void Close(Socket s) { closesocket(s); }
bool Initialize() {
    static const bool ok = []() {
        WSADATA w{};
        return WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }();
    return ok;
}
#else
using Socket = int;
constexpr Socket Invalid = -1;
void Close(Socket s) { close(s); }
bool Initialize() { return true; }
#endif
using Deadline = std::chrono::steady_clock::time_point;
bool RetrySocketError() {
#ifdef _WIN32
    const auto e = WSAGetLastError();
    return e == WSAEINTR || e == WSAEWOULDBLOCK;
#else
    return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
bool ReadySocket(Socket socket, bool writing, Deadline deadline) {
    for (;;) {
        const auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now())
                .count();
        if (us <= 0)
            return false;
#ifdef _WIN32
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket, &set);
        timeval timeout{long(us / 1000000), long(us % 1000000)};
        const auto n = select(0, writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &timeout);
        if (n < 0 && WSAGetLastError() == WSAEINTR)
            continue;
        return n > 0;
#else
        pollfd descriptor{socket, short(writing ? POLLOUT : POLLIN), 0};
        const auto n = poll(&descriptor, 1, int((us + 999) / 1000));
        if (n < 0 && errno == EINTR)
            continue;
        return n > 0 && (descriptor.revents & descriptor.events);
#endif
    }
}
bool LocalRelay(const Address &address) {
#ifdef _WIN32
    ULONG size = 16384;
    std::vector<uint8_t> data(size);
    auto *list = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(data.data());
    auto err = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, list, &size);
    if (err == ERROR_BUFFER_OVERFLOW) {
        data.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(data.data());
        err = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, list, &size);
    }
    if (err != NO_ERROR)
        return true;
    for (auto *a = list; a; a = a->Next)
        for (auto *u = a->FirstUnicastAddress; u; u = u->Next)
            if (u->Address.lpSockaddr && u->Address.iSockaddrLength >= int(sizeof(sockaddr_in)) &&
                u->Address.lpSockaddr->sa_family == AF_INET) {
                const auto *ip = reinterpret_cast<sockaddr_in *>(u->Address.lpSockaddr);
                const auto *p = reinterpret_cast<const uint8_t *>(&ip->sin_addr);
                const unsigned bits = u->OnLinkPrefixLength;
                if (bits > 32)
                    return true;
                bool same = true;
                for (unsigned i = 0; i < bits; ++i)
                    if ((address.bytes[i / 8] & (128u >> (i % 8))) != (p[i / 8] & (128u >> (i % 8))))
                        same = false;
                if (same)
                    return true;
            }
#else
    ifaddrs *list = nullptr;
    if (getifaddrs(&list))
        return true;
    bool local = false;
    for (auto *a = list; a; a = a->ifa_next)
        if (a->ifa_addr && a->ifa_netmask && a->ifa_addr->sa_family == AF_INET) {
            const auto *ip = reinterpret_cast<const sockaddr_in *>(a->ifa_addr);
            const auto *mask = reinterpret_cast<const sockaddr_in *>(a->ifa_netmask);
            uint32_t candidate = 0;
            std::memcpy(&candidate, address.bytes, 4);
            if ((candidate & mask->sin_addr.s_addr) == (ip->sin_addr.s_addr & mask->sin_addr.s_addr))
                local = true;
        }
    freeifaddrs(list);
    return local;
#endif
    return false;
}
bool Random(uint8_t *p, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, p, ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    return getrandom(p, n, 0) == ssize_t(n);
#endif
}
} // namespace
Status UdpExchange(const Address &target, uint16_t port, std::span<const uint8_t> request,
                   std::vector<uint8_t> &response) {
    response.clear();
    if (!Initialize() || !port || request.empty() || request.size() > Mtu + 32)
        return Status::Unavailable;
#ifdef _WIN32
    Socket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    Socket socket = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
#endif
    if (socket == Invalid)
        return Status::Unavailable;
    struct Guard {
        Socket socket;
        ~Guard() { Close(socket); }
    } guard{socket};
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);
    std::memcpy(&remote.sin_addr, target.bytes, 4);
#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking))
        return Status::Unavailable;
#else
    const auto flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK))
        return Status::Unavailable;
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    if (connect(socket, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)))
        return Status::Unavailable;
    // VM wake signals may interrupt poll/recv. Retrying uses one absolute
    // deadline, so signals cannot shorten a valid wait or extend the budget.
    for (;;) {
        if (!ReadySocket(socket, true, deadline))
            return Status::Unavailable;
        const auto sent =
            send(socket, reinterpret_cast<const char *>(request.data()), int(request.size()), 0);
        if (sent == int(request.size()))
            break;
        if (sent < 0 && RetrySocketError())
            continue;
        return Status::Unavailable;
    }
    uint8_t buffer[Mtu + 33]{}; // Extra byte detects overlong/truncated replies.
    for (;;) {
        if (!ReadySocket(socket, false, deadline))
            return Status::Unavailable;
        const auto n = recv(socket, reinterpret_cast<char *>(buffer), sizeof(buffer), 0);
        if (n < 0 && RetrySocketError())
            continue;
        if (n <= 0 || n > int(Mtu + 32))
            return Status::Unavailable;
        response.assign(buffer, buffer + n);
        return Status::Ok;
    }
}
Transport ProxyTransport(const Policy &policy, RelayExchange exchange) {
    Validate(policy);
    state::Require(policy.mode == Mode::Proxy && !LocalRelay(policy.relay),
                   "Proxy relay belongs to a host interface/subnet or cannot be checked");
    return [policy, exchange](const Frame *input, Frame &output) {
        if (!input)
            return Status::Empty;
        Datagram original{};
        if (Parse(*input, original) != Status::Ok)
            return Status::Malformed;
        bool allowed = false;
        for (const auto &rule : policy.destinations)
            if (rule.family == original.family && rule.port == original.targetPort &&
                Equal(rule.address, original.target))
                allowed = true;
        if (!allowed)
            return Status::Denied;
        if (LocalRelay(policy.relay))
            return Status::Denied; // Re-check after interface changes.
        std::vector<uint8_t> request(32 + input->bytes);
        const uint8_t magic[8] = {'G', 'V', 'U', 'D', 'P', '0', '1', 0};
        std::memcpy(request.data(), magic, 8);
        if (!Random(request.data() + 8, 16))
            return Status::Unavailable;
        Write16(request.data() + 24, uint16_t(input->bytes));
        std::memcpy(request.data() + 32, input->data, input->bytes);
        std::vector<uint8_t> response;
        auto status = exchange ? exchange(policy.relay, policy.relayPort, request, response)
                               : UdpExchange(policy.relay, policy.relayPort, request, response);
        if (status != Status::Ok)
            return status;
        if (response.size() < 32 || std::memcmp(response.data(), request.data(), 24) || response[26] ||
            response[27] || response[28] || response[29] || response[30] || response[31] ||
            size_t(Read16(response.data() + 24)) + 32 != response.size() || response.size() > Mtu + 32)
            return Status::Malformed;
        output = {};
        output.bytes = uint32_t(response.size() - 32);
        std::memcpy(output.data, response.data() + 32, output.bytes);
        Datagram reply{};
        if (Parse(output, reply) != Status::Ok || reply.family != original.family ||
            !Equal(reply.source, original.target) || !Equal(reply.target, original.source) ||
            reply.sourcePort != original.targetPort || reply.targetPort != original.sourcePort) {
            output = {};
            return Status::Denied;
        }
        return Status::Ok;
    };
}
} // namespace av::microvm::net
