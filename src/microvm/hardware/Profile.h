#pragma once
#include "guardian/Hardware.h"
#include "../state/Files.h"
#include <array>
namespace av::microvm::hardware {
struct Profile {
    std::string nt = "win11";
    std::array<std::string, guardian::hardware::Count> models{
        "Host x64 CPU", "Guardian software graphics", "Guardian virtual block disk",
        "Guardian boot firmware", "Guardian virtual board"};
    std::string identity;
};
void Validate(const Profile&);
Profile HostReference();
std::vector<uint8_t> Encode(const Profile&);
Profile Decode(std::span<const uint8_t>);
guardian::hardware::Info Inventory(const Profile&, uint64_t ramBytes, const std::string& contract);
std::string Json(const guardian::hardware::Info&);
// Bundles are inert data. Import never loads a Windows driver on either side.
std::vector<uint8_t> ImportDriver(const Profile&, const std::filesystem::path&, const std::string& nt,
                                  const std::string& device);
std::string DriverJson(std::span<const uint8_t>, const Profile&);
}
