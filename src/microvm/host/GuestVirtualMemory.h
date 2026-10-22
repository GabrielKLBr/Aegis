// Private virtual memory for the existing identity-mapped MiniNt arena.
// State is separate from guest RAM. A failed hardware mapping permanently stops
// the VM instead of reporting success with uncertain page permissions.
#pragma once
#include "GuestMemory.h"
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace av { namespace microvm {
struct VirtualMemoryBackend {
    virtual ~VirtualMemoryBackend() = default;
    virtual GuestMemory Memory() const = 0;
    // Access bits: read=1, write=2, execute=4; zero removes the mapping.
    virtual bool SetAccess(uint64_t address, uint64_t bytes, uint8_t access) = 0;
    virtual void FailClosed() = 0;
};

class GuestVirtualMemory {
public:
    static constexpr uint64_t PageSize = 4096, Granularity = 65536;
    static constexpr uint32_t Success = 0, Unsuccessful = 0xC0000001,
        AccessViolation = 0xC0000005, InvalidHandle = 0xC0000008,
        InvalidParameter = 0xC000000D, NoMemory = 0xC0000017,
        Conflict = 0xC0000018, NotCommitted = 0xC000002D,
        InvalidProtection = 0xC0000045, SectionProtection = 0xC000004E, NotAllocated = 0xC00000A0,
        NotSupported = 0xC00000BB;
    static constexpr uint32_t Commit = 0x1000, Reserve = 0x2000,
        Decommit = 0x4000, Release = 0x8000, TopDown = 0x100000;
    static constexpr uint32_t Private = 0x20000, Mapped = 0x40000, Image = 0x1000000;
    struct BasicInformation {
        uint64_t baseAddress, allocationBase;
        uint32_t allocationProtect, partitionAndPadding;
        uint64_t regionSize;
        uint32_t state, protect, type, padding;
    };
    static_assert(sizeof(BasicInformation) == 48, "x64 MEMORY_BASIC_INFORMATION layout");

    GuestVirtualMemory() { Configure(0xA00000, 0xE00000); }
    void Configure(uint64_t base, uint64_t end) {
        if (bound_) throw std::logic_error("Cannot reconfigure an active virtual memory arena");
        if (!base || (base % Granularity) || (end % PageSize) || end <= base || end - base > 512ull * 1024 * 1024)
            throw std::invalid_argument("Virtual memory arena must be aligned, nonempty and at most 512 MiB");
        std::vector<Page> fresh(static_cast<size_t>((end - base) / PageSize));
        pages_ = std::move(fresh); regions_.clear(); base_ = base; end_ = end;
    }
    uint64_t Base() const { return base_; }
    uint64_t End() const { return end_; }
    bool Bound() const { return bound_ != nullptr; }
    bool Faulted() const { return faulted_; }
    bool ReservationAt(uint64_t address, uint64_t& base, uint64_t& bytes, uint32_t& type) const {
        if (!Inside(address, 1)) return false;
        auto it = regions_.upper_bound(address);
        if (it == regions_.begin()) return false;
        --it; if (address - it->first >= it->second.size) return false;
        base = it->first; bytes = it->second.size; type = it->second.type; return true;
    }
    static bool ValidProtection(uint32_t protection) {
        return protection == 1 || protection == 2 || protection == 4 || protection == 0x20 || protection == 0x40;
    }
    static uint8_t Access(uint32_t protection) {
        switch (protection) {
            case 2: return 1; case 4: return 3; case 0x20: return 5; case 0x40: return 7;
            default: return 0;
        }
    }
    // Outside this arena, the caller must still validate actual guest RAM bounds.
    bool Allows(uint64_t address, uint64_t bytes, uint8_t access) const {
        if (faulted_ || bytes > (std::numeric_limits<uint64_t>::max)() - address) return false;
        const auto end = address + bytes;
        if (!bytes || end <= base_ || address >= end_) return true;
        const auto low = address < base_ ? base_ : address;
        const auto high = end > end_ ? end_ : end;
        for (size_t i = Index(low); i <= Index(high - 1); ++i)
            if (!pages_[i].allocation || (Access(pages_[i].protect) & access) != access) return false;
        return true;
    }
    uint32_t Allocate(VirtualMemoryBackend& backend, uint64_t& address, uint64_t& bytes,
                      uint32_t flags, uint32_t protect, uint64_t zeroBits = 0, uint32_t type = Private,
                      uint32_t maximumProtect = 0) {
        if (!ValidProtection(protect)) return InvalidProtection;
        if (zeroBits || (flags & ~(Commit | Reserve | TopDown))) return NotSupported;
        if (!(flags & (Commit | Reserve)) || !bytes) return InvalidParameter;
        if (type != Private && type != Mapped && type != Image) return InvalidParameter;
        if (type == Mapped) {
            if (!maximumProtect) maximumProtect = protect;
            if (!ValidProtection(maximumProtect)) return InvalidProtection;
            if (Access(protect) & ~Access(maximumProtect)) return SectionProtection;
        }
        const bool fresh = (flags & Reserve) || !address;
        uint64_t low = address, size = bytes;
        if (address && !Rounded(address, bytes, fresh ? Granularity : PageSize, low, size)) return InvalidParameter;
        if (!address && !RoundUp(bytes, PageSize, size)) return InvalidParameter;
        if (size > end_ - base_) return NoMemory;
        if (address && !Inside(low, size)) return Conflict;
        const auto status = Bind(backend); if (status) return status;
        if (fresh) {
            if (!address) {
                if (!FindFree(size, (flags & TopDown) != 0, low)) return NoMemory;
            } else if (!FreeRange(low, size)) return Conflict;
            if (regions_.size() >= 4096) return NoMemory;
            try { regions_.emplace(low, Region{size, protect, type, maximumProtect}); }
            catch (const std::bad_alloc&) { return NoMemory; }
            if ((flags & Commit) && !Change(backend, low, size, protect, false, true)) {
                regions_.erase(low); return Unsuccessful;
            }
            for (size_t i = Index(low), end = i + static_cast<size_t>(size / PageSize); i < end; ++i)
                pages_[i] = Page{low, (flags & Commit) ? protect : 0};
        } else {
            auto region = RegionFor(low, size);
            if (region == regions_.end() || region->second.type != Private) return NotAllocated;
            if (!Change(backend, low, size, protect, true, true)) return Unsuccessful;
            for (size_t i = Index(low), end = i + static_cast<size_t>(size / PageSize); i < end; ++i)
                if (!pages_[i].protect) pages_[i].protect = protect;
        }
        address = low; bytes = size; return Success;
    }
    uint32_t Free(VirtualMemoryBackend& backend, uint64_t& address, uint64_t& bytes, uint32_t flags) {
        if (flags != Decommit && flags != Release) return InvalidParameter;
        if (!Inside(address, 1)) return NotAllocated;
        auto region = RegionFor(address, 1);
        if (region == regions_.end() || region->second.type != Private) return NotAllocated;
        uint64_t low = address, size = bytes;
        if (flags == Release) {
            if (bytes || address != region->first) return InvalidParameter;
            size = region->second.size;
        } else if (!bytes) {
            if (address != region->first) return InvalidParameter;
            size = region->second.size;
        } else if (!Rounded(address, bytes, PageSize, low, size) || RegionFor(low, size) != region) return NotAllocated;
        const auto status = Bind(backend); if (status) return status;
        if (!Change(backend, low, size, 0, false, false)) return Unsuccessful;
        for (size_t i = Index(low), end = i + static_cast<size_t>(size / PageSize); i < end; ++i) {
            if (flags == Release) pages_[i] = {};
            else pages_[i].protect = 0;
        }
        if (flags == Release) regions_.erase(region);
        address = low; bytes = size; return Success;
    }
    uint32_t Protect(VirtualMemoryBackend& backend, uint64_t& address, uint64_t& bytes,
                     uint32_t protect, uint32_t& oldProtect) {
        if (!ValidProtection(protect)) return InvalidProtection;
        uint64_t low = 0, size = 0;
        if (!bytes || !Rounded(address, bytes, PageSize, low, size) || !Inside(low, size)) return InvalidParameter;
        const auto region = RegionFor(low, size);
        if (region == regions_.end()) return NotAllocated;
        if (region->second.type == Mapped && (Access(protect) & ~Access(region->second.maximumProtect))) return SectionProtection;
        const auto first = Index(low), count = static_cast<size_t>(size / PageSize);
        for (size_t i = first; i < first + count; ++i) if (!pages_[i].protect) return NotCommitted;
        const auto status = Bind(backend); if (status) return status;
        const auto previous = pages_[first].protect;
        if (!Change(backend, low, size, protect, false, false)) return Unsuccessful;
        for (size_t i = first; i < first + count; ++i) pages_[i].protect = protect;
        address = low; bytes = size; oldProtect = previous; return Success;
    }
    uint32_t Query(VirtualMemoryBackend& backend, uint64_t address, BasicInformation& information) {
        if (!Inside(address, 1)) return InvalidParameter;
        const auto status = Bind(backend); if (status) return status;
        const auto first = Index(address);
        const auto& page = pages_[first];
        size_t end = first + 1;
        while (end < pages_.size() && pages_[end].allocation == page.allocation && pages_[end].protect == page.protect) ++end;
        information = {};
        information.baseAddress = base_ + first * PageSize;
        information.regionSize = (end - first) * PageSize;
        if (!page.allocation) information.state = 0x10000; // MEM_FREE
        else {
            const auto& region = regions_.at(page.allocation);
            information.allocationBase = page.allocation;
            information.allocationProtect = region.protect;
            information.state = page.protect ? Commit : Reserve;
            information.protect = page.protect; information.type = region.type;
        }
        return Success;
    }
    uint32_t UnmapView(VirtualMemoryBackend& backend, uint64_t address) {
        if (!Inside(address, 1)) return NotAllocated;
        auto region = RegionFor(address, 1);
        if (region == regions_.end() || region->second.type == Private) return NotAllocated;
        const auto status = Bind(backend); if (status) return status;
        const auto low = region->first, bytes = region->second.size;
        if (!Change(backend, low, bytes, 0, false, false)) return Unsuccessful;
        for (size_t i = Index(low), end = i + static_cast<size_t>(bytes / PageSize); i < end; ++i) pages_[i] = {};
        regions_.erase(region); return Success;
    }
private:
    struct Page { uint64_t allocation = 0; uint32_t protect = 0; };
    struct Region { uint64_t size; uint32_t protect, type, maximumProtect; };
    using RegionIterator = std::map<uint64_t, Region>::iterator;
    static bool RoundUp(uint64_t value, uint64_t alignment, uint64_t& result) {
        if (value > (std::numeric_limits<uint64_t>::max)() - (alignment - 1)) return false;
        result = (value + alignment - 1) & ~(alignment - 1); return true;
    }
    static bool Rounded(uint64_t address, uint64_t bytes, uint64_t alignment, uint64_t& low, uint64_t& size) {
        if (bytes > (std::numeric_limits<uint64_t>::max)() - address) return false;
        uint64_t end = 0; if (!RoundUp(address + bytes, PageSize, end)) return false;
        low = address & ~(alignment - 1); size = end - low; return size != 0;
    }
    bool Inside(uint64_t address, uint64_t bytes) const { return address >= base_ && address < end_ && bytes <= end_ - address; }
    size_t Index(uint64_t address) const { return static_cast<size_t>((address - base_) / PageSize); }
    bool FreeRange(uint64_t address, uint64_t bytes) const {
        for (size_t i = Index(address), end = i + static_cast<size_t>(bytes / PageSize); i < end; ++i)
            if (pages_[i].allocation) return false;
        return true;
    }
    bool FindFree(uint64_t bytes, bool top, uint64_t& address) const {
        const auto count = static_cast<size_t>(bytes / PageSize);
        if (count > pages_.size()) return false;
        size_t i = top ? ((pages_.size() - count) / 16) * 16 : 0;
        for (;;) {
            if (i + count > pages_.size()) return false;
            const auto candidate = base_ + i * PageSize;
            if (FreeRange(candidate, bytes)) { address = candidate; return true; }
            if (top) { if (i < 16) return false; i -= 16; }
            else i += 16;
        }
    }
    RegionIterator RegionFor(uint64_t address, uint64_t bytes) {
        if (!Inside(address, bytes)) return regions_.end();
        auto it = regions_.upper_bound(address);
        if (it == regions_.begin()) return regions_.end();
        --it;
        return address - it->first < it->second.size && bytes <= it->second.size - (address - it->first) ? it : regions_.end();
    }
    uint32_t Bind(VirtualMemoryBackend& backend) {
        if (faulted_) return Unsuccessful;
        const auto memory = backend.Memory();
        if (!memory.Contains(base_, end_ - base_)) return InvalidParameter;
        if (bound_) return bound_ == memory.Data() && ramBytes_ == memory.Bytes() ? Success : InvalidParameter;
        if (!backend.SetAccess(base_, end_ - base_, 0)) { faulted_ = true; backend.FailClosed(); return Unsuccessful; }
        bound_ = memory.Data(); ramBytes_ = memory.Bytes(); return Success;
    }
    bool Change(VirtualMemoryBackend& backend, uint64_t address, uint64_t bytes, uint32_t protect,
                bool onlyUncommitted, bool zeroNew) {
        const auto first = Index(address), count = static_cast<size_t>(bytes / PageSize);
        const auto memory = backend.Memory();
        for (size_t i = first; i < first + count; ) {
            if (onlyUncommitted && pages_[i].protect) { ++i; continue; }
            const auto oldAccess = Access(pages_[i].protect), newAccess = Access(protect);
            size_t end = i + 1;
            while (end < first + count && !(onlyUncommitted && pages_[end].protect) && Access(pages_[end].protect) == oldAccess) ++end;
            if (zeroNew) for (size_t p = i; p < end; ++p)
                if (!pages_[p].protect) memory.Zero(base_ + p * PageSize, PageSize);
            if (oldAccess != newAccess && !backend.SetAccess(base_ + i * PageSize, (end - i) * PageSize, newAccess)) {
                faulted_ = true; backend.FailClosed(); return false;
            }
            i = end;
        }
        return true;
    }
    uint64_t base_ = 0, end_ = 0;
    void* bound_ = nullptr;
    size_t ramBytes_ = 0;
    bool faulted_ = false;
    std::vector<Page> pages_;
    std::map<uint64_t, Region> regions_;
};
} }
