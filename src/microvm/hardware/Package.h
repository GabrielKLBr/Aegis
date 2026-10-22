#pragma once
#include "Profile.h"
#include "DriverBackend.h"
namespace av::microvm::hardware {
inline constexpr uint64_t MaxPackageBytes=21ull<<20;
struct DriverPackage {
    std::string nt,device,hardwareId,service,binary,catalog;
    uint32_t minimumMajor=0,minimumMinor=0,machine=0;
    std::vector<std::string> imports,requirements;
    std::vector<uint8_t> image,inf,catalogBytes;
    std::string Compatibility(const Profile&) const;
    std::vector<uint8_t> Encode() const;
    static DriverPackage Decode(std::span<const uint8_t>);
};
DriverPackage ReadPackage(const std::filesystem::path&,const std::string& nt,const std::string& device);
bool IsPackage(std::span<const uint8_t>);
std::string PackageJson(const DriverPackage&,const Profile&,const guardian::driver::Report* report=nullptr);
}
