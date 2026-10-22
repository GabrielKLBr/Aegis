#pragma once
#include <stdint.h>
#include <stddef.h>

// Optional, read-only boot extension. Models are references, not a promise to
// emulate the instruction set, firmware or driver ABI of the named product.
namespace guardian::hardware {
inline constexpr uint64_t Magic = 0x31445748415647ull, InfoGpa = 0x138000;
inline constexpr uint32_t Version = 1, Count = 5;
enum class Kind : uint32_t { Cpu = 1, Graphics, Disk, Bios, Board };
enum class Support : uint32_t { MetadataOnly = 0, Partial = 1, Functional = 2 };
struct Device {
    Kind kind;
    Support support;
    char requestedModel[96], effectiveModel[96], syntheticId[40];
};
struct Info {
    uint64_t magic;
    uint32_t version, bytes, ntMajor, ntMinor, ntBuild, count;
    uint64_t ramBytes;
    uint32_t vcpus, reserved;
    Device devices[Count];
};
static_assert(sizeof(Device) == 240 && sizeof(Info) == 1248);
static_assert(sizeof(Info) <= 4096);
template<size_t N> inline bool Text(const char (&s)[N]) noexcept {
    bool ended = false;
    for (size_t i = 0; i < N; ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (!c) ended = true;
        else if (ended || c < 32 || c > 126) return false;
    }
    return ended && s[0] != 0;
}
inline bool Valid(const Info& i, uint64_t ram) noexcept {
    if (i.magic != Magic || i.version != Version || i.bytes != sizeof(Info) ||
        i.count != Count || i.ramBytes != ram || i.vcpus != 1 || i.reserved) return false;
    // These are requested compatibility targets, not implemented NT versions.
    const bool nt = (i.ntMajor == 5 && i.ntMinor == 1 && i.ntBuild == 2600) ||
        (i.ntMajor == 6 && ((i.ntMinor == 0 && i.ntBuild == 6002) ||
        (i.ntMinor == 1 && i.ntBuild == 7601) || (i.ntMinor == 2 && i.ntBuild == 9200) ||
        (i.ntMinor == 3 && i.ntBuild == 9600))) ||
        (i.ntMajor == 10 && i.ntMinor == 0 && (i.ntBuild == 19045 || i.ntBuild == 22631));
    if (!nt) return false;
    for (uint32_t n = 0; n < Count; ++n) {
        const auto& d = i.devices[n];
        if (d.kind != static_cast<Kind>(n + 1) ||
            static_cast<uint32_t>(d.support) > 2 || !Text(d.requestedModel) ||
            !Text(d.effectiveModel) || !Text(d.syntheticId)) return false;
        if (d.syntheticId[0] != 'g' || d.syntheticId[1] != 'v' || d.syntheticId[2] != '-') return false;
        for (uint32_t p = 0; p < n; ++p) {
            bool same = true;
            for (size_t c = 0; c < sizeof(d.syntheticId); ++c)
                if (d.syntheticId[c] != i.devices[p].syntheticId[c]) same = false;
            if (same) return false;
        }
    }
    return true;
}
inline uint64_t Digest(const Info& i) noexcept {
    uint64_t hash=14695981039346656037ull;
    const auto* bytes=reinterpret_cast<const uint8_t*>(&i);
    for(size_t n=0;n<sizeof(i);++n){hash^=bytes[n];hash*=1099511628211ull;}
    return hash;
}
}
