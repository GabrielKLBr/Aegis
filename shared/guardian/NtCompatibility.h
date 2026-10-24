#pragma once
#include <stdint.h>
#include <stddef.h>

// Guardian's own versioned contract. This is not a Windows structure or syscall ABI.
namespace guardian::nt {
inline constexpr uint64_t Magic=0x3143544E564147ull, InfoGpa=0x1F1000;
inline constexpr uint32_t Version=1, Contract=1;
struct Target {uint32_t id,major,minor,build;};
inline constexpr Target Targets[]{{1,10,0,19045},{2,6,1,7601},{3,10,0,22631}};
inline constexpr Target Find(uint32_t id) noexcept {
    for(const auto& t:Targets)if(t.id==id)return t;
    return {};
}
inline constexpr bool Matches(Target t,uint32_t major,uint32_t minor,uint32_t build) noexcept {
    return t.id&&t.major==major&&t.minor==minor&&t.build==build;
}
struct Info {
    uint64_t magic=Magic,boot=0;
    uint32_t version=Version,bytes=sizeof(Info),profile=1,major=10,minor=0,build=19045,revision=0,contract=Contract;
    uint8_t manifestSha[32]{},kernelSha[32]{},applicationSha[32]{},runtimeSha[32]{};
};
static_assert(sizeof(Info)==176);
inline bool Nonzero(const uint8_t (&hash)[32]) noexcept {for(auto b:hash)if(b)return true;return false;}
inline bool Valid(const Info& p,uint64_t boot) noexcept {
    return p.magic==Magic&&p.boot==boot&&p.version==Version&&p.bytes==sizeof(p)&&p.contract==Contract&&
        Matches(Find(p.profile),p.major,p.minor,p.build)&&!p.revision&&
        Nonzero(p.manifestSha)&&Nonzero(p.kernelSha)&&Nonzero(p.runtimeSha);
}
enum class Manifest:uint32_t {Unaware=0,Windows81=1,Windows10=2};
struct OsVersion {uint32_t major,minor,build;};
// Documented GetVersionEx policy model; native per-build observations remain separate evidence.
inline constexpr OsVersion Reported(Target t,Manifest manifest) noexcept {
    if(t.id==2||manifest==Manifest::Windows10)return {t.major,t.minor,t.build};
    if(manifest==Manifest::Windows81)return {6,3,9600};
    return {6,2,9200};
}
struct Query {
    uint32_t version=Version,bytes=sizeof(Query),manifest=0,reserved=0;
    Target target{};OsVersion reported{};
};
static_assert(sizeof(Query)==44);
}
