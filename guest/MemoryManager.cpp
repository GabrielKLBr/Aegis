#include "MemoryManager.h"
namespace guardian::memory {
namespace {
constexpr uint64_t Page = abi::PageBytes, Mask = 0x000FFFFFFFFFF000ull, Nx = 1ull << 63;
constexpr uint64_t Committed = 0x200, Guarded = 0x400, Blocked = 0x800, New = 1ull << 52;
void Zero(void* p, uint64_t n) noexcept { auto* b = static_cast<volatile uint8_t*>(p); for (uint64_t i = 0; i < n; ++i) b[i] = 0; }
bool Range(uint64_t& base, uint64_t& bytes) noexcept {
    if (!bytes || base < Arena || base >= Arena + ArenaBytes || bytes > Arena + ArenaBytes - base) return false;
    const auto end = (base + bytes + Page - 1) & ~(Page - 1); base &= ~(Page - 1); bytes = end - base; return true;
}
uint64_t Flags(uint32_t p) noexcept {
    uint64_t f = Committed | 4;
    if (p & Guard) f |= Guarded;
    if ((p & ~Guard) == NoAccess) f |= Blocked;
    if ((p & ~Guard) == ReadWrite) f |= 2;
    if ((p & ~Guard) != ExecuteRead) f |= Nx;
    if (!(f & (Guarded | Blocked))) f |= 1;
    return f;
}
uint32_t Protection(uint64_t e) noexcept {
    if (!(e & Committed)) return 0;
    const uint32_t p = (e & Blocked) ? NoAccess : ((e & Nx) ? ((e & 2) ? ReadWrite : ReadOnly) : ExecuteRead);
    return p | ((e & Guarded) ? Guard : 0);
}
}
bool Physical::Initialize(uint64_t backing, uint64_t bytes, const abi::MemoryRegion* map, uint32_t count) noexcept {
    if (!map || !bytes || bytes > abi::MaxRamBytes || (bytes & (Page - 1)) || !count || count > abi::MaxRegions) return false;
    uint64_t end = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = map[i];
        if (r.gpa != end || !r.bytes || (r.gpa & (Page - 1)) || (r.bytes & (Page - 1)) || r.gpa >= bytes || r.bytes > bytes - r.gpa || r.flags || r.reserved ||
            static_cast<uint32_t>(r.kind) > static_cast<uint32_t>(abi::MemoryKind::KernelStack) || (r.kind == abi::MemoryKind::Available && r.gpa < 0x100000)) return false;
        end += r.bytes;
    }
    if (end != bytes) return false;
    backing_ = backing; pages_ = bytes / Page; next_ = 0; counts_ = {}; budget_ = -1;
    for (uint64_t i = 0; i < pages_; ++i) frames_[i] = Frame::Unusable;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = map[i]; end = r.gpa + r.bytes;
        if (r.kind == abi::MemoryKind::Available) for (uint64_t at = r.gpa; at < end; at += Page) {
            if (at < 0x100000) return false;
            frames_[at / Page] = Frame::Free; ++counts_.free;
        }
    }
    return end == bytes;
}
Frame Physical::Kind(uint64_t gpa) const noexcept { return gpa % Page || gpa / Page >= pages_ ? Frame::Unusable : frames_[gpa / Page]; }
uint64_t Physical::Allocate(Frame kind, uint32_t pages) noexcept {
    if ((kind != Frame::Data && kind != Frame::Table) || !pages || pages > counts_.free || (budget_ >= 0 && uint64_t(budget_) < pages)) { ++counts_.failures; return 0; }
    uint64_t run = 0;
    for (uint64_t i = pages == 1 ? next_ : 0; i < pages_; ++i) {
        run = frames_[i] == Frame::Free ? run + 1 : 0;
        if (run != pages) continue;
        const auto start = i + 1 - pages;
        for (uint64_t n = start; n <= i; ++n) frames_[n] = kind;
        if (pages == 1 || (start <= next_ && next_ <= i)) next_ = i + 1;
        counts_.free -= pages;
        (kind == Frame::Data ? counts_.data : counts_.tables) += pages;
        const auto used = counts_.data + counts_.tables; if (used > counts_.peak) counts_.peak = used;
        if (budget_ >= 0) budget_ -= pages;
        Zero(At(start * Page), uint64_t(pages) * Page); return start * Page;
    }
    ++counts_.failures; return 0;
}
bool Physical::Release(uint64_t gpa, Frame kind, uint32_t pages) noexcept {
    if (!gpa || gpa % Page || !pages || gpa / Page >= pages_ || pages > pages_ - gpa / Page || (kind != Frame::Data && kind != Frame::Table)) return false;
    for (uint64_t i = gpa / Page; i < gpa / Page + pages; ++i) if (frames_[i] != kind) return false;
#ifdef GUARDIAN_NATIVE_NT
    if(pages==1&&kind==Frame::Data)for(auto& ref:references_)if(ref.page==gpa){if(!--ref.count)return false;if(ref.count==1)ref={};return true;}
    for(const auto& ref:references_)if(ref.page>=gpa&&ref.page-gpa<uint64_t(pages)*Page)return false;
#endif
    Zero(At(gpa), uint64_t(pages) * Page);
    for (uint64_t i = gpa / Page; i < gpa / Page + pages; ++i) frames_[i] = Frame::Free;
    if (gpa / Page < next_) next_ = gpa / Page;
    counts_.free += pages; (kind == Frame::Data ? counts_.data : counts_.tables) -= pages; return true;
}
bool Manager::Access(uint64_t gpa, uint32_t p) noexcept {
    ++requests_; const uint32_t access = (p & ~Guard) == ExecuteRead ? 5u : ((p & ~Guard) == ReadOnly ? 1u : 3u);
    return !permission_ || permission_(context_, gpa, access);
}
void Manager::Activate(Space* s) noexcept { active_ = s; if (s && switch_) switch_(context_, s->root); }
void Manager::Flush(Space& s) noexcept { if (active_ == &s && switch_) switch_(context_, s.root); }
bool Manager::Create(Space& s) noexcept {
    if (s.root || live_ >= MaxSpaces) return false;
    uint64_t* roots[]{&s.root, &s.pdpt, &s.identity, &s.bootstrap, &s.arena};
    for (auto* p : roots) if (!(*p = physical.Allocate(Frame::Table))) { Destroy(s); return false; }
    for (auto& p : s.privatePages) if (!(p = physical.Allocate(Frame::Data))) { Destroy(s); return false; }
    auto* root = static_cast<uint64_t*>(physical.At(s.root)); auto* pdpt = static_cast<uint64_t*>(physical.At(s.pdpt));
    auto* pd = static_cast<uint64_t*>(physical.At(s.identity)); auto* pt = static_cast<uint64_t*>(physical.At(s.bootstrap));
    root[0] = s.pdpt | 7; pdpt[0] = s.identity | 7; pdpt[1] = s.arena | 7;
    for (uint64_t i = 0; i < 512; ++i) { pd[i] = (i << 21) | 0x83; pt[i] = (cpu::UserCode + i * Page) | 3 | Nx; }
    pd[cpu::UserCode >> 21] = s.bootstrap | 7; pt[0] = cpu::UserCode | 5;
    pt[(cpu::UserStack - cpu::UserCode) / Page] = 0;
    for (uint64_t i = 0; i < 15; ++i) pt[(cpu::UserStack - cpu::UserCode) / Page + i + 1] = s.privatePages[i] | 7 | Nx;
    pt[(cpu::UserData - cpu::UserCode) / Page] = s.privatePages[15] | 7 | Nx;
    s.live = true; ++live_; return true;
}
bool Manager::Destroy(Space& s) noexcept {
    if (active_ == &s) return false;
#ifdef GUARDIAN_NATIVE_NT
    if(s.nativePage){Access(s.nativePage,ReadWrite);if(!physical.Release(s.nativePage,Frame::Data))return false;s.nativePage=0;}
    if(s.nativeTable){static_cast<uint64_t*>(physical.At(s.arena))[511]=0;if(!physical.Release(s.nativeTable,Frame::Table))return false;s.nativeTable=0;}
#endif
    if (s.arena) {
        for (auto& r : s.regions) if (r.used) { if (ReleaseRange(s, r.base) != Status::Success) return false; }
    }
    for (auto& p : s.privatePages) if (p) { if (!physical.Release(p, Frame::Data)) return false; p = 0; }
    uint64_t* roots[]{&s.arena, &s.bootstrap, &s.identity, &s.pdpt, &s.root};
    for (auto* p : roots) if (*p) { if (!physical.Release(*p, Frame::Table)) return false; *p = 0; }
    if (s.live) { s.live = false; --live_; }
    return true;
}
Region* Manager::Find(Space& s, uint64_t base, uint64_t bytes) noexcept {
    for (auto& r : s.regions) if (r.used && base >= r.base && base - r.base < r.bytes && bytes <= r.bytes - (base - r.base)) return &r;
    return nullptr;
}
uint64_t* Manager::Leaf(const Space& s, uint64_t at) const noexcept {
#ifdef GUARDIAN_NATIVE_NT
    if(s.nativeTable&&at>=0x7FFE0000&&at<0x7FFE1000)return static_cast<uint64_t*>(physical.At(s.nativeTable))+480;
#endif
    if (!s.arena || at < Arena || at >= Arena + ArenaBytes) return nullptr;
    const auto* pd = static_cast<const uint64_t*>(physical.At(s.arena)); const auto e = pd[(at - Arena) >> 21];
    return e ? static_cast<uint64_t*>(physical.At(e & Mask)) + ((at >> 12) & 511) : nullptr;
}
uint64_t Manager::Entry(const Space& s, uint64_t at) const noexcept { auto* p = Leaf(s, at); return p ? *p : 0; }
uint64_t Manager::Translate(const Space& s, uint64_t at) const noexcept { const auto e = Entry(s, at); return e & 1 ? (e & Mask) + (at & (Page - 1)) : 0; }
Status Manager::ReserveRange(Space& s, uint64_t& base, uint64_t bytes, uint32_t p) noexcept {
    if (!s.root || !ValidProtection(p)) return Status::Protection;
    if (!bytes || bytes > ArenaBytes) return Status::Invalid;
    uint64_t start = base ? (base & ~0xFFFFull) : Arena;
    if (base && (base < Arena || base >= Arena + ArenaBytes || bytes > Arena + ArenaBytes - base)) return Status::Invalid;
    const uint64_t end = base ? (base + bytes + Page - 1) & ~(Page - 1) : start + ((bytes + Page - 1) & ~(Page - 1));
    bytes = end - start;
    Region* slot = nullptr; for (auto& r : s.regions) if (!r.used) { slot = &r; break; }
    if (!slot) return Status::NoMemory;
    for (;;) {
        if (start >= Arena + ArenaBytes || bytes > Arena + ArenaBytes - start) return Status::NoMemory;
        bool conflict = false;
        for (const auto& r : s.regions) if (r.used && start < r.base + r.bytes && r.base < start + bytes) {
            if (base) return Status::Conflict;
            start = (r.base + r.bytes + 0xFFFF) & ~0xFFFFull; conflict = true; break;
        }
        if (!conflict) break;
    }
    *slot = {start, bytes, p, 1}; base = start; return Status::Success;
}
void Manager::Prune(Space& s) noexcept {
    auto* pd = static_cast<uint64_t*>(physical.At(s.arena));
    for (uint64_t i = 0; i < ArenaBytes >> 21; ++i) if (pd[i]) {
        auto* pt = static_cast<uint64_t*>(physical.At(pd[i] & Mask)); bool empty = true;
        for (unsigned n = 0; n < 512; ++n) if (pt[n]) { empty = false; break; }
        if (empty) { const auto page = pd[i] & Mask; pd[i] = 0; physical.Release(page, Frame::Table); }
    }
}
void Manager::Drop(uint64_t& e) noexcept {
    const auto page = e & Mask; Access(page, ReadWrite); e = 0; physical.Release(page, Frame::Data);
}
Status Manager::CommitRange(Space& s, uint64_t base, uint64_t bytes, uint32_t p) noexcept {
    if (!ValidProtection(p)) return Status::Protection;
    if (!Range(base, bytes)) return Status::Invalid;
    if (!Find(s, base, bytes)) return Status::NotAllocated;
    bool ok = true;
    for (uint64_t at = base; at < base + bytes; at += Page) {
        auto* leaf = Leaf(s, at);
        if (!leaf) {
            const auto page = physical.Allocate(Frame::Table); if (!page) { ok = false; break; }
            static_cast<uint64_t*>(physical.At(s.arena))[(at - Arena) >> 21] = page | 7; leaf = Leaf(s, at);
        }
        if (*leaf & Committed) continue;
        const auto page = physical.Allocate(Frame::Data); if (!page) { ok = false; break; }
        *leaf = page | Flags(p) | New;
        if (!Access(page, p)) { ok = false; break; }
    }
    for (uint64_t at = base; at < base + bytes; at += Page) {
        auto* leaf = Leaf(s, at); if (!leaf || !(*leaf & New)) continue;
        if (ok) *leaf &= ~New; else Drop(*leaf);
    }
    if (!ok) Prune(s);
    Flush(s); return ok ? Status::Success : Status::NoMemory;
}
Status Manager::DecommitRange(Space& s, uint64_t base, uint64_t bytes) noexcept {
    if (!bytes) { auto* r = Find(s, base, 1); if (!r || base != r->base) return Status::Invalid; bytes = r->bytes; }
    if (!Range(base, bytes)) return Status::Invalid;
    if (!Find(s, base, bytes)) return Status::NotAllocated;
    for (uint64_t at = base; at < base + bytes; at += Page) { auto* leaf = Leaf(s, at); if (leaf && (*leaf & Committed)) Drop(*leaf); }
    Prune(s); Flush(s); return Status::Success;
}
Status Manager::ReleaseRange(Space& s, uint64_t base, uint64_t bytes) noexcept {
    if (bytes) return Status::Invalid;
    auto* r = Find(s, base, 1); if (!r || base != r->base) return Status::NotAllocated;
    const auto status = DecommitRange(s, base, r->bytes); if (status == Status::Success) *r = {}; return status;
}
Status Manager::ProtectRange(Space& s, uint64_t base, uint64_t bytes, uint32_t p, uint32_t& old) noexcept {
    if (!ValidProtection(p)) return Status::Protection;
    if (!Range(base, bytes)) return Status::Invalid;
    if (!Find(s, base, bytes)) return Status::NotAllocated;
    for (uint64_t at = base; at < base + bytes; at += Page) if (!(Entry(s, at) & Committed)) return Status::NotCommitted;
    // Permission failures at the host boundary are terminal in the guest. Native
    // tests can inject a transient failure; restore all grants before returning.
    uint64_t at = base;
    for (; at < base + bytes; at += Page) if (!Access(Entry(s, at) & Mask, p)) break;
    if (at != base + bytes) {
        for (uint64_t back = base; back < at; back += Page) Access(Entry(s, back) & Mask, Protection(Entry(s, back)));
        return Status::NoMemory;
    }
    old = Protection(Entry(s, base));
    for (at = base; at < base + bytes; at += Page) { auto* leaf = Leaf(s, at); *leaf = (*leaf & Mask) | Flags(p); }
    Flush(s); return Status::Success;
}
Status Manager::Query(const Space& s, uint64_t at, Information& out) const noexcept {
    if (at < Arena || at >= Arena + ArenaBytes) return Status::Invalid;
    at &= ~(Page - 1); out = {}; out.base = at;
    const Region* region = nullptr;
    for (const auto& r : s.regions) if (r.used && at >= r.base && at - r.base < r.bytes) { region = &r; break; }
    const auto prot = Protection(Entry(s, at));
    out.state = region ? (prot ? Commit : Reserve) : Free; out.protect = prot;
    out.allocationBase = region ? region->base : 0; out.allocationProtect = region ? region->protection : 0; out.type = region ? 0x20000u : 0;
    uint64_t end = region ? region->base + region->bytes : Arena + ArenaBytes;
    if (!region) for (const auto& r : s.regions) if (r.used && r.base > at && r.base < end) end = r.base;
    uint64_t next = at + Page;
    if (region) for (; next < end && Protection(Entry(s, next)) == prot; next += Page) {}
    else next = end;
    out.bytes = next - at; return Status::Success;
}
bool Manager::ConsumeGuard(Space& s, uint64_t at) noexcept {
    auto* leaf = Leaf(s, at); if (!leaf || !(*leaf & Guarded)) return false;
    *leaf = (*leaf & ~Guarded) | 1; Flush(s); return true;
}
#ifdef GUARDIAN_NATIVE_NT
bool Physical::Reference(uint64_t gpa) noexcept {if(Kind(gpa)!=Frame::Data)return false;
    for(auto& ref:references_)if(ref.page==gpa){if(ref.count==UINT16_MAX)return false;++ref.count;return true;}
    for(auto& ref:references_)if(!ref.page){ref={gpa,2};return true;}return false;}
