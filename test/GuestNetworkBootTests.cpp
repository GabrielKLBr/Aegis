#include "../src/microvm/boot/KernelBoot.h"
#include <exception>
#include <barrier>
#include <iostream>
#include <thread>
using namespace av::microvm;
using namespace av::microvm::boot;
namespace {
uint32_t checks = 0;
void Check(bool ok) {
    ++checks;
    if (!ok)
        throw std::runtime_error("Network boot check " + std::to_string(checks));
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 3);
        Check(WhpAvailable());
        const auto image = KernelImage::Read(argv[1]);
        const auto root = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(root);
        const auto temp = root / state::Token();
        std::filesystem::create_directory(temp);
        state::AtomicText(temp / ".owner", "GuardianVM catalog\n");
        CpuOptions cpu;
        cpu.enabled = true;
        cpu.network = true;
        std::vector<BootResult> rows;
        for (auto ram : {16u, 32u, 64u})
            for (auto mode : {guardian::net::Mode::Offline, guardian::net::Mode::Simulated}) {
                net::Options options;
                options.policy.mode = mode;
                options.capture = temp;
                KernelBootSession session;
                session.Prepare(image, uint64_t(ram) << 20, ram + uint32_t(mode), cpu, {}, options);
                auto r = session.Run(std::chrono::seconds(20));
                if (!r.success)
                    std::cerr << r.error << " | " << r.console << '\n';
                Check(r.success && r.networkReportValid &&
                      r.network.checks == guardian::net::RequiredChecks && r.network.userCalls >= 9);
                Check(r.networkDenied == (mode == guardian::net::Mode::Offline ? 6u : 2u));
                Check(r.networkReceived == (mode == guardian::net::Mode::Offline ? 0u : 4u));
                rows.push_back(r);
            }
        for (auto ram : {16u, 32u, 64u}) {
            net::Options options;
            options.policy.mode = guardian::net::Mode::Simulated;
            KernelBootSession original;
            original.Prepare(image, uint64_t(ram) << 20, 200 + ram, cpu, {}, options);
            original.PauseAtNetworkRequest(1);
            auto paused = original.Run(std::chrono::seconds(20));
            Check(paused.paused && !paused.success && paused.networkSent == 1 && paused.networkReceived == 0);
            auto point = original.Capture();
            for (unsigned n = 0; n < 2; ++n) {
                KernelBootSession candidate;
                candidate.Prepare(image, uint64_t(ram) << 20, point.BootId(), cpu, {}, options);
                candidate.Restore(point);
                auto r = candidate.Run(std::chrono::seconds(20));
                Check(r.success && r.networkSent == 4 && r.networkReceived == 4 && r.networkDenied == 2);
                rows.push_back(r);
            }
            auto wrong = options;
            wrong.policy.packetLimit = 127;
            KernelBootSession rejected;
            rejected.Prepare(image, uint64_t(ram) << 20, point.BootId(), cpu, {}, wrong);
            bool caught = false;
            try {
                rejected.Restore(point);
            } catch (const std::exception &) {
                caught = true;
            }
            Check(caught);
        }
        // Four real guests, two isolated networks, identical IP addresses reused
        // across networks. Both directions and IPv4/IPv6 must deliver real bytes.
        std::unique_ptr<KernelBootSession> guests[4];
        BootResult results[4];
        std::exception_ptr failures[4];
        net::Options endpoints[4];
        for (unsigned i = 0; i < 4; ++i) {
            auto &o = endpoints[i];
            o.policy.mode = guardian::net::Mode::Lab;
            o.policy.lab = i < 2 ? "alpha" : "beta";
            o.policy.node = i % 2 + 1;
            o.policy.peers = {i % 2 ? 1u : 2u};
            o.policy.ports = {40000};
            o.catalog = temp;
            o.capture = temp / ("guest-" + std::to_string(i));
            std::filesystem::create_directory(o.capture);
            o.transport = net::LabTransport(o);
            guests[i] = std::make_unique<KernelBootSession>();
            guests[i]->Prepare(image, 32ull << 20, 300 + i, cpu, {}, o);
        }
        std::thread threads[4];
        std::barrier start(5);
        for (unsigned i = 0; i < 4; ++i)
            threads[i] = std::thread([&, i] {
                start.arrive_and_wait();
                try {
                    results[i] = guests[i]->Run(std::chrono::seconds(20));
                } catch (...) {
                    failures[i] = std::current_exception();
                }
            });
        start.arrive_and_wait();
        for (auto &t : threads)
            t.join();
        // Preserve all four outcomes before an assertion can leave this scope.
        WriteBootReport(root / "network-concurrent-boots.json", {results[0],results[1],results[2],results[3]});
        for (unsigned i = 0; i < 4; ++i) {
            if (failures[i])
                std::rethrow_exception(failures[i]);
            if (!results[i].success)
                std::cerr << results[i].error << " | " << results[i].console << '\n';
            Check(results[i].success && results[i].networkSent == 4 && results[i].networkReceived == 4 &&
                  results[i].networkDenied == 2);
            rows.push_back(results[i]);
        }
        Check(rows.size() == 16);
        WriteBootReport(root / "network-boots.json", rows);
        state::AtomicText(root / "network-boot-tests.json",
                          "{\"schema\":10,\"passed\":true,\"boots\":16,\"paused\":3,\"checks\":" +
                              std::to_string(checks) + "}\n");
        std::cout << checks << " network boot checks, 16 real boots/resumes PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
