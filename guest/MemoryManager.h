#pragma once
#include "Heap.h"
#include "guardian/Memory.h"
#include "guardian/Cpu.h"
namespace guardian::memory {
// The same freestanding algorithms run in CPL0 and in the native fault-injection tests.
using Permission = bool (*)(void*, uint64_t, uint32_t) noexcept;
using Switch = void (*)(void*, uint64_t) noexcept;
enum class Frame : uint8_t { Unusable, Free, Data, Table };
struct Counts { uint64_t free, data, tables, peak, failures; };
class Physical {
public:
    bool Initialize(uint64_t backing, uint64_t bytes, const abi::MemoryRegion* map, uint32_t count) noexcept;
    uint64_t Allocate(Frame kind, uint32_t pages = 1) noexcept;
    bool Release(uint64_t gpa, Frame kind, uint32_t pages = 1) noexcept;
#ifdef GUARDIAN_NATIVE_NT
    bool Reference(uint64_t gpa) noexcept;
#endif
    void* At(uint64_t gpa) const noexcept { return reinterpret_cast<void*>(backing_ + gpa); }
    Frame Kind(uint64_t gpa) const noexcept;
    Counts Statistics() const noexcept { return counts_; }
    uint64_t Bytes() const noexcept {return pages_*4096;}
    // Deterministic finite quota also exercises real allocator rollback in the guest.
    void Budget(int64_t frames) noexcept { budget_ = frames; }
private:
    Frame frames_[abi::MaxRamBytes / abi::PageBytes]{};
    uint64_t backing_ = 0, pages_ = 0, next_ = 0;
    int64_t budget_ = -1;
#ifdef GUARDIAN_NATIVE_NT
    struct ReferenceEntry {uint64_t page=0;uint16_t count=0;};
    ReferenceEntry references_[512]{};
#endif
    Counts counts_{};
};
struct Region { uint64_t base, bytes; uint32_t protection, used; };
struct Space {
    uint64_t root = 0, pdpt = 0, identity = 0, bootstrap = 0, arena = 0;
    uint64_t privatePages[16]{};
    Region regions[MaxRegions]{};
    bool live = false;
#ifdef GUARDIAN_NATIVE_NT
    uint64_t nativeTable=0,nativePage=0;
#endif
};
class Manager {
public:
    Physical physical;
    void Configure(void* context, Permission permission, Switch change) noexcept { context_ = context; permission_ = permission; switch_ = change; }
    bool Create(Space& space) noexcept;
    bool Destroy(Space& space) noexcept; // The active address space must first be switched out.
    void Activate(Space* space) noexcept;
    Status ReserveRange(Space& s, uint64_t& base, uint64_t bytes, uint32_t protection) noexcept;
    Status CommitRange(Space& s, uint64_t base, uint64_t bytes, uint32_t protection) noexcept;
    Status DecommitRange(Space& s, uint64_t base, uint64_t bytes) noexcept;
    Status ReleaseRange(Space& s, uint64_t base, uint64_t bytes = 0) noexcept;
    Status ProtectRange(Space& s, uint64_t base, uint64_t bytes, uint32_t protection, uint32_t& old) noexcept;
    Status Query(const Space& s, uint64_t at, Information& out) const noexcept;
    bool ConsumeGuard(Space& s, uint64_t at) noexcept;
    uint64_t Translate(const Space& s, uint64_t at) const noexcept;
    uint64_t Entry(const Space& s, uint64_t at) const noexcept;
#ifdef GUARDIAN_NATIVE_NT
    bool NativeSharedData(Space&,const uint8_t*) noexcept;
    Status MapReferences(Space&,uint64_t,const uint64_t*,uint32_t,uint32_t) noexcept;
#endif
    uint64_t PhysicalRequests() const noexcept { return requests_; }
private:
    Region* Find(Space& s, uint64_t base, uint64_t bytes) noexcept;
    uint64_t* Leaf(const Space& s, uint64_t at) const noexcept;
    bool Access(uint64_t gpa, uint32_t protection) noexcept;
    void Flush(Space& s) noexcept;
    void Prune(Space& s) noexcept;
    void Drop(uint64_t& entry) noexcept;
    void* context_ = nullptr;
    Permission permission_ = nullptr; Switch switch_ = nullptr;
    Space* active_ = nullptr; uint64_t requests_ = 0;
    uint32_t live_ = 0;
};
}
