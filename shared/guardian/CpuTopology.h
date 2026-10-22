#pragma once
#include "Abi.h"
namespace guardian::hardware {
// One socket, one thread per core, dense APIC IDs. The VMM creates every core.
inline void CpuTopology(uint32_t count, uint32_t id, uint32_t leaf, uint32_t sub, uint32_t &a, uint32_t &b,
                        uint32_t &c, uint32_t &d) noexcept {
    uint32_t shift = 0;
    while ((1u << shift) < count)
        ++shift;
    if (leaf == 1) {
        b = (b & 0xFFFFu) | (count << 16) | (id << 24);
        c |= 1u << 31;
        d = count > 1 ? d | (1u << 28) : d & ~(1u << 28);
    } else if (leaf == 0xB || leaf == 0x1F) {
        a = sub == 1 ? shift : 0;
        b = sub == 0 ? 1 : sub == 1 ? count : 0;
        c = (sub < 2 ? (sub + 1) << 8 : 0) | (sub & 255);
        d = id;
    } else if (leaf == 4 || leaf == 0x8000001D) {
        a = (a & 0x3FFFu) | ((count - 1) << 26);
    } else if (leaf == 0x80000008) {
        c = (c & ~0xF0FFu) | (count - 1) | (shift << 12);
    } else if (leaf == 0x8000001E) {
        a = id;
        b = id;
        c = 0;
        d = 0;
    }
}
} // namespace guardian::hardware
