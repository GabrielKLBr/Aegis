#include "SparseDisk.h"
#include "../state/StorageBudget.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#ifdef _WIN32
#include <Windows.h>
#include <winioctl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace av::microvm::storage {
using namespace guardian::scalable;
namespace {
constexpr uint64_t Magic = 0x324B5344564147ull, Prefix = 2ull * ChunkBytes;
void Directory(const std::filesystem::path &p) {
    const auto type = std::filesystem::symlink_status(p);
    state::Require(std::filesystem::is_directory(type) && !std::filesystem::is_symlink(type),
                   "Disk directory type rejected");
#ifdef _WIN32
    const auto a = GetFileAttributesW(p.c_str());
    state::Require(a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_REPARSE_POINT),
                   "Disk directory reparse point rejected");
#endif
}
} // namespace
uint64_t SparseDisk::Digest(const Header &h) {
    const auto *p = reinterpret_cast<const uint8_t *>(&h);
    uint64_t n = 14695981039346656037ull;
    for (uint32_t i = 0; i < sizeof(h); ++i)
        n = (n ^ (i >= 24 && i < 32 ? uint8_t(0) : p[i])) * 1099511628211ull;
    return n;
}
bool SparseDisk::Valid(const Header &h) const {
    if (h.magic != Magic || h.capacity != options_.capacity || !h.sequence || h.version != 2 ||
        h.bytes != sizeof(h) || h.owner[63] || std::string(h.owner) != options_.owner ||
        h.checksum != Digest(h))
        return false;
    for (auto b : h.reserved)
        if (b)
            return false;
    for (uint64_t b = options_.capacity / ChunkBytes; b < uint64_t(sizeof(h.map)) * 8; ++b)
        if (h.map[b / 8] & (1u << (b % 8)))
            return false;
    return true;
}
SparseDisk::SparseDisk(const SparseOptions &o) : options_(o) {
    state::Require(
        !o.directory.empty() && !o.owner.empty() && o.owner.size() < 64 &&
            o.owner.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") ==
                std::string::npos,
        "Disk ownership rejected");
    state::Require(o.capacity >= DataStart + ChunkBytes && o.capacity <= MaxCapacity &&
                       !(o.capacity % ChunkBytes) && o.growthLimit >= (4ull << 20) &&
                       o.growthLimit <= (256ull << 20) && !(o.growthLimit % ChunkBytes) &&
                       o.hostReserve >= (256ull << 20),
                   "Sparse disk bounds rejected");
    Directory(o.directory);
    const auto unit = state::AllocationUnit(o.directory);
    state::Require(unit <= ChunkBytes && ChunkBytes % unit == 0,
                   "Sparse disk requires host allocation units dividing 64 KiB");
    const auto marker = o.directory / ".guardian-disk-owner";
    if (std::filesystem::exists(marker)) {
        const auto data = state::ReadFile(marker, 128);
        state::Require(std::string(data.begin(), data.end()) == o.owner, "Disk belongs to another VM");
    } else {
        state::Require(o.create && std::filesystem::is_empty(o.directory),
                       "Disk directory is not empty and owned");
        state::AtomicText(marker, o.owner);
    }
    lease_ = std::make_unique<state::Lease>(o.directory / "disk.lock", true);
    state::Require(lease_->Held(), "Disk is already open by another worker");
    const auto path = o.directory / "payload.gvd2";
    const bool exists = std::filesystem::exists(path);
    state::Require(exists || o.create, "Disk payload missing");
    if (exists)
        state::Require(state::Ordinary(path) && std::filesystem::hard_link_count(path) == 1,
                       "Disk payload must be a private ordinary file");
    try {
#ifdef _WIN32
        file_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            exists ? OPEN_EXISTING : CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        state::Require(file_ != INVALID_HANDLE_VALUE, "Cannot open sparse disk");
        BY_HANDLE_FILE_INFORMATION identity{};
        state::Require(GetFileInformationByHandle(file_, &identity) && identity.nNumberOfLinks == 1 &&
                           !(identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                           GetFileType(file_) == FILE_TYPE_DISK,
                       "Sparse disk handle rejected");
        DWORD actual = 0;
        const auto attributes = GetFileAttributesW(path.c_str());
        state::Require(attributes != INVALID_FILE_ATTRIBUTES, "Cannot inspect sparse disk attributes");
        if (attributes & FILE_ATTRIBUTE_COMPRESSED) {
            state::Require(!exists, "Existing compressed disk backing rejected");
            USHORT compression = COMPRESSION_FORMAT_NONE;
            state::Require(DeviceIoControl(file_, FSCTL_SET_COMPRESSION, &compression, sizeof(compression),
                                           nullptr, 0, &actual, nullptr) != 0,
                           "Cannot disable inherited disk compression");
        }
        state::Require(DeviceIoControl(file_, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &actual, nullptr) !=
                           0,
                       "Host filesystem does not support sparse disks");
        if (!exists) {
            LARGE_INTEGER size{};
            size.QuadPart = LONGLONG(Prefix + o.capacity);
            state::Require(SetFilePointerEx(file_, size, nullptr, FILE_BEGIN) && SetEndOfFile(file_),
                           "Cannot size sparse disk");
        }
        LARGE_INTEGER size{};
        state::Require(GetFileSizeEx(file_, &size) && uint64_t(size.QuadPart) == Prefix + o.capacity,
                       "Sparse disk length rejected");
#else
        file_ = ::open(path.c_str(),
                       O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (exists ? 0 : O_CREAT | O_EXCL), 0600);
        state::Require(file_ >= 0, "Cannot open sparse disk");
        struct stat info{};
        state::Require(::fstat(file_, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1,
                       "Sparse disk file rejected");
        if (!exists)
            state::Require(::ftruncate(file_, off_t(Prefix + o.capacity)) == 0, "Cannot size sparse disk");
        else
            state::Require(info.st_size >= 0 && uint64_t(info.st_size) == Prefix + o.capacity,
                           "Sparse disk length rejected");
#endif
        if (!exists) {
            state::Require(Available() >= Prefix,
                           "Insufficient quota or host reserve for sparse disk headers");
            header_.magic = Magic;
            header_.capacity = o.capacity;
            std::memcpy(header_.owner, o.owner.data(), o.owner.size());
            dirty_ = true;
            Flush();
            dirty_ = true;
            Flush();
        } else {
            Header a{}, b{};
            Read(0, &a, sizeof(a));
            Read(ChunkBytes, &b, sizeof(b));
            const bool goodA = Valid(a), goodB = Valid(b);
            state::Require(goodA || goodB, "Both sparse disk headers are corrupt");
            if (goodB && (!goodA || b.sequence > a.sequence)) {
                header_ = b;
                active_ = 1;
            } else {
                header_ = a;
                active_ = 0;
            }
        }
        for (auto byte : header_.map)
            for (uint32_t bit = 0; bit < 8; ++bit)
                if (byte & (1u << bit))
                    ++mapped_;
        state::Require(SnapshotCharge() <= o.growthLimit, "Disk mapped data exceeds snapshot growth bound");
        state::Require(Allocated() <= o.growthLimit, "Disk growth limit is below current allocation");
    } catch (...) {
#ifdef _WIN32
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
#else
        ::close(file_);
        file_ = -1;
#endif
        throw;
    }
}
SparseDisk::~SparseDisk() {
#ifdef _WIN32
    if (file_ != INVALID_HANDLE_VALUE)
        CloseHandle(file_);
#else
    if (file_ >= 0)
        ::close(file_);
#endif
}
void SparseDisk::Read(uint64_t at, void *data, uint32_t bytes) const {
    auto *p = static_cast<uint8_t *>(data);
    uint32_t done = 0;
    while (done < bytes) {
#ifdef _WIN32
        LARGE_INTEGER pos{};
        pos.QuadPart = LONGLONG(at + done);
        DWORD n = 0;
        state::Require(SetFilePointerEx(file_, pos, nullptr, FILE_BEGIN) &&
                           ReadFile(file_, p + done, bytes - done, &n, nullptr) && n,
                       "Sparse disk read failed");
#else
        const auto got = ::pread(file_, p + done, bytes - done, off_t(at + done));
        if (got < 0 && errno == EINTR)
            continue;
        state::Require(got > 0, "Sparse disk read failed");
        const auto n = uint32_t(got);
#endif
        done += n;
    }
}
void SparseDisk::Write(uint64_t at, const void *data, uint32_t bytes) {
    const auto *p = static_cast<const uint8_t *>(data);
    uint32_t done = 0;
    while (done < bytes) {
#ifdef _WIN32
        LARGE_INTEGER pos{};
        pos.QuadPart = LONGLONG(at + done);
        DWORD n = 0;
        state::Require(SetFilePointerEx(file_, pos, nullptr, FILE_BEGIN) &&
                           WriteFile(file_, p + done, bytes - done, &n, nullptr) && n,
                       "Sparse disk write failed");
#else
        const auto got = ::pwrite(file_, p + done, bytes - done, off_t(at + done));
        if (got < 0 && errno == EINTR)
            continue;
        state::Require(got > 0, "Sparse disk write failed");
        const auto n = uint32_t(got);
#endif
        done += n;
    }
}
void SparseDisk::Flush() {
    if (dirty_) {
        state::Require(header_.sequence < UINT64_MAX, "Sparse disk sequence exhausted");
        ++header_.sequence;
        header_.checksum = Digest(header_);
        const auto target = 1 - active_;
        Write(uint64_t(target) * ChunkBytes, &header_, sizeof(header_));
        active_ = target;
    }
#ifdef _WIN32
    state::Require(FlushFileBuffers(file_) != 0, "Sparse disk flush failed");
#else
    state::Require(::fsync(file_) == 0, "Sparse disk flush failed");
#endif
    dirty_ = false;
}
uint64_t SparseDisk::Allocated() const { return state::AllocatedBytes(options_.directory / "payload.gvd2"); }
uint64_t SparseDisk::Available() const noexcept {
    try {
        if (failed_)
            return 0;
        const auto used = std::max(Allocated(), SnapshotCharge());
        auto free = used < options_.growthLimit ? options_.growthLimit - used : 0;
        const auto host = std::filesystem::space(options_.directory).available;
        free = std::min(free, host > options_.hostReserve ? host - options_.hostReserve : 0);
        if (options_.aggregateAvailable)
            free = std::min(free, options_.aggregateAvailable());
        return free / ChunkBytes * ChunkBytes;
    } catch (...) {
        return 0;
    }
}
void SparseDisk::Discard(uint64_t at, uint32_t bytes) {
#ifdef _WIN32
    FILE_ZERO_DATA_INFORMATION zero{};
    zero.FileOffset.QuadPart = LONGLONG(at);
    zero.BeyondFinalZero.QuadPart = LONGLONG(at + bytes);
    DWORD actual = 0;
    state::Require(
        DeviceIoControl(file_, FSCTL_SET_ZERO_DATA, &zero, sizeof(zero), nullptr, 0, &actual, nullptr) != 0,
        "Sparse disk deallocation failed");
#else
    state::Require(::fallocate(file_, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, off_t(at), bytes) == 0,
                   "Sparse disk deallocation failed");
#endif
}
Status SparseDisk::Io(BlockOp op, uint64_t at, uint8_t *data) noexcept {
    if (failed_)
        return Status::IoError;
    try {
        if (op == BlockOp::AllocationMap) {
            if (at || !data)
                return Status::Invalid;
            std::memset(data, 0, ChunkBytes);
            std::memcpy(data, header_.map, sizeof(header_.map));
            return Status::Ok;
        }
        if (op == BlockOp::Flush) {
            Flush();
            return Status::Ok;
        }
        if (at % ChunkBytes || at > options_.capacity || ChunkBytes > options_.capacity - at)
            return Status::Invalid;
        const auto b = uint32_t(at / ChunkBytes);
        if (op == BlockOp::Read) {
            if (!data)
                return Status::Invalid;
            if (Mapped(b))
                Read(Prefix + at, data, ChunkBytes);
            else
                std::memset(data, 0, ChunkBytes);
            return Status::Ok;
        }
        if (op == BlockOp::Discard) {
            if (Mapped(b)) {
                header_.map[b / 8] &= uint8_t(~(1u << (b % 8)));
                --mapped_;
                dirty_ = true;
                Flush();
                Discard(Prefix + at, ChunkBytes);
            }
            return Status::Ok;
        }
        if (op != BlockOp::Write || !data)
            return Status::Invalid;
        if (!Mapped(b) && Available() < ChunkBytes)
            return Status::NoSpace;
        Write(Prefix + at, data, ChunkBytes);
        if (!Mapped(b))
            ++mapped_;
        header_.map[b / 8] |= uint8_t(1u << (b % 8));
        dirty_ = true;
        return Status::Ok;
    } catch (...) {
        failed_ = true;
        return Status::IoError;
    }
}
std::vector<uint8_t> SparseDisk::Snapshot() {
    Flush();
    state::Writer w;
    w.Pod(uint64_t(0x32504E5344564147ull));
    w.Pod(options_.capacity);
    uint32_t count = 0;
    for (uint32_t b = 0; uint64_t(b) * ChunkBytes < options_.capacity; ++b)
        if (Mapped(b))
            ++count;
    w.Pod(count);
    std::array<uint8_t, ChunkBytes> block{};
    for (uint32_t b = 0; uint64_t(b) * ChunkBytes < options_.capacity; ++b)
        if (Mapped(b)) {
            w.Pod(b);
            Read(Prefix + uint64_t(b) * ChunkBytes, block.data(), ChunkBytes);
            w.Raw(block.data(), block.size());
        }
    return state::Seal(w.data);
}
void SparseDisk::Restore(const std::vector<uint8_t> &snapshot) {
    state::Require(snapshot.size() <= options_.growthLimit,
                   "Sparse snapshot exceeds configured growth bound");
    const auto data = state::Open(snapshot);
    state::Reader r(data);
    state::Require(r.Pod<uint64_t>() == 0x32504E5344564147ull && r.Pod<uint64_t>() == options_.capacity,
                   "Sparse disk snapshot identity rejected");
    const auto count = r.Pod<uint32_t>();
    state::Require(count <= options_.capacity / ChunkBytes &&
                       uint64_t(count) * ChunkBytes + Prefix <= options_.growthLimit,
                   "Sparse disk snapshot exceeds growth limit");
    std::set<uint32_t> seen;
    struct Entry {
        uint32_t index;
        std::array<uint8_t, ChunkBytes> data;
    };
    std::vector<Entry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto b = r.Pod<uint32_t>();
        state::Require(uint64_t(b) * ChunkBytes < options_.capacity && seen.insert(b).second,
                       "Sparse disk snapshot extent rejected");
        entries.push_back({b, {}});
        r.Raw(entries.back().data.data(), ChunkBytes);
    }
    r.End();
    for (uint32_t b = 0; uint64_t(b) * ChunkBytes < options_.capacity; ++b)
        state::Require(!Mapped(b), "Restore requires an empty candidate disk");
    for (const auto &e : entries)
        state::Require(Io(BlockOp::Write, uint64_t(e.index) * ChunkBytes,
                          const_cast<uint8_t *>(e.data.data())) == Status::Ok,
                       "Cannot restore candidate disk");
    Flush();
}
} // namespace av::microvm::storage
