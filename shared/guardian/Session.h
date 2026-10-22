#pragma once
#include <stdint.h>
namespace guardian::session {
inline constexpr uint32_t MaxDurationMs = 7u * 24 * 60 * 60 * 1000;
inline constexpr uint64_t MaxElapsedNs = 10ull * 365 * 24 * 60 * 60 * 1000000000;
constexpr bool ValidDuration(uint32_t duration, uint32_t checkpoint) noexcept {
    return duration >= 100 && duration <= MaxDurationMs && checkpoint >= 100 && checkpoint <= 30000;
}
} // namespace guardian::session
