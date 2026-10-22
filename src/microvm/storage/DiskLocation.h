#pragma once
#include "../state/Files.h"
namespace av::microvm::storage {
void SafeDiskParent(const std::filesystem::path &);
void CreateLocation(const std::filesystem::path &, const std::string &owner);
void ValidateLocation(const std::filesystem::path &, const std::string &owner);
void RemoveGeneration(const std::filesystem::path &, const std::string &owner);
void ValidateLocationRemoval(const std::filesystem::path &, const std::string &owner);
void RemoveLocation(const std::filesystem::path &, const std::string &owner);
} // namespace av::microvm::storage
