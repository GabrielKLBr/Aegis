#include "Network.h"
#include <algorithm>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace av::microvm::net {
namespace {
namespace fs = std::filesystem;
void Directory(const fs::path &p) {
    std::error_code ec;
    auto s = fs::symlink_status(p, ec);
    state::Require(!ec && fs::is_directory(s) && !fs::is_symlink(s), "Lab directory rejected");
#ifdef _WIN32
    auto a = GetFileAttributesW(p.c_str());
    state::Require(a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_REPARSE_POINT),
                   "Lab reparse point rejected");
#endif
}
std::unique_ptr<state::Lease> Lock(const fs::path &p) {
    // A queue publication includes durable file and directory flushes. Those
    // can exceed 100 ms on a virtual disk even when the peer is healthy.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    do {
        auto l = std::make_unique<state::Lease>(p, true);
        if (l->Held())
            return l;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Lab switch busy");
}
struct Member {
    std::string token;
    Policy policy;
};
Member ReadMember(const fs::path &p) {
    auto b = state::Open(state::ReadFile(p, 8192));
    state::Reader r(b);
    Member m;
    m.token = r.Text(128);
    m.policy = DecodePolicy(r.Bytes(4096));
    r.End();
    return m;
}
class Endpoint {
  public:
    explicit Endpoint(const Options &o) : policy(o.policy), token(state::Token()), capture(o.capture) {
        Validate(policy);
        state::Require(policy.mode == Mode::Lab && !o.catalog.empty(), "Lab catalog required");
        Directory(o.catalog);
        // Catalog marker must exist; never adopt an arbitrary shared directory.
        state::ReadFile(o.catalog / ".owner", 4096);
        auto setup = Lock(o.catalog / ".network.lock");
        path = o.catalog / (".lab-" + policy.lab);
        if (!fs::exists(path)) {
            unsigned labs = 0;
            for (const auto &e : fs::directory_iterator(o.catalog))
                if (e.path().filename().string().starts_with(".lab-"))
                    ++labs;
            state::Require(labs < 16, "Lab count quota reached");
            fs::create_directory(path);
            state::AtomicText(path / ".owner", "GuardianAV lab v1\n" + policy.lab + "\n");
        }
        Directory(path);
        auto mark = state::ReadFile(path / ".owner", 128);
        const auto expected = "GuardianAV lab v1\n" + policy.lab + "\n";
        state::Require(std::string(mark.begin(), mark.end()) == expected, "Foreign lab directory rejected");
        prefix = "node-" + std::to_string(policy.node);
        ownership = std::make_unique<state::Lease>(path / (prefix + ".lock"));
        auto lock = Lock(path / ".bus.lock");
        for (unsigned i = 0; i < Depth; ++i)
            Remove(path / (prefix + "-" + std::to_string(i) + ".packet"));
        state::Writer w;
        w.Text(token);
        w.Bytes(EncodePolicy(policy));
        state::AtomicWrite(path / (prefix + ".member"), state::Seal(w.data));
    }
    ~Endpoint() {
        try {
            Directory(path);
            auto lock = Lock(path / ".bus.lock");
            if (ReadMember(path / (prefix + ".member")).token == token) {
                Remove(path / (prefix + ".member"));
                for (unsigned i = 0; i < Depth; ++i)
                    Remove(path / (prefix + "-" + std::to_string(i) + ".packet"));
            }
        } catch (...) {
        }
    }
    Status Exchange(const Frame *input, Frame &output) {
        try {
            Directory(path);
            auto lock = Lock(path / ".bus.lock");
            if (input) {
                Datagram d{};
                if (Parse(*input, d) != Status::Ok)
                    return Status::Malformed;
                uint32_t node = 0;
                for (auto n : policy.peers)
                    if (Equal(d.target, Local(d.family, n)))
                        node = n;
                if (!node)
                    return Status::Denied;
                const auto peer = "node-" + std::to_string(node);
                state::Lease probe(path / (peer + ".lock"), true);
                if (probe.Held())
                    return Status::Unavailable;
                const auto member = ReadMember(path / (peer + ".member"));
                if (member.policy.mode != Mode::Lab || member.policy.lab != policy.lab ||
                    member.policy.node != node ||
                    std::find(member.policy.peers.begin(), member.policy.peers.end(), policy.node) ==
                        member.policy.peers.end() ||
                    std::find(member.policy.ports.begin(), member.policy.ports.end(), d.targetPort) ==
                        member.policy.ports.end())
                    return Status::Denied;
                for (unsigned i = 0; i < Depth; ++i) {
                    auto p = path / (peer + "-" + std::to_string(i) + ".packet");
                    if (fs::exists(p))
                        continue;
                    state::Writer w;
                    w.Text(member.token);
                    w.Pod(*input);
                    state::AtomicWrite(p, state::Seal(w.data));
                    return Status::Ok;
                }
                return Status::Full;
            }
            for (unsigned i = 0; i < Depth; ++i) {
                auto p = path / (prefix + "-" + std::to_string(i) + ".packet");
                if (!fs::exists(p))
                    continue;
                auto b = state::Open(state::ReadFile(p, 2048));
                state::Reader r(b);
                auto target = r.Text(128);
                auto f = r.Pod<Frame>();
                r.End();
                Remove(p);
                if (target != token)
                    continue;
                output = f;
                return Status::Ok;
            }
            lock.reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return Status::Empty;
        } catch (const std::exception &error) {
            if(!capture.empty())try{state::AtomicText(capture/"lab-error.txt",std::string(error.what()).substr(0,1024));}catch(...){}
            return Status::Unavailable;
        }
    }

  private:
    static void Remove(const fs::path &p) {
        if (fs::exists(fs::symlink_status(p))) {
            state::Require(state::Ordinary(p), "Lab queue file rejected");
            fs::remove(p);
        }
    }
    Policy policy;
    std::string token, prefix;
    fs::path path,capture;
    std::unique_ptr<state::Lease> ownership;
};
} // namespace
Transport LabTransport(const Options &o) {
    auto endpoint = std::make_shared<Endpoint>(o);
    return [endpoint](const Frame *f, Frame &out) { return endpoint->Exchange(f, out); };
}
} // namespace av::microvm::net
