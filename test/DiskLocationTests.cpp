#include "../src/microvm/state/VmManager.h"
#include "../src/microvm/storage/SparseDisk.h"
#include <array>
#include <iostream>
using namespace av::microvm;
namespace fs = std::filesystem;
namespace {
unsigned checks = 0;
void Check(bool v) {
    ++checks;
    state::Require(v, ("Disk location check " + std::to_string(checks)).c_str());
}
fs::path Location(state::VmManager &m, const std::string &name) {
    const auto json = m.Resources(name);
    const std::string key = "\"directory\":\"";
    const auto first = json.find(key);
    Check(first != json.npos);
    const auto at = first + key.size();
    auto path = json.substr(at, json.find('"', at) - at);
    std::string decoded;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '\\' && i + 1 < path.size() && path[i + 1] == '\\')
            ++i;
        decoded += path[i];
    }
    std::u8string utf8;
    for (unsigned char c : decoded)
        utf8 += char8_t(c);
    return fs::path(utf8);
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 3);
        const auto root = fs::absolute(argv[2]) / state::Token();
        const auto catalog = root / "catalog", a = root / "disk-a", b = root / "disk-b";
        fs::create_directories(a);
        fs::create_directory(b);
        state::AtomicText(a / "personal.txt", "do not erase");
        state::VmConfig config;
        config.id = "source";
        config.contract = "resources";
        config.ramMiB = 16;
        config.requestedVcpus = 1;
        config.hostReserveMiB = 1024;
        config.diskFolder = a;
        config.storageLimitBytes = 64ull << 20;
        {
            state::VmManager m(catalog);
            m.Create(config, argv[1]);
            const auto original = state::ReadFile(catalog / "source/config.gvc");
            const auto profile = hardware::HostReference();
            bool rejected = false;
            try {
                m.ConfigureHardware("source", profile);
            } catch (const std::exception &) {
                rejected = true;
            }
            Check(rejected && state::ReadFile(catalog / "source/config.gvc") == original);
            auto unsupported = config;
            unsupported.id = "unsupported";
            unsupported.hardware = profile;
            rejected = false;
            try {
                m.Create(unsupported, argv[1]);
            } catch (const std::exception &) {
                rejected = true;
            }
            Check(rejected && !fs::exists(catalog / "unsupported"));
        }
        for (int64_t fault = 0; fault < 32; ++fault) {
            {
                state::VmManager m(catalog);
                auto c = config;
                c.id = "creation";
                state::FailAfter(fault);
                try {
                    m.Create(c, argv[1]);
                } catch (const std::exception &) {
                }
                state::FailAfter(-1);
            }
            {
                state::VmManager m(catalog);
                if (fs::exists(catalog / "creation"))
                    m.Delete("creation");
                Check(state::ReadFile(a / "personal.txt").size() == 12);
            }
        }
        fs::path folder;
        {
            state::VmManager m(catalog);
            folder = Location(m, "source");
        }
        {
            state::VmManager m(catalog);
            const auto original = state::ReadFile(catalog / "source/config.gvc");
            bool rejected = false;
            try {
                m.ConfigureDiskFolder("source", folder);
            } catch (const std::exception &) {
                rejected = true;
            }
            Check(rejected && state::ReadFile(catalog / "source/config.gvc") == original);
            rejected = false;
            try {
                m.ConfigureDiskFolder("source", catalog / "source/snapshots");
            } catch (const std::exception &) {
                rejected = true;
            }
            Check(rejected && state::ReadFile(catalog / "source/config.gvc") == original);
            auto nested = config;
            nested.id = "nested";
            nested.diskFolder = folder;
            rejected = false;
            try {
                m.Create(nested, argv[1]);
            } catch (const std::exception &) {
                rejected = true;
            }
            Check(rejected && !fs::exists(catalog / "nested"));
        }
        {
            state::VmManager m(catalog);
            m.ConfigureDiskFolder("source", catalog);
            Check(Location(m, "source").parent_path() == catalog &&
                  m.List().find("source") != std::string::npos);
            m.ConfigureDiskFolder("source", a);
            folder = Location(m, "source");
        }
        std::vector<uint8_t> snapshot;
        {
            storage::SparseOptions o;
            o.directory = folder / "fixture";
            o.owner = "fixture";
            o.create = true;
            fs::create_directory(o.directory);
            {
                storage::SparseDisk disk(o);
                std::array<uint8_t, guardian::scalable::ChunkBytes> bytes{};
                bytes.fill(0x73);
                Check(disk.Io(guardian::scalable::BlockOp::Write, guardian::scalable::DataStart,
                              bytes.data()) == guardian::scalable::Status::Ok);
                snapshot = disk.Snapshot();
            }
            fs::remove_all(o.directory);
            state::AtomicWrite(folder / "disk.gvs", snapshot);
        }
        for (int64_t fault = 0; fault < 32; ++fault) {
            {
                state::VmManager m(catalog);
                const auto original = Location(m, "source");
                const auto parent = original.parent_path() == a ? b : a;
                state::FailAfter(fault);
                try {
                    m.ConfigureDiskFolder("source", parent);
                } catch (const std::exception &) {
                }
                state::FailAfter(-1);
            }
            {
                state::VmManager m(catalog);
                folder = Location(m, "source");
                Check(state::ReadFile(folder / "disk.gvs") == snapshot);
                Check(!fs::exists(catalog / "source/disk-move.gvt"));
                Check(state::ReadFile(a / "personal.txt").size() == 12);
            }
        }
        for (int64_t fault = 0; fault < 32; ++fault) {
            {
                state::VmManager m(catalog);
                state::FailAfter(fault);
                try {
                    m.Clone("source", "branch");
                } catch (const std::exception &) {
                }
                state::FailAfter(-1);
            }
            {
                state::VmManager m(catalog);
                if (fs::exists(catalog / "branch")) {
                    const auto clone = Location(m, "branch");
                    Check(clone != Location(m, "source") && state::ReadFile(clone / "disk.gvs") == snapshot);
                    m.Delete("branch");
                    Check(!fs::exists(clone));
                }
                Check(state::ReadFile(Location(m, "source") / "disk.gvs") == snapshot);
            }
        }
        {
            state::VmManager m(catalog);
            folder = Location(m, "source");
            const auto foreignGeneration = folder / ".active-123-1";
            fs::create_directory(foreignGeneration);
            state::AtomicText(foreignGeneration / ".guardian-disk-owner", "other-owner");
            state::AtomicText(foreignGeneration / "personal.txt", "preserve");
            bool refusedCleanup = false;
            try {
                m.Status("source");
            } catch (const std::exception &) {
                refusedCleanup = true;
            }
            Check(refusedCleanup && state::ReadFile(foreignGeneration / "personal.txt").size() == 8 &&
                  fs::exists(catalog / "source/config.gvc"));
            fs::remove_all(foreignGeneration);
            const auto external = folder / "external-managed";
            fs::create_directory(external);
            state::AtomicText(external / ".guardian-location-owner", "other-owner");
            state::AtomicText(external / "personal.txt", "preserve");
            bool rejected = false;
            try {
                m.Delete("source");
            } catch (const std::exception &) {
                rejected = true;
            }
            Check(rejected && fs::exists(catalog / "source/config.gvc") &&
                  state::ReadFile(external / "personal.txt").size() == 8);
            fs::remove_all(external);
            m.Delete("source");
            Check(!fs::exists(folder));
        }
        Check(state::ReadFile(a / "personal.txt").size() == 12);
        fs::remove_all(root);
        std::cout << checks
                  << " creation, independent clone, migration fault prefixes, recovery and owned deletion "
                     "checks PASS\n";
        return 0;
    } catch (const std::exception &e) {
        state::FailAfter(-1);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
