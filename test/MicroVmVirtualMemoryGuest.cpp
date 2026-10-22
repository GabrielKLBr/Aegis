// WHP virtual-memory integration tests. Shared fixture generates all guest code.
#include "MicroVmTestGuest.h"

int main() {
    BOOL available = FALSE; UINT32 written = 0;
    if (FAILED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &available, sizeof(available), &written)) || !available) {
        std::cout << "SKIP: WHP unavailable; no settings changed\n"; return 77;
    }
    try {
        Fixture f;
        check(f.Allocate(0, 8192, M::Reserve, 4) == 0, "reserve failed");
        const auto base = f.Read(BaseSlot);
        check(base == 0xA00000 && f.Query(base).state == M::Reserve, "reservation state wrong");
        f.Probe(base, 0, 0);
        check(f.Allocate(base, 4096, M::Commit, 4) == 0 && f.Probe(base, 0) == 0, "commit/zeroing failed");
        check(f.Probe(base, 1) == Marker, "guest write failed");
        f.memory.Write(OldSlot, uint64_t(0xAABBCCDDFFFFFFFFull));
        check(f.Protect(base + 3, 7, 2) == 0, "read-only protect failed");
        check(f.Read(OldSlot) == 0xAABBCCDD00000004ull && f.Read(BaseSlot) == base && f.Read(SizeSlot) == 4096, "old protection canary or rounding wrong");
        check(f.Probe(base, 0) == Marker, "read-only lost readable data");
        f.Probe(base, 1, 1); f.Probe(base, 2, 2);
        // A trusted loader/test may initialize backing bytes; the guest cannot
        // write this RX code itself. mov eax,0x33445566; ret
        const uint8_t executable[]{0xB8, 0x66, 0x55, 0x44, 0x33, 0xC3};
        std::memcpy(static_cast<uint8_t*>(f.vm.RamData()) + base, executable, sizeof(executable));
        check(f.Protect(base, 4096, 0x20) == 0 && f.Probe(base, 2) == 0x33445566, "execute-read code failed");
        f.Probe(base, 1, 1);
        check(f.Protect(base, 4096, 1) == 0 && f.Query(base).state == M::Commit, "committed no-access state wrong");
        f.Probe(base, 0, 0); f.Probe(base, 2, 2);
        check(f.Protect(base, 4096, 4) == 0, "restore RW failed");
        check(f.Probe(base, 1) == Marker, "restored RW not usable");
        check(f.Allocate(base, 4096, M::Commit, 2) == 0 && f.Probe(base, 0) == Marker && f.Query(base).protect == 4, "recommit changed live page");
        check(f.Protect(base, 8192, 2) == M::NotCommitted && f.Query(base).protect == 4, "partly reserved range changed protection");
        check(f.Free(base + 17, 1, M::Decommit) == 0 && f.Read(BaseSlot) == base && f.Read(SizeSlot) == 4096, "decommit output wrong");
        f.Probe(base, 0, 0);
        check(f.Allocate(base, 4096, M::Commit, 4) == 0 && f.Probe(base, 0) == 0, "recommit did not erase old content");
        check(f.Free(base, 1, M::Release) == M::InvalidParameter && f.Probe(base, 1) == Marker, "invalid release freed live memory");
        check(f.Free(base, 0, M::Release) == 0 && f.Read(SizeSlot) == 8192, "full release output wrong");
        f.Probe(base, 0, 0); f.Probe(base, 2, 2);
        check(f.Allocate(0, 4096, M::Reserve | M::Commit, 4) == 0 && f.Read(BaseSlot) == base && f.Probe(base, 0) == 0, "released address not reused/zeroed");
        check(f.Allocate(0, UINT64_MAX, M::Reserve | M::Commit, 4) == M::InvalidParameter && f.Read(SizeSlot) == UINT64_MAX, "overflow changed output");
        check(f.Allocate(0, 4096, M::Reserve | M::Commit, 0x104) == M::InvalidProtection, "guard page success fabricated");
        f.memory.Write(BaseSlot, uint64_t(0)); f.memory.Write(SizeSlot, uint64_t(4096));
        check(f.Call("NtAllocateVirtualMemory", {0x1234, BaseSlot, 0, SizeSlot}, {0x3000, 4}) == M::InvalidHandle, "foreign process accepted");
        check(f.Call("NtAllocateVirtualMemoryEx", {UINT64_MAX, BaseSlot, SizeSlot, 0x3000}, {4, 0, 1}) == M::NotSupported, "extended parameter ignored");
        check(f.Call("NtAllocateVirtualMemoryEx", {UINT64_MAX, BaseSlot, SizeSlot, 0x3000}, {4, 0, 0}) == 0, "extended allocation ABI failed");
        check(f.Call("NtQueryVirtualMemory", {UINT64_MAX, base, 0, Info}, {47, ReturnLength}) == 0xC0000004 && f.Read(ReturnLength) == 48, "query buffer length handling wrong");
        check(f.Protect(base, 4096, 2) == 0, "parameter test protection failed");
        check(f.Call("NtQueryVirtualMemory", {UINT64_MAX, base, 0, base + 0x100}, {48, ReturnLength}) == M::AccessViolation, "syscall wrote a read-only output buffer");
        check(f.Call("NtAllocateVirtualMemory", {UINT64_MAX, UINT64_MAX - 3, 0, SizeSlot}, {0x3000, 4}) == M::AccessViolation, "overflowed ABI pointer accepted");
        check(f.Protect(base, 4096, 4) == 0, "parameter test RW restore failed");
        f.memory.Write(base + 0x100, base); f.memory.Write(base + 0x108, uint64_t(4096));
        check(f.Call("NtProtectVirtualMemory", {UINT64_MAX, base + 0x100, base + 0x108, 2}, {OldSlot}) == M::InvalidParameter && f.Query(base).protect == 4, "self-referential outputs changed page permissions");
        check(f.Call("NtUnmapViewOfSection", {UINT64_MAX, base, 0, 0}) == M::NotAllocated, "section unmap released private allocation");

        // Use the same address space: section views must coexist with, and not
        // overwrite, the private allocations above. Only one partition is needed.
        auto& section = f;
        check(section.Probe(base, 1) == Marker, "private memory unusable before section test");
        constexpr uint64_t sectionSlot = 0x6300, maximumSize = 0x6310;
        section.memory.Write(maximumSize, uint64_t(4096));
        check(section.Call("NtCreateSection", {sectionSlot, 0xF001F, 0, maximumSize}, {2, 0x8000000, 0}) == 0, "pagefile section creation failed");
        const auto handle = section.Read(sectionSlot);
        section.memory.Write(BaseSlot, uint64_t(0)); section.memory.Write(SizeSlot, uint64_t(0));
        check(section.Call("NtMapViewOfSection", {handle, UINT64_MAX, BaseSlot, 0}, {0, 0, SizeSlot, 2, 0, 2}) == 0, "section map failed");
        const auto view = section.Read(BaseSlot);
        check(view != base && section.Query(view).type == M::Mapped && section.Query(view).protect == 2 &&
            section.Query(base).type == M::Private && section.Probe(base, 0) == Marker, "section view collided with private memory or metadata wrong");
        check(section.Protect(view, 4096, 4) == M::SectionProtection && section.Query(view).protect == 2,
            "protection change exceeded the read-only section maximum");
        section.Probe(view, 1, 1);
        check(section.Call("NtClose", {handle, 0, 0, 0}) == 0 && section.Probe(view, 0) == 0 &&
            section.Query(view).type == M::Mapped, "closing section handle destroyed its live view");
        check(section.Free(view, 0, M::Release) == M::NotAllocated, "private release freed section");
        check(section.Call("NtUnmapViewOfSection", {UINT64_MAX, view + 8, 0, 0}) == 0, "interior view unmap failed");
        section.Probe(view, 0, 0);
        check(section.Allocate(0, 4096, 0x3000, 4) == 0 && section.Read(BaseSlot) == view, "view address not recovered");
        section.vm.FailMemoryMappings();
        check(!section.vm.Run() && section.vm.MemoryMappingsFailed(), "poisoned VM resumed execution");
        std::cout << "PASS: real ring-3 MiniNt memory syscalls, query/ABI canaries, reserve/decommit/release faults, read-only and NX enforcement, RX execution, section lifetime and failed-mapping stop\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
