#pragma once
#include <stdint.h>
namespace guardian::storage {
inline constexpr uint64_t TimesMagic=0x31454D49545647ull, UnixEpoch100ns=116444736000000000ull;
struct FileTimes {uint64_t creation=0,access=0,write=0;};
static_assert(sizeof(FileTimes)==24);
inline bool FileSecond(uint64_t value,uint32_t& seconds) noexcept {
    if(!value){seconds=0;return true;}
    if(value<=UnixEpoch100ns || (value-UnixEpoch100ns)%10000000)return false;
    const auto n=(value-UnixEpoch100ns)/10000000;if(n>UINT32_MAX)return false;
    seconds=static_cast<uint32_t>(n);return true;
}
inline uint64_t FileUtc(uint32_t seconds) noexcept {return seconds?UnixEpoch100ns+uint64_t(seconds)*10000000:0;}
}
