#pragma once
#include <stdint.h>
namespace guardian::memory {
// Fixed arena heap adapted from the existing GuestHeap: 16-byte alignment,
// splitting/coalescing, zero allocation and realloc preserving data on failure.
class Heap {
public:
    bool Initialize(void* memory, uint32_t bytes) noexcept;
    void* Allocate(uint32_t bytes, bool zero = false) noexcept;
    bool Release(void* address) noexcept;
    void* Reallocate(void* address, uint32_t bytes) noexcept;
    uint32_t Used() const noexcept { return used_; }
    uint32_t Size(void* p) noexcept {const auto* b=Find(p);return b?b->requested:0;}
private:
    struct alignas(16) Block { uint32_t bytes, next, used, requested; };
    Block* Find(void* p) noexcept;
    uint8_t* arena_ = nullptr; uint32_t bytes_ = 0, used_ = 0;
};
inline void HeapZero(void* p,uint32_t n) noexcept {auto* b=static_cast<volatile uint8_t*>(p);for(uint32_t i=0;i<n;++i)b[i]=0;}
inline bool Heap::Initialize(void* memory, uint32_t bytes) noexcept {
    if (!memory || (reinterpret_cast<uint64_t>(memory) & 15) || bytes < 32 || (bytes & 15)) return false;
    arena_ = static_cast<uint8_t*>(memory); bytes_ = bytes; used_ = 0; *reinterpret_cast<Block*>(arena_) = {bytes - 16, 0, 0, 0}; return true;
}
inline Heap::Block* Heap::Find(void* p) noexcept {
    if (!arena_ || !p) return nullptr;
    for (uint32_t at = 0;;) { auto* b = reinterpret_cast<Block*>(arena_ + at); if (arena_ + at + 16 == p) return b->used ? b : nullptr; if (!b->next) return nullptr; at = b->next; }
}
inline void* Heap::Allocate(uint32_t bytes, bool zero) noexcept {
    if (!arena_ || !bytes || bytes > bytes_ - 16) return nullptr;
    const auto rounded = (bytes + 15u) & ~15u;
    for (uint32_t at = 0;;) {
        auto* b = reinterpret_cast<Block*>(arena_ + at);
        if (!b->used && b->bytes >= rounded) {
            if (b->bytes >= rounded + 32) { const auto split = at + 16 + rounded; *reinterpret_cast<Block*>(arena_ + split) = {b->bytes - rounded - 16, b->next, 0, 0}; b->next = split; b->bytes = rounded; }
            b->used = 1; b->requested = bytes; ++used_; auto* p = arena_ + at + 16; if (zero) HeapZero(p, bytes); return p;
        }
        if (!b->next) return nullptr; at = b->next;
    }
}
inline bool Heap::Release(void* address) noexcept {
    auto* b = Find(address); if (!b) return false; HeapZero(address, b->bytes); b->used = b->requested = 0; --used_;
    for (uint32_t at = 0;;) {
        auto* p = reinterpret_cast<Block*>(arena_ + at); if (!p->next) break;
        auto* n = reinterpret_cast<Block*>(arena_ + p->next);
        if (!p->used && !n->used) { p->bytes += n->bytes + 16; p->next = n->next; } else at = p->next;
    }
    return true;
}
inline void* Heap::Reallocate(void* address, uint32_t bytes) noexcept {
    if (!address) return Allocate(bytes);
    auto* b = Find(address); if (!b || !bytes) return nullptr;
    if (bytes <= b->bytes) { b->requested = bytes; return address; }
    auto* next = static_cast<uint8_t*>(Allocate(bytes)); if (!next) return nullptr;
    const auto n = bytes < b->requested ? bytes : b->requested;
    for (uint32_t i = 0; i < n; ++i) next[i] = static_cast<const uint8_t*>(address)[i];
    Release(address); return next;
}
}
