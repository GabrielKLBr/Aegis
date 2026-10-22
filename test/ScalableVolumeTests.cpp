#include "../guest/ScalableVolume.h"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace guardian::scalable;
namespace {
uint32_t checks = 0;
void Check(bool v) {
    ++checks;
    if (!v)
        throw std::runtime_error("Scalable volume check " + std::to_string(checks));
}
struct Disk {
    std::map<uint64_t, std::vector<uint8_t>> blocks;
    uint64_t capacity = 2ull << 30, limit = 32ull << 20;
    int64_t operations = -1;
    static Status Io(void *p, BlockOp op, uint64_t offset, uint8_t *data) noexcept {
        auto &d = *static_cast<Disk *>(p);
        if (op == BlockOp::AllocationMap) {
            if (offset || !data)
                return Status::Invalid;
            std::memset(data, 0, ChunkBytes);
            for (const auto &[at, value] : d.blocks) {
                (void)value;
                const auto b = at / ChunkBytes;
                data[b / 8] |= uint8_t(1u << (b % 8));
            }
            return Status::Ok;
        }
        if (op == BlockOp::Flush) {
            if (d.operations == 0)
                return Status::IoError;
            if (d.operations > 0)
                --d.operations;
            return Status::Ok;
        }
        if (offset % ChunkBytes || offset > d.capacity || ChunkBytes > d.capacity - offset)
            return Status::Invalid;
        if (op == BlockOp::Read) {
            if (!data)
                return Status::Invalid;
            auto it = d.blocks.find(offset);
            if (it == d.blocks.end())
                std::memset(data, 0, ChunkBytes);
            else
                std::memcpy(data, it->second.data(), ChunkBytes);
            return Status::Ok;
        }
        if (d.operations == 0)
            return Status::IoError;
        if (d.operations > 0)
            --d.operations;
        if (op == BlockOp::Discard) {
            d.blocks.erase(offset);
            return Status::Ok;
        }
        if (op != BlockOp::Write || !data)
            return Status::Invalid;
        if (!d.blocks.contains(offset) && d.blocks.size() * ChunkBytes >= d.limit)
            return Status::NoSpace;
        d.blocks[offset] = std::vector<uint8_t>(data, data + ChunkBytes);
        return Status::Ok;
    }
    static uint64_t Available(void *p) noexcept {
        const auto &d = *static_cast<Disk *>(p);
        return d.blocks.size() * ChunkBytes < d.limit ? d.limit - d.blocks.size() * ChunkBytes : 0;
    }
};
struct Fixture {
    std::unique_ptr<Metadata> one = std::make_unique<Metadata>(), two = std::make_unique<Metadata>();
    std::vector<uint8_t> scratch = std::vector<uint8_t>(ChunkBytes);
    Volume volume;
    Fixture(Disk &d, uint64_t quota = 16ull << 20) {
        Check(volume.Configure(&d, Disk::Io, Disk::Available, one.get(), two.get(), scratch.data(),
                               d.capacity, quota));
    }
};
} // namespace
int main() {
    try {
        Disk disk;
        Fixture f(disk);
        Check(f.volume.Mount(true) == Status::Ok);
        auto space = f.volume.QuerySpace();
        Check(space.capacity == 2ull << 30 && space.allocated == DataStart && space.available == 14ull << 20);
        Check(f.volume.Create("large.bin") == Status::Ok);
        Check(f.volume.Create("LARGE.BIN") == Status::Exists);
        Check(f.volume.Create("../bad") == Status::Invalid);
        std::vector<uint8_t> data(ChunkBytes), read(ChunkBytes);
        uint32_t n = 0;
        for (uint32_t block = 0; block < 80; ++block) {
            for (uint32_t i = 0; i < ChunkBytes; ++i)
                data[i] = uint8_t(block * 7 + i);
            Check(f.volume.Write("large.bin", uint64_t(block) * ChunkBytes, data.data(), ChunkBytes, n) ==
                      Status::Ok &&
                  n == ChunkBytes);
        }
        uint64_t bytes = 0;
        Check(f.volume.Size("large.bin", bytes) == Status::Ok && bytes == 80ull * ChunkBytes);
        Fixture reopened(disk);
        Check(reopened.volume.Mount(false) == Status::Ok);
        for (uint32_t block = 0; block < 80; ++block) {
            Check(reopened.volume.Read("large.bin", uint64_t(block) * ChunkBytes, read.data(), ChunkBytes,
                                       n) == Status::Ok &&
                  n == ChunkBytes);
            for (uint32_t i = 0; i < ChunkBytes; ++i)
                if (read[i] != uint8_t(block * 7 + i))
                    throw std::runtime_error("Persistent extent content changed");
        }
        Check(reopened.volume.Truncate("large.bin", ChunkBytes + 17) == Status::Ok);
        Check(reopened.volume.Truncate("large.bin", 3ull * ChunkBytes) == Status::Ok);
        Check(reopened.volume.Read("large.bin", ChunkBytes, read.data(), ChunkBytes, n) == Status::Ok &&
              n == ChunkBytes);
        for (uint32_t i = 0; i < ChunkBytes; ++i)
            Check(read[i] == (i < 17 ? uint8_t(7 + i) : 0));
        const auto before = disk.blocks.size();
        Check(reopened.volume.Remove("large.bin") == Status::Ok);
        Check(disk.blocks.size() < before);
        Check(reopened.volume.QuerySpace().allocated == DataStart);
        Check(reopened.volume.Create("quota.bin") == Status::Ok);
        disk.limit = disk.blocks.size() * ChunkBytes + 3 * ChunkBytes;
        uint32_t writes = 0;
        while (reopened.volume.Write("quota.bin", uint64_t(writes) * ChunkBytes, data.data(), ChunkBytes,
                                     n) == Status::Ok) {
            ++writes;
            if (writes > 10)
                throw std::runtime_error("Physical quota bypassed");
        }
        Check(writes <= 3 && reopened.volume.QuerySpace().available == 0);
        Check(reopened.volume.Remove("quota.bin") == Status::Ok);
        Check(reopened.volume.QuerySpace().available > 0);
        // Every failed write prefix must preserve the last committed file generation.
        Disk base;
        Fixture initial(base);
        Check(initial.volume.Mount(true) == Status::Ok);
        Check(initial.volume.Create("atomic.bin") == Status::Ok);
        std::fill(data.begin(), data.end(), 0x31);
        Check(initial.volume.Write("atomic.bin", 0, data.data(), ChunkBytes, n) == Status::Ok);
        for (int64_t budget = 0; budget < 16; ++budget) {
            Disk interrupted = base;
            Fixture test(interrupted);
            Check(test.volume.Mount(false) == Status::Ok);
            interrupted.operations = budget;
            std::fill(data.begin(), data.end(), 0x62);
            const auto result = test.volume.Write("atomic.bin", 0, data.data(), ChunkBytes, n);
            interrupted.operations = -1;
            Fixture boot(interrupted);
            Check(boot.volume.Mount(false) == Status::Ok);
            Check(boot.volume.Read("atomic.bin", 0, read.data(), ChunkBytes, n) == Status::Ok &&
                  n == ChunkBytes);
            const auto value = read[0];
            Check(value == 0x31 || value == 0x62);
            Check(interrupted.blocks.size() <= base.blocks.size() + 1);
            for (auto c : read)
                if (c != value)
                    throw std::runtime_error("Torn file publication");
            if (result == Status::Ok)
                Check(value == 0x62);
        }
        for (int64_t budget = 0; budget < 28; ++budget) {
            Disk virgin;
            virgin.operations = budget;
            Fixture test(virgin);
            test.volume.Mount(true);
            virgin.operations = -1;
            Fixture reboot(virgin);
            const auto recovered = reboot.volume.Mount(true);
            Check(recovered == Status::Ok);
            if (recovered == Status::Ok)
                Check(reboot.volume.QuerySpace().allocated == DataStart);
        }
        Disk sparse;
        Fixture sparseFixture(sparse);
        Check(sparseFixture.volume.Mount(true) == Status::Ok);
        Check(sparseFixture.volume.Create("edge.bin") == Status::Ok);
        Check(sparseFixture.volume.Truncate("edge.bin", MaxFileBytes) == Status::Ok);
        data[0] = 0x7B;
        Check(sparseFixture.volume.Write("edge.bin", MaxFileBytes - 1, data.data(), 1, n) == Status::Ok &&
              n == 1);
        Check(sparseFixture.volume.Read("edge.bin", MaxFileBytes - ChunkBytes, read.data(), ChunkBytes, n) ==
                  Status::Ok &&
              n == ChunkBytes);
        Check(read.back() == 0x7B);
        for (uint32_t i = 0; i < ChunkBytes - 1; ++i)
            if (read[i])
                throw std::runtime_error("Sparse gap was not zero");
        Check(sparseFixture.volume.Write("edge.bin", MaxFileBytes, data.data(), 1, n) ==
                  Status::FileTooLarge &&
              n == 0);
        Check(sparseFixture.volume.Write("edge.bin", UINT64_MAX, data.data(), 1, n) == Status::FileTooLarge);
        Check(sparseFixture.volume.Write("edge.bin", 0, sparseFixture.scratch.data(), 1, n) ==
              Status::Invalid);
        Check(sparseFixture.volume.Read("edge.bin", 0, sparseFixture.scratch.data(), 1, n) ==
              Status::Invalid);
        Disk unknown;
        unknown.blocks[0] = std::vector<uint8_t>(ChunkBytes, 0x91);
        Fixture unknownFixture(unknown);
        Check(unknownFixture.volume.Mount(true) == Status::Corrupt);
        std::cout << checks
                  << " scalable volume checks; 2 GiB logical volume, 5 MiB file, physical quota, delete, "
                     "sparse tail and crash prefixes PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
