#include "ScalableVolume.h"
namespace guardian::scalable {
namespace {
void Copy(void *d, const void *s, uint32_t n) noexcept {
    auto *to = static_cast<uint8_t *>(d);
    auto *from = static_cast<const uint8_t *>(s);
    for (uint32_t i = 0; i < n; ++i)
        to[i] = from[i];
}
void Zero(void *d, uint32_t n) noexcept {
    auto *p = static_cast<uint8_t *>(d);
    for (uint32_t i = 0; i < n; ++i)
        p[i] = 0;
}
bool Zeros(const void *d, uint32_t n) noexcept {
    auto *p = static_cast<const uint8_t *>(d);
    for (uint32_t i = 0; i < n; ++i)
        if (p[i])
            return false;
    return true;
}
bool Overlap(const void *a, uint64_t an, const void *b, uint64_t bn) noexcept {
    if (!an || !bn)
        return false;
    const auto x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    if (an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y)
        return true;
    return x < y + bn && y < x + an;
}
} // namespace
bool Volume::Configure(void *c, Block b, Available a, Metadata *one, Metadata *two, uint8_t *scratch,
                       uint64_t capacity, uint64_t quota) noexcept {
    if (mounted_ || !b || !a || !one || !two || one == two || !scratch || capacity < DataStart + ChunkBytes ||
        capacity > MaxCapacity || capacity % ChunkBytes || quota < DataStart || quota > capacity ||
        quota % ChunkBytes || Overlap(one, sizeof(Metadata), two, sizeof(Metadata)) ||
        Overlap(scratch, ChunkBytes, one, sizeof(Metadata)) ||
        Overlap(scratch, ChunkBytes, two, sizeof(Metadata)))
        return false;
    context_ = c;
    block_ = b;
    available_ = a;
    current_ = one;
    previous_ = two;
    scratch_ = scratch;
    capacity_ = capacity;
    quota_ = quota;
    return true;
}
bool Volume::Name(const char *s, char *out) noexcept {
    if (!s)
        return false;
    uint32_t n = 0;
    for (; n < 31 && s[n]; ++n) {
        const auto c = s[n];
        if (c < ' ' || c > '~' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return false;
        out[n] = c >= 'A' && c <= 'Z' ? char(c + 32) : c;
    }
    if (!n || s[n] || out[n - 1] == '.' || out[n - 1] == ' ' || (n == 1 && out[0] == '.') ||
        (n == 2 && out[0] == '.' && out[1] == '.'))
        return false;
    out[n] = 0;
    return true;
}
bool Volume::Equal(const char *a, const char *b) noexcept {
    for (uint32_t i = 0; i < 32; ++i) {
        if (a[i] != b[i])
            return false;
        if (!a[i])
            return true;
    }
    return false;
}
uint64_t Volume::Digest(const Metadata &m) noexcept {
    auto *p = reinterpret_cast<const uint8_t *>(&m);
    uint64_t h = 14695981039346656037ull;
    for (uint32_t i = 0; i < sizeof(m); ++i) {
        const auto b = i >= 32 && i < 40 ? uint8_t(0) : p[i];
        h = (h ^ b) * 1099511628211ull;
    }
    return h;
}
bool Volume::Validate(const Metadata &m) const noexcept {
    const auto &h = m.header;
    if (h.magic != Magic || h.capacity != capacity_ || h.quota < DataStart || h.quota > capacity_ ||
        h.quota % ChunkBytes || !h.sequence || h.version != Version || h.bytes != sizeof(Metadata) ||
        h.reserved[0] || h.reserved[1] || h.checksum != Digest(m))
        return false;
    uint64_t allocated = DataStart;
    Zero(bitmap_, sizeof(bitmap_));
    for (uint32_t i = 0; i < MaxFiles; ++i) {
        const auto &n = m.nodes[i];
        if (!n.used) {
            if (!Zeros(&n, sizeof(n)))
                return false;
            continue;
        }
        char normalized[32]{};
        if (n.used != 1 || n.reserved || !Name(n.name, normalized) || !Equal(n.name, normalized) ||
            n.bytes > MaxFileBytes)
            return false;
        for (uint32_t j = 0; j < i; ++j)
            if (m.nodes[j].used && Equal(m.nodes[j].name, n.name))
                return false;
        for (uint32_t j = 0; j < Extents; ++j) {
            const auto block = n.extents[j];
            if (!block)
                continue;
            if (uint64_t(block) * ChunkBytes < DataStart || uint64_t(block) * ChunkBytes >= capacity_ ||
                uint64_t(j) * ChunkBytes >= n.bytes)
                return false;
            const auto word = block / 32, bit = 1u << (block % 32);
            if (bitmap_[word] & bit)
                return false;
            bitmap_[word] |= bit;
            allocated += ChunkBytes;
        }
    }
    return allocated <= h.quota;
}
Status Volume::ReadSlot(uint32_t slot, Metadata &m, bool &zero) noexcept {
    zero = true;
    Zero(&m, sizeof(m));
    for (uint32_t i = 0; i < SlotChunks; ++i) {
        const auto status =
            block_(context_, BlockOp::Read, uint64_t(slot * SlotChunks + i) * ChunkBytes, scratch_);
        if (status != Status::Ok) {
            zero = false;
            return status;
        }
        if (!Zeros(scratch_, ChunkBytes))
            zero = false;
        const auto offset = i * ChunkBytes, n = uint32_t(sizeof(m)) - offset;
        Copy(reinterpret_cast<uint8_t *>(&m) + offset, scratch_, n < ChunkBytes ? n : ChunkBytes);
        if (n < ChunkBytes && !Zeros(scratch_ + n, ChunkBytes - n))
            return Status::Corrupt;
    }
    return Validate(m) ? Status::Ok : Status::Corrupt;
}
bool Volume::Intent(bool write, bool clear) noexcept {
    constexpr uint64_t intentMagic = 0x32544E49415647ull;
    constexpr uint64_t at = uint64_t(24) * ChunkBytes;
    const auto hash = [](const Header &h) noexcept {
        const auto *p = reinterpret_cast<const uint8_t *>(&h);
        uint64_t value = 14695981039346656037ull;
        for (uint32_t i = 0; i < sizeof(h); ++i)
            value = (value ^ (i >= 32 && i < 40 ? uint8_t(0) : p[i])) * 1099511628211ull;
        return value;
    };
    if (write) {
        Zero(scratch_, ChunkBytes);
        if (!clear) {
            Header h{intentMagic, capacity_, quota_, 1, 0, Version, sizeof(Header), {}};
            h.checksum = hash(h);
            Copy(scratch_, &h, sizeof(h));
        }
        return block_(context_, BlockOp::Write, at, scratch_) == Status::Ok &&
               block_(context_, BlockOp::Flush, 0, nullptr) == Status::Ok;
    }
    if (block_(context_, BlockOp::Read, at, scratch_) != Status::Ok)
        return false;
    Header h{};
    Copy(&h, scratch_, sizeof(h));
    return h.magic == intentMagic && h.capacity == capacity_ && h.quota == quota_ && h.sequence == 1 &&
           h.version == Version && h.bytes == sizeof(h) && !h.reserved[0] && !h.reserved[1] &&
           h.checksum == hash(h) && Zeros(scratch_ + sizeof(h), ChunkBytes - sizeof(h));
}
Status Volume::Mount(bool format, bool recover) noexcept {
    if (!block_ || mounted_)
        return Status::Invalid;
    bool za = false, zb = false;
    const auto sa = ReadSlot(0, *current_, za), sb = ReadSlot(1, *previous_, zb);
    const bool a = sa == Status::Ok, b = sb == Status::Ok;
    if ((sa != Status::Ok && sa != Status::Corrupt) || (sb != Status::Ok && sb != Status::Corrupt))
        return Status::IoError;
    if (!a && !b) {
        if (!format || (!(za && zb) && !Intent(false)))
            return Status::Corrupt;
        if (!Intent(true))
            return Status::IoError;
        Zero(current_, sizeof(Metadata));
        Zero(previous_, sizeof(Metadata));
        current_->header = {Magic, capacity_, quota_, 0, 0, Version, sizeof(Metadata), {}};
        active_ = 0;
        mounted_ = true;
        Begin();
        auto status = Commit();
        if (status == Status::Ok) {
            Begin();
            status = Commit();
        }
        if (status != Status::Ok || !Intent(true, true)) {
            mounted_ = false;
            return status == Status::Ok ? Status::IoError : status;
        }
        return Status::Ok;
    }
    if (b && (!a || previous_->header.sequence > current_->header.sequence)) {
        auto *p = current_;
        current_ = previous_;
        previous_ = p;
        active_ = 1;
    } else
        active_ = 0;
    if (!(active_ ? a : b))
        Copy(previous_, current_, sizeof(Metadata));
    mounted_ = true;
    if (QuerySpace().allocated > quota_) {
        mounted_ = false;
        return Status::NoSpace;
    }
    if (recover && Intent(false) && !Intent(true, true)) {
        mounted_ = false;
        return Status::IoError;
    }
    const auto reclaimed = recover ? Reclaim() : Status::Ok;
    if (reclaimed != Status::Ok)
        mounted_ = false;
    return reclaimed;
}
Node *Volume::Find(const char *name) const noexcept {
    for (auto &n : current_->nodes)
        if (n.used && Equal(n.name, name))
            return &n;
    return nullptr;
}
Status Volume::Reclaim() noexcept {
    const auto status = block_(context_, BlockOp::AllocationMap, 0, scratch_);
    if (status != Status::Ok)
        return status;
    Zero(bitmap_, sizeof(bitmap_));
    Mark(*current_);
    Mark(*previous_);
    for (uint32_t b = uint32_t(DataStart / ChunkBytes); uint64_t(b) * ChunkBytes < capacity_; ++b) {
        if ((scratch_[b / 8] & (1u << (b % 8))) && !(bitmap_[b / 32] & (1u << (b % 32)))) {
            const auto discarded = block_(context_, BlockOp::Discard, uint64_t(b) * ChunkBytes, nullptr);
            if (discarded != Status::Ok)
                return discarded;
        }
    }
    return Status::Ok;
}
bool Volume::Referenced(uint32_t block, const Metadata &m) const noexcept {
    for (const auto &n : m.nodes)
        if (n.used)
            for (auto e : n.extents)
                if (e == block)
                    return true;
    return false;
}
void Volume::Mark(const Metadata &m) const noexcept {
    for (const auto &n : m.nodes)
        if (n.used)
            for (auto b : n.extents)
                if (b)
                    bitmap_[b / 32] |= 1u << (b % 32);
}
uint32_t Volume::Allocate() const noexcept {
    Zero(bitmap_, sizeof(bitmap_));
    Mark(*current_);
    Mark(*previous_);
    for (uint32_t b = uint32_t(DataStart / ChunkBytes); uint64_t(b) * ChunkBytes < capacity_; ++b)
        if (!(bitmap_[b / 32] & (1u << (b % 32))))
            return b;
    return 0;
}
Space Volume::QuerySpace() const noexcept {
    Space s{capacity_, DataStart, capacity_ > DataStart ? capacity_ - DataStart : 0,
            quota_,    0,         available_ ? available_(context_) : 0};
    if (!mounted_)
        return s;
    for (const auto &n : current_->nodes)
        if (n.used)
            for (auto e : n.extents)
                if (e)
                    s.allocated += ChunkBytes;
    s.free = s.allocated < capacity_ ? capacity_ - s.allocated : 0;
    const auto q = s.allocated < quota_ ? quota_ - s.allocated : 0;
    s.available = s.free < q ? s.free : q;
    if (s.available > s.physicalAvailable)
        s.available = s.physicalAvailable;
    s.available = s.available / ChunkBytes * ChunkBytes;
    return s;
}
void Volume::Begin(Node *n) noexcept {
    undoHeader_ = current_->header;
    undoIndex_ = n ? uint32_t(n - current_->nodes) : UINT32_MAX;
    if (n)
        undo_ = *n;
    transaction_ = true;
}
void Volume::Rollback() noexcept {
    current_->header = undoHeader_;
    if (undoIndex_ < MaxFiles)
        current_->nodes[undoIndex_] = undo_;
    transaction_ = false;
}
Status Volume::Commit() noexcept {
    if (!transaction_)
        return Status::Invalid;
    const auto oldSequence = undoHeader_.sequence;
    if (oldSequence == UINT64_MAX) {
        Rollback();
        return Status::Invalid;
    }
    // Preserve every block referenced by either the new or last committed root.
    Zero(bitmap_, sizeof(bitmap_));
    Mark(*current_);
    if (undoIndex_ < MaxFiles && undo_.used)
        for (auto b : undo_.extents)
            if (b)
                bitmap_[b / 32] |= 1u << (b % 32);
    retiredCount_ = 0;
    for (const auto &n : previous_->nodes)
        if (n.used)
            for (auto b : n.extents)
                if (b && !(bitmap_[b / 32] & (1u << (b % 32)))) {
                    if (retiredCount_ == Extents) {
                        Rollback();
                        return Status::Corrupt;
                    }
                    retired_[retiredCount_++] = b;
                }
    current_->header.magic = Magic;
    current_->header.capacity = capacity_;
    current_->header.quota = quota_;
    current_->header.version = Version;
    current_->header.bytes = sizeof(Metadata);
    current_->header.sequence = oldSequence + 1;
    current_->header.checksum = Digest(*current_);
    const auto target = 1 - active_;
    for (uint32_t i = 0; i < SlotChunks; ++i) {
        Zero(scratch_, ChunkBytes);
        const auto offset = i * ChunkBytes, n = uint32_t(sizeof(Metadata)) - offset;
        Copy(scratch_, reinterpret_cast<const uint8_t *>(current_) + offset, n < ChunkBytes ? n : ChunkBytes);
        const auto status =
            block_(context_, BlockOp::Write, uint64_t(target * SlotChunks + i) * ChunkBytes, scratch_);
        if (status != Status::Ok) {
            Rollback();
            return status;
        }
    }
    const auto status = block_(context_, BlockOp::Flush, 0, nullptr);
    if (status != Status::Ok) {
        Rollback();
        return status;
    }
    Copy(previous_, current_, sizeof(Metadata));
    previous_->header = undoHeader_;
    if (undoIndex_ < MaxFiles)
        previous_->nodes[undoIndex_] = undo_;
    active_ = target;
    transaction_ = false;
    for (uint32_t i = 0; i < retiredCount_; ++i)
        block_(context_, BlockOp::Discard, uint64_t(retired_[i]) * ChunkBytes, nullptr);
    return Status::Ok;
}
Status Volume::Create(const char *name) noexcept {
    if (!mounted_)
        return Status::Invalid;
    char normalized[32]{};
    if (!Name(name, normalized))
        return Status::Invalid;
    if (Find(normalized))
        return Status::Exists;
    for (auto &n : current_->nodes)
        if (!n.used) {
            Begin(&n);
            n = {};
            n.used = 1;
            Copy(n.name, normalized, 32);
            const auto s = Commit();
            return s;
        }
    return Status::NoSpace;
}
Status Volume::Size(const char *name, uint64_t &size) const noexcept {
    size = 0;
    if (!mounted_)
        return Status::Invalid;
    char normalized[32]{};
    if (!Name(name, normalized))
        return Status::Invalid;
    const auto *n = Find(normalized);
    if (!n)
        return Status::NotFound;
    size = n->bytes;
    return Status::Ok;
}
Status Volume::Read(const char *name, uint64_t offset, uint8_t *data, uint32_t bytes,
                    uint32_t &transferred) noexcept {
    transferred = 0;
    if (!mounted_ || !data || bytes > ChunkBytes || offset > MaxFileBytes ||
        Overlap(data, bytes, scratch_, ChunkBytes))
        return Status::Invalid;
    char normalized[32]{};
    if (!Name(name, normalized))
        return Status::Invalid;
    const auto *n = Find(normalized);
    if (!n)
        return Status::NotFound;
    while (transferred < bytes && offset < n->bytes) {
        const auto index = uint32_t(offset / ChunkBytes), within = uint32_t(offset % ChunkBytes);
        auto count = ChunkBytes - within;
        if (count > bytes - transferred)
            count = bytes - transferred;
        if (count > n->bytes - offset)
            count = uint32_t(n->bytes - offset);
        if (n->extents[index]) {
            const auto s =
                block_(context_, BlockOp::Read, uint64_t(n->extents[index]) * ChunkBytes, scratch_);
            if (s != Status::Ok)
                return s;
            Copy(data + transferred, scratch_ + within, count);
        } else
            Zero(data + transferred, count);
        transferred += count;
        offset += count;
    }
    return Status::Ok;
}
Status Volume::Write(const char *name, uint64_t offset, const uint8_t *data, uint32_t bytes,
                     uint32_t &transferred) noexcept {
    transferred = 0;
    if (!mounted_ || !data || bytes > ChunkBytes || Overlap(data, bytes, scratch_, ChunkBytes))
        return Status::Invalid;
    if (offset > MaxFileBytes || bytes > MaxFileBytes - offset)
        return Status::FileTooLarge;
    char normalized[32]{};
    if (!Name(name, normalized))
        return Status::Invalid;
    auto *n = Find(normalized);
    if (!n)
        return Status::NotFound;
    while (transferred < bytes) {
        const auto index = uint32_t(offset / ChunkBytes), within = uint32_t(offset % ChunkBytes);
        auto count = ChunkBytes - within;
        if (count > bytes - transferred)
            count = bytes - transferred;
        const auto before = n->extents[index];
        if (!before && QuerySpace().available < ChunkBytes)
            return Status::NoSpace;
        const auto fresh = Allocate();
        if (!fresh)
            return Status::NoSpace;
        if (before) {
            const auto s = block_(context_, BlockOp::Read, uint64_t(before) * ChunkBytes, scratch_);
            if (s != Status::Ok)
                return s;
        } else
            Zero(scratch_, ChunkBytes);
        Copy(scratch_ + within, data + transferred, count);
        auto s = block_(context_, BlockOp::Write, uint64_t(fresh) * ChunkBytes, scratch_);
        if (s != Status::Ok)
            return s;
        s = block_(context_, BlockOp::Flush, 0, nullptr);
        if (s != Status::Ok)
            return s;
        Begin(n);
        n->extents[index] = fresh;
        if (n->bytes < offset + count)
            n->bytes = offset + count;
        s = Commit();
        if (s != Status::Ok)
            return s;
        transferred += count;
        offset += count;
    }
    return Status::Ok;
}
Status Volume::Truncate(const char *name, uint64_t bytes) noexcept {
    if (!mounted_ || bytes > MaxFileBytes)
        return Status::Invalid;
    char normalized[32]{};
    if (!Name(name, normalized))
        return Status::Invalid;
    auto *n = Find(normalized);
    if (!n)
        return Status::NotFound;
    Begin(n);
    // Shrinks clear complete extents. A partial tail is COW-zeroed before publication.
    if (bytes < n->bytes && bytes % ChunkBytes && n->extents[bytes / ChunkBytes]) {
        const auto at = uint32_t(bytes / ChunkBytes);
        const auto fresh = Allocate();
        if (!fresh) {
            Rollback();
            return Status::NoSpace;
        }
        auto s = block_(context_, BlockOp::Read, uint64_t(n->extents[at]) * ChunkBytes, scratch_);
        if (s != Status::Ok) {
            Rollback();
            return s;
        }
        Zero(scratch_ + bytes % ChunkBytes, ChunkBytes - uint32_t(bytes % ChunkBytes));
        s = block_(context_, BlockOp::Write, uint64_t(fresh) * ChunkBytes, scratch_);
        if (s != Status::Ok) {
            Rollback();
            return s;
        }
        if (block_(context_, BlockOp::Flush, 0, nullptr) != Status::Ok) {
            Rollback();
            return Status::IoError;
        }
        n->extents[at] = fresh;
    }
    for (uint32_t i = uint32_t((bytes + ChunkBytes - 1) / ChunkBytes); i < Extents; ++i)
        n->extents[i] = 0;
    n->bytes = bytes;
    auto s = Commit();
    if (s == Status::Ok) {
        Begin();
        s = Commit();
    }
    return s;
}
Status Volume::Remove(const char *name) noexcept {
    if (!mounted_)
        return Status::Invalid;
    char normalized[32]{};
    if (!Name(name, normalized))
        return Status::Invalid;
    auto *n = Find(normalized);
    if (!n)
        return Status::NotFound;
    Begin(n);
    *n = {};
    auto s = Commit();
    if (s == Status::Ok) {
        Begin();
        s = Commit();
    }
    return s;
}
} // namespace guardian::scalable
