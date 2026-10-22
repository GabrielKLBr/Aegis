#include "../src/microvm/host/GuestVirtualMemory.h"
#include <iostream>
#include <vector>

using namespace av::microvm;
using M = GuestVirtualMemory;
namespace {
void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
struct Backend : VirtualMemoryBackend {
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x100000, 0xCC);
    std::vector<uint8_t> permissions = std::vector<uint8_t>(0x100, 7);
    int failAfter = -1; size_t changes = 0; bool fatal = false;
    GuestMemory Memory() const override { return GuestMemory(const_cast<uint8_t*>(ram.data()), ram.size()); }
    bool SetAccess(uint64_t address, uint64_t bytes, uint8_t access) override {
        ++changes;
        check(!(address % 4096) && !(bytes % 4096) && Memory().Contains(address, bytes), "backend received invalid range");
        if (failAfter == 0) return false;
        if (failAfter > 0) --failAfter;
        for (size_t i = static_cast<size_t>(address / 4096); i < (address + bytes) / 4096; ++i) permissions[i] = access;
        return true;
    }
    void FailClosed() override { fatal = true; }
};
}
int main() {
    try {
        Backend backend; M memory; memory.Configure(0x10000, 0x90000);
        uint64_t address = 0, bytes = 0;
        check(memory.Allocate(backend, address, bytes, M::Commit | M::Reserve, 4) == M::InvalidParameter, "zero size accepted");
        bytes = UINT64_MAX;
        check(memory.Allocate(backend, address, bytes, M::Commit, 4) == M::InvalidParameter, "overflow accepted");
        bytes = 4096;
        check(memory.Allocate(backend, address, bytes, M::Reserve, 0x104) == M::InvalidProtection, "guard modifier fabricated");
        check(memory.Allocate(backend, address, bytes, M::Reserve, 4, 1) == M::NotSupported, "ZeroBits silently ignored");
        check(backend.changes == 0, "invalid request changed mappings");
        address = 0x10000; bytes = 0;
        check(memory.Free(backend, address, bytes, M::Release) == M::NotAllocated && backend.changes == 0, "free of unallocated region changed mappings");
        address = 0; bytes = 8193;
        check(memory.Allocate(backend, address, bytes, M::Reserve, 4) == 0 && address == 0x10000 && bytes == 12288, "reserve/rounding failed");
        const auto base = address;
        M::BasicInformation info{};
        check(memory.Query(backend, base, info) == 0 && info.state == M::Reserve && info.protect == 0 && info.regionSize == 12288, "reserved state wrong");
        check(backend.permissions[base / 4096] == 0 && !memory.Allows(base, 1, 1), "reserved region accessible");
        address = base + 1; bytes = 1;
        check(memory.Allocate(backend, address, bytes, M::Commit, 4) == 0 && address == base && bytes == 4096, "partial commit not page rounded");
        uint64_t value = 1; backend.Memory().Read(base, value); check(value == 0, "fresh commit not zeroed");
        backend.Memory().Write(base, uint64_t(0xABCD));
        check(memory.Allocate(backend, address, bytes, M::Commit, 2) == 0, "recommit failed");
        backend.Memory().Read(base, value); check(value == 0xABCD && backend.permissions[base / 4096] == 3, "recommit reset bytes or protection");
        address = base; bytes = 12288;
        check(memory.Allocate(backend, address, bytes, M::Commit, 2) == 0, "mixed recommit failed");
        check(backend.permissions[base / 4096] == 3 && backend.permissions[base / 4096 + 1] == 1, "mixed recommit changed existing permissions");
        check(memory.Query(backend, base + 4096, info) == 0 && info.protect == 2 && info.regionSize == 8192 && info.allocationProtect == 4, "query did not split protection runs");
        uint32_t old = 0xFFFFFFFF; address = base; bytes = 12288;
        check(memory.Protect(backend, address, bytes, 0x20, old) == 0 && old == 4, "RX protection failed");
        check(memory.Allows(base, 12288, 5) && !memory.Allows(base, 1, 2), "RX access check wrong");
        check(memory.Protect(backend, address, bytes, 1, old) == 0 && old == 0x20, "no-access protection failed");
        check(memory.Query(backend, base, info) == 0 && info.state == M::Commit && info.protect == 1 && !memory.Allows(base, 1, 1), "committed no-access confused with reserve");
        check(memory.Protect(backend, address, bytes, 4, old) == 0, "restore RW failed");
        address = base + 17; bytes = 1;
        check(memory.Free(backend, address, bytes, M::Decommit) == 0 && address == base && bytes == 4096, "decommit rounding failed");
        check(!memory.Allows(base, 1, 1) && memory.Allows(base + 4096, 1, 2), "decommit changed wrong pages");
        backend.Memory().Write(base + 4096, uint64_t(0xFEED));
        check(memory.Allocate(backend, address, bytes, M::Commit, 4) == 0, "recommit after decommit failed");
        backend.Memory().Read(base, value); check(value == 0, "recommit disclosed old bytes");
        backend.Memory().Read(base + 4096, value); check(value == 0xFEED, "recommit zeroed another page");
        address = base; bytes = 0;
        check(memory.Free(backend, address, bytes, M::Decommit) == 0 && bytes == 12288, "zero-length full decommit failed");
        old = 123; address = base; bytes = 4096;
        check(memory.Protect(backend, address, bytes, 2, old) == M::NotCommitted && old == 123, "protect of reserved page fabricated success");
        address = base; bytes = 1;
        check(memory.Free(backend, address, bytes, M::Release) == M::InvalidParameter && bytes == 1, "release accepted nonzero size");
        address = base; bytes = 0;
        check(memory.Free(backend, address, bytes, M::Release) == 0 && bytes == 12288 && address == base, "release output wrong");
        check(memory.Query(backend, base, info) == 0 && info.state == 0x10000 && info.type == 0, "released state wrong");
        address = 0; bytes = 4096;
        check(memory.Allocate(backend, address, bytes, M::Commit, 4) == 0 && address == base, "release did not make address reusable");
        address = 0; bytes = 4096;
        check(memory.Allocate(backend, address, bytes, M::Reserve | M::TopDown, 4) == 0 && address == 0x80000, "top-down placement wrong");
        address = 0x31234; bytes = 4096;
        check(memory.Allocate(backend, address, bytes, M::Reserve, 4) == 0 && address == 0x30000 && bytes == 12288, "fixed reservation rounding wrong");
        address = 0x32000; bytes = 4096;
        check(memory.Allocate(backend, address, bytes, M::Reserve, 4) == M::Conflict && address == 0x32000, "overlap changed output");
        check(!memory.Allows(UINT64_MAX - 1, 8, 1), "logical access check wrapped");
        Backend another; M::BasicInformation unchanged{};
        check(memory.Query(another, base, unchanged) == M::InvalidParameter, "arena moved to another VM");

        Backend views; M vm; vm.Configure(0x10000, 0x90000); address = 0; bytes = 4096;
        check(vm.Allocate(views, address, bytes, M::Reserve | M::Commit, 2, 0, M::Mapped) == 0, "view allocation failed");
        const auto view = address;
        check(vm.Query(views, view, info) == 0 && info.type == M::Mapped, "view type not recorded");
        old = 123;
        check(vm.Protect(views, address, bytes, 4, old) == M::SectionProtection && old == 123 &&
            vm.Query(views, view, info) == 0 && info.protect == 2, "protect exceeded the section maximum");
        bytes = 0;
        check(vm.Free(views, address, bytes, M::Release) == M::NotAllocated, "private free released a section view");
        check(vm.UnmapView(views, view + 7) == 0 && vm.UnmapView(views, view) == M::NotAllocated, "view unmap not tracked");
        address = 0; bytes = 4096;
        check(vm.Allocate(views, address, bytes, M::Commit, 4) == 0 && address == view, "view address not recovered");
        address = 0; bytes = 4096;
        check(vm.Allocate(views, address, bytes, M::Reserve | M::Commit, 2, 0, M::Mapped, 4) == 0 &&
            vm.Protect(views, address, bytes, 4, old) == 0 && old == 2, "view could not use the section's allowed write access");

        Backend failing; M poisoned; poisoned.Configure(0x10000, 0x90000);
        address = 0; bytes = 4096; failing.failAfter = 1; // initial unmap succeeds; commit map fails
        check(poisoned.Allocate(failing, address, bytes, M::Reserve | M::Commit, 4) == M::Unsuccessful,
            "mapping failure reported success");
        check(failing.fatal && poisoned.Faulted() && address == 0 && bytes == 4096, "mapping failure did not stop VM/preserve outputs");
        check(poisoned.Allocate(failing, address, bytes, M::Commit, 4) == M::Unsuccessful, "poisoned VM allowed another allocation");
        std::cout << "PASS: reserve/commit/decommit/release/protect/query, rounding, reuse, zeroing, mixed pages, section ownership and fail-closed backend errors\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
