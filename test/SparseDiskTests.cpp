#include "../guest/ScalableVolume.h"
#include "../src/microvm/state/StorageBudget.h"
#include "../src/microvm/storage/SparseDisk.h"
#include <array>
#include <iostream>
#include <memory>
using namespace av::microvm;
using namespace guardian::scalable;
namespace {
unsigned checks = 0;
void Check(bool v) {
    ++checks;
    state::Require(v, ("Sparse disk check " + std::to_string(checks)).c_str());
}
template <class F> void Reject(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    Check(rejected);
}
struct Guest {
    std::unique_ptr<Metadata> a = std::make_unique<Metadata>(), b = std::make_unique<Metadata>();
    std::unique_ptr<uint8_t[]> scratch = std::make_unique<uint8_t[]>(ChunkBytes);
    Volume volume;
    Guest(storage::SparseDisk &d) {
        Check(volume.Configure(&d, storage::SparseDisk::Block, storage::SparseDisk::Space, a.get(), b.get(),
                               scratch.get(), d.Options().capacity, 16ull << 20));
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 2);
        const auto root = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(root);
        const auto run = root / state::Token();
        std::filesystem::create_directory(run);
        storage::SparseOptions o;
        o.directory = run / "original";
        o.owner = "vm-first";
        o.create = true;
        std::filesystem::create_directory(o.directory);
        std::vector<uint8_t> snapshot;
        std::array<uint8_t, ChunkBytes> data{}, read{};
        uint32_t n = 0;
        {
            storage::SparseDisk disk(o);
            Check(disk.Allocated() < 1ull << 20);
            Check(std::filesystem::file_size(o.directory / "payload.gvd2") > 2ull << 30);
            Reject([&] { storage::SparseDisk second(o); });
            auto wrong = o;
            wrong.owner = "other";
            Reject([&] { storage::SparseDisk other(wrong); });
            Guest guest(disk);
            Check(guest.volume.Mount(true) == Status::Ok);
            Check(guest.volume.Create("large.bin") == Status::Ok);
            for (uint32_t block = 0; block < 80; ++block) {
                data.fill(uint8_t(block));
                Check(guest.volume.Write("large.bin", uint64_t(block) * ChunkBytes, data.data(), ChunkBytes,
                                         n) == Status::Ok &&
                      n == ChunkBytes);
            }
            Check(disk.Allocated() < 8ull << 20);
            snapshot = disk.Snapshot();
            Check(snapshot.size() < 8ull << 20);
            Reject([&] { disk.Restore(snapshot); });
        }
        o.create = false;
        {
            storage::SparseDisk disk(o);
            Guest guest(disk);
            Check(guest.volume.Mount(false) == Status::Ok);
            for (uint32_t block = 0; block < 80; ++block) {
                Check(guest.volume.Read("large.bin", uint64_t(block) * ChunkBytes, read.data(), ChunkBytes,
                                        n) == Status::Ok &&
                      n == ChunkBytes);
                for (auto c : read)
                    if (c != uint8_t(block))
                        throw std::runtime_error("Persistent sparse disk content changed");
            }
            const auto allocated = disk.Allocated();
            Check(guest.volume.Remove("large.bin") == Status::Ok);
            Check(disk.Allocated() + 4ull * 1024 * 1024 < allocated);
        }
        const auto alias = run / "unrelated-hard-link";
        std::filesystem::create_hard_link(o.directory / "payload.gvd2", alias);
        Reject([&] { storage::SparseDisk disk(o); });
        std::filesystem::remove(alias);
        auto clone = o;
        clone.directory = run / "clone";
        clone.owner = "vm-clone";
        clone.create = true;
        std::filesystem::create_directory(clone.directory);
        {
            storage::SparseDisk disk(clone);
            auto invalid = snapshot;
            invalid.back() ^= 1;
            Reject([&] { disk.Restore(invalid); });
            disk.Restore(snapshot);
            Guest guest(disk);
            Check(guest.volume.Mount(false) == Status::Ok);
            uint64_t size = 0;
            Check(guest.volume.Size("large.bin", size) == Status::Ok && size == 5ull << 20);
        }
        {
            storage::SparseDisk disk(o);
            Guest guest(disk);
            Check(guest.volume.Mount(false) == Status::Ok);
            uint64_t size = 0;
            Check(guest.volume.Size("large.bin", size) == Status::NotFound);
        }
        auto limited = o;
        limited.directory = run / "limited";
        limited.owner = "limited";
        limited.growthLimit = 4ull << 20;
        limited.create = true;
        std::filesystem::create_directory(limited.directory);
        {
            storage::SparseDisk disk(limited);
            Reject([&] { disk.Restore(snapshot); });
            data.fill(0x42);
            uint32_t blocks = 0;
            while (disk.Io(BlockOp::Write, uint64_t(blocks) * ChunkBytes, data.data()) == Status::Ok) {
                ++blocks;
                Check(blocks < 64);
            }
            Check(blocks > 30 && blocks < 64 && disk.Available() == 0);
            Check(disk.Allocated() <= limited.growthLimit && disk.SnapshotCharge() <= limited.growthLimit);
            Check(disk.Io(BlockOp::Write, 0, data.data()) == Status::Ok);
            Check(disk.Io(BlockOp::Discard, 0, nullptr) == Status::Ok && disk.Available() >= ChunkBytes);
            Check(disk.Io(BlockOp::Read, 0, read.data()) == Status::Ok);
            for (auto c : read)
                if (c)
                    throw std::runtime_error("Discard leaked old contents");
        }
        auto foreign = o;
        foreign.create = true;
        foreign.directory = run / "foreign";
        std::filesystem::create_directory(foreign.directory);
        state::AtomicText(foreign.directory / "user.txt", "preserve");
        Reject([&] { storage::SparseDisk disk(foreign); });
        Check(state::ReadFile(foreign.directory / "user.txt").size() == 8);
#ifndef _WIN32
        auto linked = o;
        linked.directory = run / "linked";
        std::filesystem::create_directory_symlink(o.directory, linked.directory);
        Reject([&] { storage::SparseDisk disk(linked); });
#endif
        std::filesystem::remove_all(run);
        std::cout << checks
                  << " sparse backing, ownership, allocation, persistence, clone and quota checks PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
