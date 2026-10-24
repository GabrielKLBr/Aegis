#pragma once
#include "Abi.h"
namespace guardian::externalgui {
inline constexpr uint64_t Magic=0x31495547455047ull,InfoGpa=0x1F0000,SourceGpa=0xA00000;
inline constexpr uint32_t Version=1,MaxFile=0x20000,Checks=1;
struct Info {uint64_t magic,boot;uint32_t version,bytes,fileBytes,reserved;uint64_t hash;};
inline uint64_t Hash(const uint8_t* data,uint32_t n) noexcept {
    uint64_t h=14695981039346656037ull;for(uint32_t i=0;i<n;++i){h^=data[i];h*=1099511628211ull;}return h;
}
inline bool Valid(const Info& p,uint64_t boot) noexcept {
    return p.magic==Magic&&p.boot==boot&&p.version==Version&&p.bytes==sizeof(p)&&p.fileBytes>=64&&p.fileBytes<=MaxFile&&!p.reserved;
}
static_assert(sizeof(Info)==40);
}
