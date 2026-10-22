#pragma once
#include <stdint.h>
namespace guardian::pe {
// Shared by the loader and runtime unwinder, including unoptimized owned probes.
inline constexpr uint32_t MaxRuntimeFunctions = 256;
}
