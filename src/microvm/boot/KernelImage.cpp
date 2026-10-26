#include "KernelImage.h"
#include "guardian/ResourceAbi.h"
#include "guardian/Abi.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace av::microvm::boot {
namespace {
[[noreturn]] void Bad(const char* reason) { throw std::runtime_error(reason); }
bool Range(uint64_t offset, uint64_t bytes, uint64_t limit) { return offset <= limit && bytes <= limit - offset; }
template<class T> T Field(std::span<const uint8_t> bytes, uint64_t offset) {
    if (!Range(offset, sizeof(T), bytes.size())) Bad("Truncated kernel image field");
    T result{}; std::memcpy(&result, bytes.data() + static_cast<size_t>(offset), sizeof(T)); return result;
}
uint32_t Page(uint32_t bytes) { return (bytes + 4095u) & ~4095u; }
bool Overlap(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) { return an && bn && a < b + bn && b < a + an; }
}
KernelImage KernelImage::Parse(std::span<const uint8_t> bytes) {
    if (bytes.size() < 64 || bytes.size() > MaxFileBytes || Field<uint16_t>(bytes, 0) != 0x5A4D) Bad("Invalid kernel DOS header or file size");
    const uint64_t pe = Field<uint32_t>(bytes, 0x3C);
    if (pe < 64 || !Range(pe, 24, bytes.size()) || Field<uint32_t>(bytes, pe) != 0x4550) Bad("Invalid kernel PE header");
    const auto count = Field<uint16_t>(bytes, pe + 6), flags = Field<uint16_t>(bytes, pe + 22);
    if (Field<uint16_t>(bytes, pe + 4) != guardian::abi::X64 || !count || count > 16 ||
        (flags & 0x2000) || !(flags & 2) || !(flags & 1) || Field<uint16_t>(bytes, pe + 20) != 240 ||
        Field<uint32_t>(bytes, pe + 12) || Field<uint32_t>(bytes, pe + 16)) Bad("Unsupported kernel COFF format");
    const uint64_t optional = pe + 24, table = optional + 240;
    if (!Range(optional, 240 + uint64_t(count) * 40, bytes.size()) || Field<uint16_t>(bytes, optional) != 0x20B ||
        Field<uint64_t>(bytes, optional + 24) != PreferredBase || Field<uint32_t>(bytes, optional + 32) != 4096 ||
        Field<uint32_t>(bytes, optional + 36) != 512 || Field<uint16_t>(bytes, optional + 68) != 1 ||
        Field<uint32_t>(bytes, optional + 108) != 16) Bad("Unsupported kernel optional header");
    KernelImage image;
    image.imageBytes_ = Field<uint32_t>(bytes, optional + 56);
    image.headerBytes_ = Field<uint32_t>(bytes, optional + 60);
    image.entryRva_ = Field<uint32_t>(bytes, optional + 16);
    if (!image.imageBytes_ || image.imageBytes_ > MaxImageBytes || image.imageBytes_ % 4096 ||
        !image.headerBytes_ || image.headerBytes_ % 512 || image.headerBytes_ > 4096 ||
        image.headerBytes_ < table + uint64_t(count) * 40 || image.headerBytes_ > bytes.size() ||
        image.headerBytes_ >= image.imageBytes_) Bad("Invalid kernel image/header extent");
    bool executableEntry = false;
    uint64_t fileEnd = image.headerBytes_;
    for (uint16_t i = 0; i < count; ++i) {
        const uint64_t s = table + uint64_t(i) * 40;
        Section section{Field<uint32_t>(bytes, s + 12), Field<uint32_t>(bytes, s + 8),
            Field<uint32_t>(bytes, s + 20), Field<uint32_t>(bytes, s + 16), 0, 0};
        const auto attributes = Field<uint32_t>(bytes, s + 36);
        if (!section.virtualBytes || section.virtualBytes > MaxImageBytes || section.rawBytes > MaxImageBytes ||
            section.rva < Page(image.headerBytes_) || section.rva % 4096 || section.rawBytes % 512 ||
            (section.rawBytes && (section.rawOffset < image.headerBytes_ || section.rawOffset % 512 ||
                                  !Range(section.rawOffset, section.rawBytes, bytes.size()))) ||
            (!section.rawBytes && section.rawOffset)) Bad("Invalid kernel section range");
        section.mappedBytes = Page((std::max)(section.virtualBytes, section.rawBytes));
        if (!Range(section.rva, section.mappedBytes, image.imageBytes_)) Bad("Kernel section exceeds image");
        section.access = static_cast<uint8_t>(((attributes & 0x40000000) ? 1 : 0) |
            ((attributes & 0x80000000) ? 2 : 0) | ((attributes & 0x20000000) ? 4 : 0));
        if (!(section.access & 1) || ((section.access & 6) == 6)) Bad("Kernel section violates readable/W^X policy");
        for (const auto& previous : image.sections_)
            if (Overlap(section.rva, section.mappedBytes, previous.rva, previous.mappedBytes) ||
                Overlap(section.rawOffset, section.rawBytes, previous.rawOffset, previous.rawBytes)) Bad("Overlapping kernel sections");
        if ((section.access & 4) && image.entryRva_ >= section.rva &&
            uint64_t(image.entryRva_) - section.rva < section.virtualBytes) executableEntry = true;
        if (section.rawBytes) fileEnd = (std::max)(fileEnd, uint64_t(section.rawOffset) + section.rawBytes);
        image.sections_.push_back(section);
    }
    if (!executableEntry || fileEnd != bytes.size()) Bad("Invalid kernel entry or trailing data");
    for (unsigned i = 0; i < 16; ++i) {
        const auto rva = Field<uint32_t>(bytes, optional + 112 + i * 8), size = Field<uint32_t>(bytes, optional + 116 + i * 8);
        if (!rva && !size) continue;
        if ((i != 3 && i != 6) || !rva || !size || !Range(rva, size, image.imageBytes_)) Bad("Unsupported kernel data directory");
        bool covered = false;
        for (const auto& section : image.sections_)
            if (rva >= section.rva && uint64_t(rva) - section.rva <= section.virtualBytes &&
                size <= section.virtualBytes - (rva - section.rva)) covered = true;
        if (!covered) Bad("Kernel directory is not in a mapped section");
    }
    image.file_.assign(bytes.begin(), bytes.end()); return image;
}
KernelImage KernelImage::Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) Bad("Cannot open kernel image");
    const auto size = input.tellg();
    if (size < 64 || static_cast<uint64_t>(size) > MaxFileBytes) Bad("Kernel file size outside limits");
    std::vector<uint8_t> bytes(static_cast<size_t>(size)); input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) Bad("Cannot read complete kernel image");
    return Parse(bytes);
}
void KernelImage::Map(std::span<uint8_t> ram) const {
    if (!imageBytes_ || !Range(Base(), imageBytes_, ram.size())) Bad("Kernel image does not fit backing RAM");
    auto* destination = ram.data() + Base();
    std::fill_n(destination, imageBytes_, uint8_t(0)); std::memcpy(destination, file_.data(), headerBytes_);
    for (const auto& section : sections_) if (section.rawBytes)
        std::memcpy(destination + section.rva, file_.data() + section.rawOffset, section.rawBytes);
}
}

