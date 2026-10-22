#pragma once
#include "../state/Files.h"
#include "guardian/ScalableStorage.h"
#include <filesystem>
#include <functional>
#include <set>
namespace av::microvm::storage {
struct SparseOptions {
    std::filesystem::path directory;
    std::string owner;
    uint64_t capacity = 2ull << 30, growthLimit = 64ull << 20, hostReserve = 256ull << 20;
    bool create = false;
    std::function<uint64_t()> aggregateAvailable;
};
class SparseDisk {
  public:
    explicit SparseDisk(const SparseOptions &);
    ~SparseDisk();
    SparseDisk(const SparseDisk &) = delete;
    SparseDisk &operator=(const SparseDisk &) = delete;
    guardian::scalable::Status Io(guardian::scalable::BlockOp, uint64_t, uint8_t *) noexcept;
    uint64_t Available() const noexcept;
    uint64_t Allocated() const;
    uint64_t SnapshotCharge() const { return (2ull + mapped_) * guardian::scalable::ChunkBytes; }
    std::vector<uint8_t> Snapshot();
    void Restore(const std::vector<uint8_t> &);
    const SparseOptions &Options() const { return options_; }
    static guardian::scalable::Status Block(void *p, guardian::scalable::BlockOp op, uint64_t at,
                                            uint8_t *data) noexcept {
        return static_cast<SparseDisk *>(p)->Io(op, at, data);
    }
    static uint64_t Space(void *p) noexcept { return static_cast<SparseDisk *>(p)->Available(); }

  private:
    struct Header {
        uint64_t magic = 0, capacity = 0, sequence = 0, checksum = 0;
        uint32_t version = 2, bytes = sizeof(Header);
        char owner[64]{};
        uint8_t map[16384]{};
        uint8_t reserved[49048]{};
    };
    static_assert(sizeof(Header) == guardian::scalable::ChunkBytes);
    static uint64_t Digest(const Header &);
    bool Valid(const Header &) const;
    void Flush();
    void Read(uint64_t, void *, uint32_t) const;
    void Write(uint64_t, const void *, uint32_t);
    void Discard(uint64_t, uint32_t);
    bool Mapped(uint32_t b) const { return (header_.map[b / 8] & (1u << (b % 8))) != 0; }
    SparseOptions options_;
    std::unique_ptr<state::Lease> lease_;
    Header header_{};
    uint32_t active_ = 0, mapped_ = 0;
    bool dirty_ = false, failed_ = false;
#ifdef _WIN32
    void *file_ = reinterpret_cast<void *>(intptr_t(-1));
#else
    int file_ = -1;
#endif
};
} // namespace av::microvm::storage
