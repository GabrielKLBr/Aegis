#pragma once
#include <cstdint>
namespace av::microvm {
// Private launch handshake, outside the RAM range mapped into the guest.
inline constexpr uint64_t MemoryHostMagic=0x47564D52414D3031ull,MemoryHostPage=4096;
struct MemoryHostHeader {uint64_t magic,bytes,address;};
}
