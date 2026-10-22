#pragma once
#include "ProcessManager.h"
#include "guardian/Runtime.h"
#include "guardian/Storage.h"
namespace guardian::runtime {
// Only allocations created through VirtualAlloc can be freed/protected here.
// Loader mappings, TEBs and stacks keep their separate ownership.
class VirtualMemory {
public:
    void Reset() noexcept { for (auto& range : ranges_) range = {}; }
    uint32_t Execute(processes::Manager& processes, memory::Manager& memory,
                     uint32_t pid, Command& command) noexcept {
        auto& c = command;
        if (!processes.Get(pid)) return processes::Terminated;
        if (c.version!=Version || c.bytes!=sizeof(c) || c.status || c.transferred ||
            c.share || c.disposition || c.type || c.reserved)
            return storage::Invalid;
        if (c.op == Op::MemoryAllocate) {
            if (c.handle || !c.argument || c.argument > memory::ArenaBytes || c.length ||
                !memory::ValidProtection(c.access)) return storage::Invalid;
            if (c.flags != 0x3000) return storage::Unsupported;
            if (c.access!=memory::NoAccess && c.access!=memory::ReadOnly && c.access!=memory::ReadWrite)
                return storage::Unsupported;
            Range* slot = nullptr;
            for (auto& range : ranges_) if (!range.base) { slot = &range; break; }
            if (!slot) return storage::Quota;
            const auto bytes = (c.argument + 4095) & ~4095ull;
            uint64_t base = 0;
            auto status = processes.Reserve(pid, base, bytes, c.access);
            if (status) return status;
            status = processes.Commit(pid, base, bytes, c.access);
            if (status) { processes.Release(pid, base); return status; }
            *slot = {base, bytes}; c.handle = base;
            return storage::Success;
        }
        if (c.op == Op::MemoryQuery) {
            if (c.flags || c.access || c.argument || c.length != sizeof(memory::Information))
                return storage::Invalid;
            memory::Information info{};
            const auto status = memory.Query(processes.Get(pid)->space, c.handle, info);
            if (status != memory::Status::Success) return static_cast<uint32_t>(status);
            const auto* bytes = reinterpret_cast<const uint8_t*>(&info);
            for (uint32_t i = 0; i < sizeof(info); ++i) c.data[i] = bytes[i];
            c.transferred = sizeof(info); return storage::Success;
        }
        Range* range = nullptr;
        for (auto& candidate : ranges_) if (candidate.base && c.handle >= candidate.base &&
            c.handle - candidate.base < candidate.bytes) { range = &candidate; break; }
        if (!range) return static_cast<uint32_t>(memory::Status::NotAllocated);
        if (c.op == Op::MemoryFree) {
            if (c.flags != 0x8000) return storage::Unsupported;
            if (c.argument || c.access || c.length || c.handle != range->base) return storage::Invalid;
            const auto status = processes.Release(pid, range->base);
            if (!status) *range = {};
            return status;
        }
        if (c.op == Op::MemoryProtect) {
            if (c.flags || c.length || !c.argument || !memory::ValidProtection(c.access) ||
                c.argument > range->bytes - (c.handle - range->base)) return storage::Invalid;
            if (c.access!=memory::NoAccess && c.access!=memory::ReadOnly && c.access!=memory::ReadWrite)
                return storage::Unsupported;
            const auto base = c.handle & ~4095ull;
            const auto end = (c.handle + c.argument + 4095) & ~4095ull;
            uint32_t old = 0;
            const auto status = memory.ProtectRange(processes.Get(pid)->space, base, end - base, c.access, old);
            if (status == memory::Status::Success) c.transferred = old;
            return static_cast<uint32_t>(status);
        }
        return storage::Unsupported;
    }
private:
    struct Range { uint64_t base = 0, bytes = 0; } ranges_[8]{};
};
}
