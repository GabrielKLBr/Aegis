#pragma once
#include <stdint.h>
namespace guardian::frameworks {
// An immutable copy in the guest's writable namespace, backed by bytes in its
// bound package. It contains no host path, address, executable code or handle.
inline constexpr uint64_t FileMagic=0x314650434E4741ull;
struct FileReference {
    uint64_t magic=FileMagic;uint32_t version=1,bytes=sizeof(FileReference);
    uint64_t fileBytes=0,hash=0;char source[128]{};
};
static_assert(sizeof(FileReference)==160);
}
