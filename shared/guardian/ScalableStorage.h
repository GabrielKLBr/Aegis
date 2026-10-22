#pragma once
#include "Abi.h"
namespace guardian::scalable {
inline constexpr uint64_t Magic = 0x32465341415647ull;
inline constexpr uint32_t Version = 2, ChunkBytes = 65536, MaxFiles = 32, Extents = 4096;
inline constexpr uint64_t DataStart = 2ull << 20, MaxCapacity = 8ull << 30,
                          MaxFileBytes = uint64_t(Extents) * ChunkBytes;
enum class Status : uint32_t { Ok, NotFound, Invalid, Exists, NoSpace, Corrupt, IoError, FileTooLarge };
enum class BlockOp : uint32_t { Read, Write, Flush, Discard, AllocationMap };
struct Node {
    uint64_t bytes = 0;
    uint32_t used = 0, reserved = 0;
    char name[32]{};
    uint32_t extents[Extents]{};
};
struct Header {
    uint64_t magic = 0, capacity = 0, quota = 0, sequence = 0, checksum = 0;
    uint32_t version = 0, bytes = 0;
    uint64_t reserved[2]{};
};
struct Metadata {
    Header header{};
    Node nodes[MaxFiles]{};
};
inline constexpr uint32_t SlotChunks = (sizeof(Metadata) + ChunkBytes - 1) / ChunkBytes;
struct Space {
    uint64_t capacity = 0, allocated = 0, free = 0, quota = 0, available = 0, physicalAvailable = 0;
};
using Block = Status (*)(void *, BlockOp, uint64_t, uint8_t *) noexcept;
using Available = uint64_t (*)(void *) noexcept;
static_assert(sizeof(Header) == 64 && SlotChunks * ChunkBytes * 2 < DataStart);
} // namespace guardian::scalable
