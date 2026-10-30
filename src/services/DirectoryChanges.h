#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace av::services {
struct DirectoryBatch {
    bool rescanRequired = false;
    std::vector<std::filesystem::path> changed;
};
DirectoryBatch DecodeDirectoryChanges(const std::filesystem::path &, std::span<const uint8_t>);
} // namespace av::services
