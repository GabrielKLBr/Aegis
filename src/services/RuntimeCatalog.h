#pragma once
#include <filesystem>
#include <cstddef>
namespace av::services {
// Only for a NEW, inactive, administrator-protected installation slot whose
// engine files were copied from the verified publisher package. Never call on
// an active catalog or use a user-selected executable as the helper source.
size_t RefreshRuntimeHelpers(const std::filesystem::path& registry,
                             const std::filesystem::path& verifiedEngines);
}
