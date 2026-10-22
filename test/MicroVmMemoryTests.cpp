#include "../src/microvm/host/GuestHeap.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

using av::microvm::GuestHeap;
using av::microvm::GuestMemory;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool failed = false; try { f(); } catch (const std::invalid_argument&) { failed = true; }
    check(failed, "invalid heap configuration accepted");
}
}
int main() {
    try {
        std::vector<uint8_t> ram(0x40000, 0xA5);
        GuestMemory memory(ram.data(), ram.size());
        check(!memory.Contains(UINT64_MAX, 8), "overflowed guest pointer accepted");
        check(!memory.Contains(1, UINT64_MAX), "overflowed length accepted");
        uint64_t unchanged = 123;
        check(!memory.Read(UINT64_MAX - 3, unchanged) && unchanged == 123, "invalid read changed output");
        const auto before = ram;
        check(!memory.Write(UINT64_MAX - 3, unchanged) && ram == before, "invalid write changed RAM");
        check(!memory.Zero(ram.size() - 1, 2) && ram == before, "out-of-bounds zero changed RAM");
        check(!memory.Copy(0, UINT64_MAX, 8), "out-of-bounds copy accepted");
        check(memory.Write(ram.size() - 8, unchanged), "last valid word rejected");

        GuestHeap heap;
        check(heap.Handle(memory) == 0, "unconfigured heap fabricated a handle");
        rejects([&] { heap.Configure(UINT64_MAX - 15, 16); });
        rejects([&] { heap.Configure(0x1001, 0x30000); });
        heap.Configure(0x10000, 0x30000);
        const auto h = heap.Handle(memory);
        check(h == 0x10000, "configured handle mismatch");
        check(heap.Allocate(memory, h + 16, 0, 16) == 0, "wrong heap handle accepted");
        check(heap.Allocate(memory, h, 4, 16) == 0, "unsupported exception flag accepted");
        check(heap.Allocate(memory, h, 0, UINT64_MAX) == 0, "size rounding overflow accepted");
        const auto p = heap.Allocate(memory, h, 8, 31);
        const auto q = heap.Allocate(memory, h, 0, 64);
        check(p && q && !(p & 15) && q >= p + 31, "allocation overlaps or is unaligned");
        check(std::all_of(ram.begin() + p, ram.begin() + p + 31, [](uint8_t b) { return b == 0; }), "new block not zeroed");
        std::fill_n(ram.begin() + p, 31, uint8_t(0x5A));
        check(heap.Reallocate(memory, h, 0x10, p, 128) == 0, "in-place-only moved a block");
        check(heap.Size(memory, h, 0, p) == 31 && ram[p] == 0x5A, "failed resize damaged original block");
        check(heap.Reallocate(memory, h, 8, p, UINT64_MAX) == 0, "reallocation overflow accepted");
        const auto moved = heap.Reallocate(memory, h, 8, p, 128);
        check(moved && moved != p, "blocked block did not relocate");
        check(std::all_of(ram.begin() + moved, ram.begin() + moved + 31, [](uint8_t b) { return b == 0x5A; }), "reallocation lost data");
        check(std::all_of(ram.begin() + moved + 31, ram.begin() + moved + 128, [](uint8_t b) { return b == 0; }), "grown tail not zeroed");
        check(!heap.Free(memory, h, 0, p) && heap.Size(memory, h, 0, p) == GuestHeap::InvalidSize, "old moved allocation still live");
        check(!heap.Free(memory, h, 0, moved + 1), "interior pointer freed");
        check(heap.Free(memory, h, 0, moved) && !heap.Free(memory, h, 0, moved), "double free not rejected");
        check(heap.Free(memory, h, 0, q), "valid free failed");
        const auto merged = heap.Allocate(memory, h, 0, 0x1F000);
        check(merged == 0x11000, "freed segments were not coalesced");
        check(heap.Allocate(memory, h, 0, 1) == 0, "exhausted heap allocated extra memory");
        check(heap.Reallocate(memory, h, 0, merged, 0x20000) == 0 && heap.Size(memory, h, 0, merged) == 0x1F000, "out-of-memory resize lost original");
        check(heap.Reallocate(memory, h, 0, merged, 7) == merged, "shrink failed");
        std::fill_n(ram.begin() + merged, 16, uint8_t(0x44));
        check(heap.Reallocate(memory, h, 8, merged, 15) == merged, "growth within aligned span failed");
        check(ram[merged + 6] == 0x44 && ram[merged + 7] == 0 && ram[merged + 14] == 0, "logical grown tail was not zeroed");
        check(heap.Reallocate(memory, h, 0x18, merged, 4096) == merged, "adjacent free segment not used in place");
        check(heap.Free(memory, h, 0, merged), "resized allocation not released");
        const auto z1 = heap.Allocate(memory, h, 0, 0), z2 = heap.Allocate(memory, h, 0, 0);
        check(z1 && z2 && z1 != z2 && heap.Size(memory, h, 0, z1) == 0, "zero-sized allocation not distinct");
        check(heap.Free(memory, h, 0, z1) && heap.Free(memory, h, 0, z2), "zero-sized allocation not released");
        std::vector<uint8_t> other(ram.size());
        check(heap.Allocate(GuestMemory(other.data(), other.size()), h, 0, 16) == 0, "heap moved to another VM without reset");
        GuestHeap outside; outside.Configure(0x40000, 0x50000);
        check(outside.Handle(memory) == 0, "heap outside guest RAM accepted");

        // Repeated arbitrary-sized allocations/free/reallocations validate data
        // and non-overlap against live blocks, not allocator implementation details.
        struct Live { uint64_t p, size; uint8_t value; };
        std::vector<Live> live;
        std::mt19937 random(0xAE615);
        for (unsigned step = 0; step < 2500; ++step) {
            if (live.empty() || (live.size() < 40 && random() % 3 == 0)) {
                const uint64_t size = random() % 1024 + 1;
                const auto pointer = heap.Allocate(memory, h, 0, size);
                check(pointer != 0, "bounded stress allocation failed");
                const auto value = static_cast<uint8_t>(random());
                std::fill_n(ram.begin() + pointer, static_cast<size_t>(size), value);
                live.push_back({pointer, size, value});
            } else {
                const auto index = random() % live.size();
                auto& block = live[index];
                if (random() & 1) {
                    const uint64_t size = random() % 1024 + 1;
                    const auto pointer = heap.Reallocate(memory, h, 8, block.p, size);
                    check(pointer != 0, "bounded stress resize failed");
                    for (uint64_t i = 0; i < (std::min)(size, block.size); ++i) check(ram[pointer + i] == block.value, "stress resize corrupted prefix");
                    for (uint64_t i = block.size; i < size; ++i) check(ram[pointer + i] == 0, "stress resize did not zero tail");
                    block.p = pointer; block.size = size;
                    std::fill_n(ram.begin() + pointer, static_cast<size_t>(size), block.value);
                } else {
                    check(heap.Free(memory, h, 0, block.p), "stress free failed");
                    live.erase(live.begin() + index);
                }
            }
            for (size_t i = 0; i < live.size(); ++i) {
                const auto& a = live[i];
                check(a.p >= 0x11000 && a.p + a.size <= 0x30000 && !(a.p & 15), "stress block outside arena");
                check(heap.Size(memory, h, 0, a.p) == a.size, "stress size mismatch");
                for (uint64_t byte = 0; byte < a.size; ++byte) check(ram[a.p + byte] == a.value, "another allocation overwrote live data");
                for (size_t j = 0; j < i; ++j) check(a.p + a.size <= live[j].p || live[j].p + live[j].size <= a.p, "live blocks overlap");
            }
        }
        std::cout << "PASS: bounded guest access, ownership, zeroing, release/reuse/coalescing, resize failure preservation, in-place-only and 2500 mixed operations\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
