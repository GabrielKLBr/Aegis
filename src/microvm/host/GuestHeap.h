// The MiniNt heap remains a single configured guest arena. Allocation metadata
// lives outside guest RAM; freeing/coalescing never follows guest-owned links.
// This is a reusable allocator, not Windows' heap implementation or virtual memory.
#pragma once
#include "GuestMemory.h"
#include <limits>
#include <iterator>
#include <map>
#include <new>
#include <stdexcept>
#include <utility>

namespace av { namespace microvm {
class GuestHeap {
public:
    static constexpr uint64_t InvalidSize = (std::numeric_limits<uint64_t>::max)();
    static constexpr size_t MaxSegments = 16384;
    uint64_t Base() const { return base_; }
    uint64_t End() const { return end_; }
    void Configure(uint64_t base, uint64_t end) {
        if (!base || (base & 15) || (end & 15) || end <= base || end - base <= 0x1000)
            throw std::invalid_argument("Guest heap requires an aligned arena and a reserved header page");
        std::map<uint64_t, Block> fresh;
        fresh.emplace(base + 0x1000, Block{end - base - 0x1000, 0, false});
        blocks_ = std::move(fresh);
        base_ = base; end_ = end; bound_ = nullptr; boundBytes_ = 0;
    }
    uint64_t Handle(const GuestMemory& memory) { return Bind(memory) ? base_ : 0; }
    uint64_t Allocate(const GuestMemory& memory, uint64_t heap, uint64_t flags, uint64_t bytes) {
        uint64_t span = 0;
        if (!Valid(memory, heap) || (flags & ~uint64_t(0x9)) || !Aligned(bytes, span)) return 0;
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
            if (it->second.used || it->second.span < span) continue;
            const auto address = it->first;
            const auto remainder = it->second.span - span;
            if (remainder) {
                if (blocks_.size() >= MaxSegments) return 0;
                try { blocks_.emplace(address + span, Block{remainder, 0, false}); }
                catch (const std::bad_alloc&) { return 0; }
            }
            it->second = Block{span, bytes, true};
            if (flags & 8) memory.Zero(address, bytes);
            return address;
        }
        return 0;
    }
    bool Free(const GuestMemory& memory, uint64_t heap, uint64_t flags, uint64_t pointer) {
        if (!Valid(memory, heap) || (flags & ~uint64_t(1))) return false;
        if (!pointer) return true;
        auto it = blocks_.find(pointer);
        if (it == blocks_.end() || !it->second.used) return false;
        it->second.used = false; it->second.requested = 0;
        Coalesce(it);
        return true;
    }
    uint64_t Size(const GuestMemory& memory, uint64_t heap, uint64_t flags, uint64_t pointer) {
        if (!Valid(memory, heap) || (flags & ~uint64_t(1))) return InvalidSize;
        auto it = blocks_.find(pointer);
        return it != blocks_.end() && it->second.used ? it->second.requested : InvalidSize;
    }
    uint64_t Reallocate(const GuestMemory& memory, uint64_t heap, uint64_t flags,
                        uint64_t pointer, uint64_t bytes) {
        uint64_t span = 0;
        if (!Valid(memory, heap) || (flags & ~uint64_t(0x19)) || !Aligned(bytes, span)) return 0;
        auto it = blocks_.find(pointer);
        if (it == blocks_.end() || !it->second.used) return 0;
        const auto oldBytes = it->second.requested;
        if (span <= it->second.span) {
            const auto remainder = it->second.span - span;
            if (remainder && blocks_.size() < MaxSegments) {
                try {
                    auto tail = blocks_.emplace(pointer + span, Block{remainder, 0, false}).first;
                    it->second.span = span;
                    Coalesce(tail);
                } catch (const std::bad_alloc&) { return 0; }
            }
            if ((flags & 8) && bytes > oldBytes) memory.Zero(pointer + oldBytes, bytes - oldBytes);
            it->second.requested = bytes;
            return pointer;
        }
        auto next = std::next(it);
        const auto needed = span - it->second.span;
        if (next != blocks_.end() && !next->second.used && next->second.span >= needed) {
            if (next->second.span == needed) blocks_.erase(next);
            else {
                // Re-keying the node allocates no host memory; failure cannot
                // destroy the original block during an in-place resize.
                auto node = blocks_.extract(next);
                node.key() += needed; node.mapped().span -= needed;
                blocks_.insert(std::move(node));
            }
            it->second.span = span; it->second.requested = bytes;
            if (flags & 8) memory.Zero(pointer + oldBytes, bytes - oldBytes);
            return pointer;
        }
        if (flags & 0x10) return 0;
        const auto moved = Allocate(memory, heap, flags & 0x9, bytes);
        if (!moved) return 0;
        memory.Copy(moved, pointer, oldBytes < bytes ? oldBytes : bytes);
        Free(memory, heap, 0, pointer);
        return moved;
    }
private:
    struct Block { uint64_t span, requested; bool used; };
    using Iterator = std::map<uint64_t, Block>::iterator;
    static bool Aligned(uint64_t bytes, uint64_t& span) {
        if (bytes > InvalidSize - 15) return false;
        span = bytes ? (bytes + 15) & ~uint64_t(15) : 16;
        return true;
    }
    bool Bind(const GuestMemory& memory) {
        if (!base_ || !memory.Contains(base_, end_ - base_)) return false;
        if (bound_ && (bound_ != memory.Data() || boundBytes_ != memory.Bytes())) return false;
        bound_ = memory.Data(); boundBytes_ = memory.Bytes(); return true;
    }
    bool Valid(const GuestMemory& memory, uint64_t heap) { return heap && heap == base_ && Bind(memory); }
    void Coalesce(Iterator it) {
        if (it != blocks_.begin()) {
            auto previous = std::prev(it);
            if (!previous->second.used) {
                previous->second.span += it->second.span;
                blocks_.erase(it); it = previous;
            }
        }
        const auto next = std::next(it);
        if (next != blocks_.end() && !next->second.used) {
            it->second.span += next->second.span;
            blocks_.erase(next);
        }
    }
    uint64_t base_ = 0, end_ = 0;
    void* bound_ = nullptr;
    size_t boundBytes_ = 0;
    std::map<uint64_t, Block> blocks_;
};
} }