namespace av::microvm::boot {
bool KernelImage::SupportsStorageQuota() const noexcept {
    constexpr char tag[]="GuardianStorageQuota/1/512/32";
    return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();
}
bool KernelImage::SupportsEnvironment() const noexcept {
    constexpr char tag[]="GuardianEnvironment/1/synthetic";
    return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();
}
bool KernelImage::SupportsSystemClock() const noexcept {
    constexpr char tag[]="GuardianSystemClock/1/real";
    return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();
}
bool KernelImage::SupportsExternalGui() const noexcept {
    constexpr char tag[]="GuardianExternalGui/1/bounded";
    return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();
}
bool KernelImage::SupportsExternalPe() const noexcept {
    static constexpr char capability[]="GuardianExternalPe/1/volume";
    return std::search(file_.begin(),file_.end(),std::begin(capability),std::end(capability)-1)!=file_.end();
}
bool KernelImage::SupportsDesktop() const noexcept {constexpr char tag[]="GuardianDesktop/1/guest";return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();}
bool KernelImage::SupportsComponents() const noexcept {
    constexpr char tag[]="GuardianComponent/1/guest";
    return std::search(file_.begin(),file_.end(),std::begin(tag),std::end(tag)-1)!=file_.end();
}
bool KernelImage::SupportsNativeGui() const noexcept {constexpr char tag[]="GuardianNativeGui/1/guest";return std::search(file_.begin(),file_.end(),std::begin(tag),std::end(tag)-1)!=file_.end();}
bool KernelImage::SupportsWindowBridge() const noexcept {constexpr char tag[]="GuardianWindowBridge/1/guest";return std::search(file_.begin(),file_.end(),std::begin(tag),std::end(tag)-1)!=file_.end();}
bool KernelImage::SupportsInternet() const noexcept {
    constexpr char tag[]="GuardianInternet/1/guest";
    return std::search(file_.begin(),file_.end(),std::begin(tag),std::end(tag)-1)!=file_.end();
}
bool KernelImage::SupportsSecurity() const noexcept {
    constexpr char tag[]="GuardianSecurity/1/guest";
    return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();
}
}

namespace av::microvm::boot {
bool KernelImage::SupportsResources() const noexcept {
    const uint64_t marker[]{guardian::resource::CapabilityMagic,guardian::resource::Version,guardian::scalable::MaxCapacity,4};
    for(size_t at=0;at+sizeof(marker)<=file_.size();++at)if(std::memcmp(file_.data()+at,marker,sizeof(marker))==0)return true;
    return false;
}
}

namespace av::microvm::boot {
bool KernelImage::SupportsTelemetry() const noexcept {constexpr char tag[]="GuardianTelemetry/1/bounded";return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();}
bool KernelImage::SupportsAnalysisFixture() const noexcept {constexpr char tag[]="GuardianAnalysis/1/owned";return std::search(file_.begin(),file_.end(),tag,tag+sizeof(tag))!=file_.end();}
}

namespace av::microvm::boot {bool KernelImage::SupportsNativeUser32() const noexcept {constexpr char tag[]="GuardianNativeUser32/2/guest";return std::search(file_.begin(),file_.end(),std::begin(tag),std::end(tag)-1)!=file_.end();}}