bool Manager::NativeSharedData(Space& s,const uint8_t* initial) noexcept {
    if(!s.arena||s.nativeTable||s.nativePage)return false;
    const auto table=physical.Allocate(Frame::Table),page=physical.Allocate(Frame::Data);
    if(!table||!page){if(table)physical.Release(table,Frame::Table);if(page)physical.Release(page,Frame::Data);return false;}
    if(initial){auto* out=static_cast<uint8_t*>(physical.At(page));for(uint32_t i=0;i<4096;++i)out[i]=initial[i];}
    // The guest kernel updates the clock in this frame; the user PTE is RO/NX.
    if(!Access(page,ReadWrite)){physical.Release(table,Frame::Table);physical.Release(page,Frame::Data);return false;}
    s.nativeTable=table;s.nativePage=page;static_cast<uint64_t*>(physical.At(table))[480]=page|Flags(ReadOnly);
    static_cast<uint64_t*>(physical.At(s.arena))[511]=table|7;Flush(s);return true;
}
Status Manager::MapReferences(Space& s,uint64_t base,const uint64_t* pages,uint32_t count,uint32_t protection) noexcept {
    if(!pages||!count||count>64||(base&4095)||(protection!=ReadOnly&&protection!=ReadWrite)||!Find(s,base,uint64_t(count)*Page))return Status::Invalid;
    for(uint32_t i=0;i<count;++i)if(physical.Kind(pages[i])!=Frame::Data||(Entry(s,base+i*Page)&Committed))return Status::Invalid;
    uint32_t completed=0;for(;completed<count;++completed){const auto at=base+completed*Page;auto* leaf=Leaf(s,at);
        if(!leaf){const auto table=physical.Allocate(Frame::Table);if(!table)break;static_cast<uint64_t*>(physical.At(s.arena))[(at-Arena)>>21]=table|7;leaf=Leaf(s,at);}
        if(!physical.Reference(pages[completed]))break;*leaf=pages[completed]|Flags(protection);}
    if(completed!=count){for(uint32_t i=0;i<completed;++i){auto* leaf=Leaf(s,base+i*Page);physical.Release(*leaf&Mask,Frame::Data);*leaf=0;}Prune(s);Flush(s);return Status::NoMemory;}
    Flush(s);return Status::Success;
}
#endif
}
