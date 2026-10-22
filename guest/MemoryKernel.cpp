#include "MemoryKernel.h"
#include "MemoryManager.h"
#include "guardian/Validate.h"
#include <intrin.h>
using namespace guardian;
namespace {
memory::Manager manager;
memory::Space spaces[2];
memory::Report result;
memory::Space* active = nullptr;
void (*verify)(bool, uint32_t) noexcept = nullptr;
uint64_t sequence = 0, session = 0;
void Check(bool b, uint32_t code) noexcept { verify(b, 0x100 + code); }
void Copy(volatile uint8_t* to, const void* from, uint64_t n) noexcept {
    const auto* p = static_cast<const uint8_t*>(from); for (uint64_t i = 0; i < n; ++i) to[i] = p[i];
}
bool Permission(void*, uint64_t gpa, uint32_t access) noexcept {
    const memory::Request r{memory::Magic, memory::Version, sizeof(r), session, ++sequence, gpa, abi::PageBytes, access, 0};
    Copy(reinterpret_cast<volatile uint8_t*>(memory::RequestGpa), &r, sizeof(r)); _ReadWriteBarrier(); __outbyte(memory::Port, 1); return true;
}
void Change(void*, uint64_t root) noexcept { __writecr3(root); ++result.switches; }
void Activate(memory::Space& s) noexcept { active = &s; manager.Activate(&s); }
uint64_t ReadResult() noexcept { return reinterpret_cast<volatile cpu::UserState*>(cpu::UserData)->x87Result; }
void Read(uint64_t at, uint64_t value) noexcept { Check(MemoryRunUser(0, at, 0) == 0 && ReadResult() == value, 1); }
void Write(uint64_t at, uint64_t value) noexcept { Check(MemoryRunUser(1, at, value) == 0 && ReadResult() == value, 2); }
void Fault(uint64_t mode, uint64_t at, uint64_t error) noexcept {
    const auto count = result.userFaults;
    Check(MemoryRunUser(mode, at, 0) == 14 && result.userFaults == count + 1 && result.faultAddress[count] == at && result.faultError[count] == error, 3);
}
}
void MemoryInitialize(const abi::BootInfo& b, void (*check)(bool, uint32_t) noexcept) noexcept {
    verify = check; session = b.bootId;
    result.magic = memory::Magic; result.version = memory::Version; result.bytes = sizeof(result); result.bootId = session;
    Check(abi::Covered(reinterpret_cast<const abi::MemoryRegion*>(b.memoryMapGpa), b.memoryMapCount,
        memory::RequestGpa, 2 * abi::PageBytes, abi::MemoryKind::BootData), 50);
    Check(manager.physical.Initialize(0, b.ramBytes, reinterpret_cast<const abi::MemoryRegion*>(b.memoryMapGpa), b.memoryMapCount), 4);
    result.freeBefore = manager.physical.Statistics().free;
    manager.Configure(nullptr, Permission, Change);
    Check(manager.Create(spaces[0]) && manager.Create(spaces[1]), 5);
    Activate(spaces[0]); result.checks |= 1;
}
void MemoryRecordFault(const TrapFrame& f) noexcept {
    if (f.vector != 14 || (f.cs & 3) != 3) return;
    const auto at = GUARDIAN_FAULT_ADDRESS();
    if (result.userFaults < 8) { result.faultAddress[result.userFaults] = at; result.faultError[result.userFaults] = f.error; }
    ++result.userFaults;
    // A guard is consumed once. The current access is reported/terminated;
    // the next controlled user entry can access the restored protection.
    if (manager.ConsumeGuard(*active, at)) ++result.guardFaults;
}
void MemoryPublish() noexcept {
    result.physicalRequests = manager.PhysicalRequests();
    Copy(reinterpret_cast<volatile uint8_t*>(memory::ReportGpa), &result, sizeof(result)); _ReadWriteBarrier(); __outbyte(memory::Port, 2);
}
void MemorySelfTest() noexcept {
    using namespace memory;
    auto& a = spaces[0]; auto& b = spaces[1];
    uint64_t va = Arena; uint32_t old = 0; Information info{};
    Check(manager.ReserveRange(a, va, 3 * abi::PageBytes, ReadWrite) == Status::Success && va == Arena, 6);
    Check(manager.Query(a, va, info) == Status::Success && info.state == Reserve && info.bytes == 3 * abi::PageBytes, 7);
    Fault(0, va, 4); // Reserved memory has no physical backing.
    Check(manager.CommitRange(a, va, 2 * abi::PageBytes, ReadWrite) == Status::Success, 8);
    Read(va, 0); Write(va, 0x123456789ABCDEF0ull);
    Check(manager.CommitRange(a, va, abi::PageBytes, ReadOnly) == Status::Success, 9); Read(va, 0x123456789ABCDEF0ull);
    Check(manager.ProtectRange(a, va, 3 * abi::PageBytes, ReadOnly, old) == Status::NotCommitted, 10);
    Write(va, 42); result.checks |= 2;
    Check(manager.ProtectRange(a, va, abi::PageBytes, ReadOnly, old) == Status::Success && old == ReadWrite, 11);
    Fault(1, va, 7); Read(va, 42);
    Check(manager.ProtectRange(a, va, abi::PageBytes, NoAccess, old) == Status::Success, 12); Fault(0, va, 4);
    Check(manager.ProtectRange(a, va, abi::PageBytes, ReadWrite | Guard, old) == Status::Success, 13);
    Fault(0, va, 4); Read(va, 42); Write(va, 43); result.checks |= 4;
    // Executable data is denied by the guest NX bit before reaching WHP.
    Fault(2, va, 0x15);
    const auto pa = manager.Translate(a, va); auto* code = static_cast<volatile uint8_t*>(manager.physical.At(pa));
    code[0] = 0xB8; code[1] = 0x78; code[2] = 0x56; code[3] = 0x34; code[4] = 0x12; code[5] = 0xC3;
    Check(manager.ProtectRange(a, va, abi::PageBytes, ExecuteRead, old) == Status::Success, 14);
    Check(MemoryRunUser(2, va, 0) == 0 && ReadResult() == 0x12345678, 15);
    Check(manager.ProtectRange(a, va, abi::PageBytes, 0x40, old) == Status::Protection, 16);
    Check(manager.ProtectRange(a, va, abi::PageBytes, ReadWrite, old) == Status::Success, 17); Write(va, 111); result.checks |= 8;
    uint64_t vb = va;
    Check(manager.ReserveRange(b, vb, abi::PageBytes, ReadWrite) == Status::Success && manager.CommitRange(b, vb, abi::PageBytes, ReadWrite) == Status::Success, 18);
    Check(manager.Translate(b, vb) != manager.Translate(a, va), 19);
    Activate(b); Read(vb, 0); Write(vb, 222); Activate(a); Read(va, 111); Activate(b); Read(vb, 222); Activate(a);
    // Kernel image and the physical alias of another space remain supervisor-only.
    Fault(0, 0x200000, 5); Fault(0, manager.Translate(b, vb), 5); result.checks |= 16;
    Check(manager.DecommitRange(a, va, abi::PageBytes) == Status::Success, 20); Fault(0, va, 4);
    Check(manager.CommitRange(a, va, abi::PageBytes, ReadWrite) == Status::Success, 21); Read(va, 0);
    Check(manager.ReleaseRange(a, va) == Status::Success && manager.Query(a, va, info) == Status::Success && info.state == Free, 22);
    Check(manager.ReleaseRange(b, vb) == Status::Success, 23); result.checks |= 32;
    const auto before = manager.physical.Statistics();
    // Fail each allocation position, including after crossing a page-table boundary.
    for (int64_t budget = 0; budget < 7; ++budget) {
        uint64_t at = Arena + 0x1FE000;
        Check(manager.ReserveRange(a, at, 5 * abi::PageBytes, ReadWrite) == Status::Success, 24);
        manager.physical.Budget(budget);
        Check(manager.CommitRange(a, Arena + 0x1FE000, 5 * abi::PageBytes, ReadWrite) == Status::NoMemory, 25);
        manager.physical.Budget(-1);
        const auto now = manager.physical.Statistics();
        Check(now.free == before.free && now.data == before.data && now.tables == before.tables && !manager.Entry(a, Arena + 0x1FE000), 26);
        Check(manager.ReleaseRange(a, at) == Status::Success, 27);
    }
    result.checks |= 64;
    for (unsigned i = 0; i < 128; ++i) {
        uint64_t at = 0;
        Check(manager.ReserveRange(a, at, 8192, ReadWrite) == Status::Success && manager.CommitRange(a, at, 8192, ReadWrite) == Status::Success, 28);
        Write(at, i + 1); Check(manager.ReleaseRange(a, at) == Status::Success, 29); ++result.cycles;
    }
    Check(manager.physical.Statistics().free == before.free, 30); result.checks |= 128;
    const auto heapPage = manager.physical.Allocate(Frame::Data, 16); Check(heapPage != 0, 31);
    Heap heap; Check(heap.Initialize(manager.physical.At(heapPage), 65536), 32);
    auto* p = static_cast<uint8_t*>(heap.Allocate(31, true)); Check(p && !(reinterpret_cast<uint64_t>(p) & 15) && p[0] == 0, 33); p[0] = 0x5A;
    Check(!heap.Reallocate(p, UINT32_MAX) && p[0] == 0x5A && heap.Used() == 1, 34);
    auto* q = static_cast<uint8_t*>(heap.Reallocate(p, 1024)); Check(q && q[0] == 0x5A && heap.Release(q) && !heap.Release(q), 35);
    auto* all = heap.Allocate(65520); Check(all && heap.Release(all) && !heap.Used(), 36);
    Check(manager.physical.Release(heapPage, Frame::Data, 16), 37); result.heapChecks = 5; result.checks |= 256;
    // Space creation is transactional even if a stack frame cannot be obtained.
    for (int64_t budget = 0; budget < 21; ++budget) {
        Space temporary{}; manager.physical.Budget(budget); Check(!manager.Create(temporary), 38); manager.physical.Budget(-1);
        Check(!temporary.root && manager.physical.Statistics().free == before.free, 39);
    }
    result.checks |= 512;
    // Exhaust actual free physical memory, rather than relying only on quotas.
    // The held pages themselves form a bounded linked list; no host allocator.
    uint64_t held = 0;
    for (;;) {
        const auto page = manager.physical.Allocate(Frame::Data); if (!page) break;
        *static_cast<volatile uint64_t*>(manager.physical.At(page)) = held; held = page; ++result.exhaustedFrames;
    }
    uint64_t empty = Arena;
    Check(manager.ReserveRange(a, empty, abi::PageBytes, ReadWrite) == Status::Success &&
        manager.CommitRange(a, empty, abi::PageBytes, ReadWrite) == Status::NoMemory, 46);
    Check(manager.Query(a, empty, info) == Status::Success && info.state == Reserve && manager.ReleaseRange(a, empty) == Status::Success, 47);
    while (held) { const auto next = *static_cast<volatile uint64_t*>(manager.physical.At(held)); Check(manager.physical.Release(held, Frame::Data), 48); held = next; }
    Check(result.exhaustedFrames == before.free && manager.physical.Statistics().free == before.free, 49);
    Check(manager.ReserveRange(a, va = Arena, UINT64_MAX, ReadWrite) == Status::Invalid &&
        manager.CommitRange(a, UINT64_MAX - 1, 8192, ReadWrite) == Status::Invalid, 40);
    result.checks |= 1024;
    // Keep one address space for the live CPU; destroy the other and recreate it.
    Check(!manager.Destroy(a) && manager.Destroy(b), 41);
    Check(manager.Create(b), 42); Activate(b); Check(manager.Destroy(a), 43);
    const auto final = manager.physical.Statistics();
    Check(final.free + 21 == result.freeBefore && final.tables == 5 && final.data == 16 && result.userFaults == 8 && result.guardFaults == 1, 44);
    result.freeAfter = final.free; result.peakFrames = final.peak; result.allocationFailures = final.failures;
    result.checks |= 2048;
    Check(result.checks == RequiredChecks, 45);
}
