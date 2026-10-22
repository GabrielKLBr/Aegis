#pragma once
#include <stdint.h>
#include <stddef.h>
namespace guardian::driver {
inline constexpr uint64_t Magic=0x3144565244415647ull, BootGpa=0x139000, RequestGpa=0x13A000, ReportGpa=0x13B000;
inline constexpr uint64_t Arena=0x1A0000, ArenaEnd=0x1E0000, SourceGpa=0x800000, ApiGpa=0x820000;
inline constexpr uint32_t Version=1, MaxFile=0x20000;
inline constexpr uint16_t Port=0xF4, DevicePort=0xF5;
inline constexpr const char* HardwareId="ROOT\\GUARDIAN_PROBE";
enum class Op:uint32_t {Protect=1,Report=2};
struct Boot {uint64_t magic;uint32_t version,bytes;uint64_t bootId,packageHash;uint32_t imageBytes,apiBytes;uint64_t reserved[3];char service[64];};
struct Request {uint64_t magic;uint32_t version,bytes;uint64_t sequence;Op op;uint32_t access;uint64_t gpa,span;uint32_t status,reserved;};
struct Report {uint64_t magic;uint32_t version,bytes;uint64_t bootId,packageHash;uint32_t installed,initialized,functional,unloaded,error,operations;uint64_t apiBefore,apiAfter;};
inline uint32_t Exchange(uint32_t value) noexcept {value^=0x6A1C12A0u;return (value<<7)|(value>>25);}
static_assert(sizeof(Boot)==128&&sizeof(Request)==56&&sizeof(Report)==72);
}
