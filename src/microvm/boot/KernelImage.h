#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace av::microvm::boot {
class KernelImage {
public:
    // Keep the complete debug kernel below the separate bootstrap stack at 4 MiB.
    static constexpr uint64_t PreferredBase = 0x200000, MaxImageBytes = 0x180000, MaxFileBytes = 4ull << 20;
    struct Section { uint32_t rva, virtualBytes, rawOffset, rawBytes, mappedBytes; uint8_t access; };
    static KernelImage Parse(std::span<const uint8_t> bytes);
    static KernelImage Read(const std::filesystem::path& path);
    void Map(std::span<uint8_t> ram) const;
    bool SupportsStorageQuota() const noexcept;
    bool SupportsResources() const noexcept;
    bool SupportsEnvironment() const noexcept;
    bool SupportsTelemetry() const noexcept;
    bool SupportsAnalysisFixture() const noexcept;
    bool SupportsSystemClock() const noexcept;
    bool SupportsExternalGui() const noexcept;
    bool SupportsExternalPe() const noexcept;
    bool SupportsSecurity() const noexcept;
    bool SupportsDesktop() const noexcept;
    bool SupportsComponents() const noexcept;
    bool SupportsInternet() const noexcept;
    uint64_t Base() const { return PreferredBase; }
    uint64_t Size() const { return imageBytes_; }
    uint64_t Entry() const { return PreferredBase + entryRva_; }
    uint32_t HeaderBytes() const { return headerBytes_; }
    const std::vector<Section>& Sections() const { return sections_; }
    const std::vector<uint8_t>& FileBytes() const { return file_; }
private:
    uint32_t imageBytes_ = 0, entryRva_ = 0, headerBytes_ = 0;
    std::vector<uint8_t> file_;
    std::vector<Section> sections_;
};
}
