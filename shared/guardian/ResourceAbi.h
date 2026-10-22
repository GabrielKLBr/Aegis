#pragma once
#include "ScalableStorage.h"
namespace guardian::resource {
inline constexpr uint64_t Magic = 0x32534552564147ull, BootGpa = 0x100000, RequestGpa = 0x110000,
                          DataGpa = 0x120000, ReportGpa = 0x131000;
inline constexpr uint16_t Port = 0xE8;
inline constexpr uint32_t Version = 2, ReportOperation = 8, SpaceOperation = 9, SmpCheckpointOperation = 10;
inline constexpr uint64_t CapabilityMagic = 0x324D504D53564147ull;
struct Boot {
    uint64_t magic = Magic, bootId = 0, ramBytes = 0, capacity = 0, quota = 0;
    uint32_t version = Version, bytes = sizeof(Boot), vcpus = 0, reserved = 0;
};
struct Request {
    uint64_t sequence = 0, offset = 0, buffer = DataGpa, available = 0;
    uint32_t operation = 0, bytes = scalable::ChunkBytes, status = uint32_t(scalable::Status::Invalid),
             reserved = 0;
};
struct Report {
    uint64_t bootId = 0, capacity = 0, fileBytes = 0, counter = 0;
    scalable::Space space{};
    uint32_t cpuid[4][12]{};
    uint32_t vcpus = 0, readyMask = 0, topologyMask = 0, doneMask = 0, checks = 0, status = 1, fpuMask = 0;
};
inline constexpr uint32_t Iterations = 50000, RequiredChecks = 63;
} // namespace guardian::resource
