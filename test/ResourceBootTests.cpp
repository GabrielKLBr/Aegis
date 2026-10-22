#include "../src/microvm/boot/KernelBoot.h"
#include "../src/microvm/boot/ResourceSession.h"
#include <iostream>
using namespace av::microvm;
namespace {
unsigned checks = 0;
void Check(bool v) {
    ++checks;
    state::Require(v, ("Resource boot check " + std::to_string(checks)).c_str());
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 3);
        const auto image = boot::KernelImage::Read(argv[1]);
        const auto root = std::filesystem::path(argv[2]) / state::Token();
        std::filesystem::create_directories(root);
        for (uint32_t cpus : {1u, 2u, 3u, 4u}) {
            storage::SparseOptions o;
            o.directory = root / ("disk-" + std::to_string(cpus));
            o.owner = "owner-" + std::to_string(cpus);
            o.create = true;
            std::filesystem::create_directory(o.directory);
            boot::SessionSnapshot saved;
            {
                boot::ResourceSession session;
                session.Prepare(image, 32ull << 20, 400 + cpus, cpus, o, 16ull << 20);
                session.PauseAtRequest(cpus == 4 ? 1 : 120);
                auto result = session.Run(std::chrono::seconds(20));
                Check(result.paused && !result.timedOut);
                saved = session.Capture();
                Check(!saved.disk.empty() && saved.machine.ram.size() == 32ull << 20);
            }
            auto clone = o;
            clone.directory = root / ("restored-" + std::to_string(cpus));
            std::filesystem::create_directory(clone.directory);
            {
                storage::SparseDisk candidate(clone);
                candidate.Restore(saved.disk);
            }
            clone.create = false;
            {
                boot::ResourceSession session;
                session.Prepare(image, 32ull << 20, 400 + cpus, cpus, clone, 16ull << 20);
                session.Restore(saved);
                const auto result = session.Run(std::chrono::seconds(20));
                if (!result.success)
                    std::cerr << "cpus=" << cpus << " error=" << result.error
                              << " checks=" << result.resource.checks
                              << " topology=" << result.resource.topologyMask
                              << " ready=" << result.resource.readyMask
                              << " counter=" << result.resource.counter << '\n';
                if (!result.success) {
                    for (auto v : result.resource.cpuid[0])
                        std::cerr << std::hex << v << " ";
                    std::cerr << std::dec << "\n";
                }
                Check(result.success && result.resourceReportValid);
                Check(result.resource.fpuMask == ((1u << cpus) - 1));
                Check(result.resource.vcpus == cpus &&
                      result.resource.counter == uint64_t(cpus) * guardian::resource::Iterations &&
                      result.resource.topologyMask == ((1u << cpus) - 1));
                Check(result.resource.fileBytes == 5ull << 20 && result.resource.capacity == 2ull << 30);
            }
        }
        for (const auto timeout : {1, 40}) {
            storage::SparseOptions o;
            o.directory = root / ("deadline-" + std::to_string(timeout));
            o.owner = "deadline";
            o.create = true;
            std::filesystem::create_directory(o.directory);
            boot::ResourceSession session;
            session.Prepare(image, 16ull << 20, 900, 4, o, 16ull << 20);
            auto *ram = static_cast<uint8_t *>(session.Machine().RamData());
            ram[image.Entry()] = 0xEB;
            ram[image.Entry() + 1] = 0xFE;
            const auto result = session.Run(std::chrono::milliseconds(timeout));
            Check(!result.success && result.timedOut);
        }
        std::filesystem::remove_all(root);
        std::cout << checks << " real SMP and scalable guest boot/checkpoint/restore checks PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
