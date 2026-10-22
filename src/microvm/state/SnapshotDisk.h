#pragma once
#include "Files.h"
namespace av::microvm::state {
// Validate the sealed v1 container in bounded chunks and materialize only disk.
std::vector<uint8_t> SnapshotDisk(const std::filesystem::path&,uint32_t ramMiB,uint64_t imageHash,uint64_t scalableGrowth=0);
}
