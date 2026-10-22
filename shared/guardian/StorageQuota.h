#pragma once
#include "Storage.h"
namespace guardian::storage {
inline constexpr uint64_t QuotaMagic=0x3141544F55515647ull,QuotaGpa=0x13C000;
// The reference filesystem uses one 512-byte allocation unit per node.
inline constexpr uint64_t VolumeCapacity=uint64_t(MaxNodes)*DataBytes;
struct QuotaInfo {uint64_t magic=QuotaMagic;uint32_t version=1,bytes=sizeof(QuotaInfo);uint64_t allocationLimit=VolumeCapacity,reserved[5]{};};
struct Space {uint64_t capacity=VolumeCapacity,free=0,available=0,allocationLimit=VolumeCapacity,allocated=0;};
struct BackingSpace {uint64_t limit=0,used=0,available=0,hostAvailable=0,hostReserve=0,allocated=0,aggregateLimit=0,aggregateAvailable=0;};
inline bool ValidQuota(const QuotaInfo& i) noexcept {
    if(i.magic!=QuotaMagic||i.version!=1||i.bytes!=sizeof(i)||i.allocationLimit<3*DataBytes||i.allocationLimit>VolumeCapacity||(i.allocationLimit%DataBytes))return false;
    for(auto n:i.reserved)if(n)return false;return true;
}
}
