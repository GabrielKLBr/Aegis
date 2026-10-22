#pragma once
#include "Abi.h"

namespace guardian::abi {
constexpr bool Range(uint64_t gpa, uint64_t bytes, uint64_t limit) {
    return bytes && gpa <= limit && bytes <= limit - gpa;
}
constexpr bool Overlap(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    // Caller first validates both ranges against the same finite RAM limit.
    return a < b + bn && b < a + an;
}
constexpr bool Aligned(uint64_t value) { return value % PageBytes == 0; }

inline Result ValidateBoot(const BootInfo& b, uint64_t selfGpa, uint64_t actualRamBytes) noexcept {
    if (b.magic != BootMagic) return Result::BadMagic;
    if (b.major != Major || b.minor != Minor || b.machine != X64) return Result::UnsupportedVersion;
    if (b.bytes != sizeof(BootInfo) || b.memoryMapEntryBytes != sizeof(MemoryRegion) ||
        b.deviceEntryBytes != sizeof(Device)) return Result::InvalidSize;
    if (b.flags || b.reserved[0] || b.reserved[1] || b.reserved[2] || b.reserved[3]) return Result::ReservedBits;
    if (b.ramBytes != actualRamBytes || b.ramBytes < MinRamBytes || b.ramBytes > MaxRamBytes ||
        !Aligned(b.ramBytes) || b.vcpuCount != 1 || !b.bootId) return Result::InvalidRange;
    if (!b.memoryMapCount || b.memoryMapCount > MaxRegions || !b.deviceCount || b.deviceCount > MaxDevices ||
        b.messageBytes != MaxMessageBytes) return Result::InvalidSize;
    const uint64_t starts[]{selfGpa, b.memoryMapGpa, b.devicesGpa, b.messageGpa, b.imageGpa, b.stackGpa};
    const uint64_t sizes[]{sizeof(BootInfo), uint64_t(b.memoryMapCount) * sizeof(MemoryRegion),
        uint64_t(b.deviceCount) * sizeof(Device), b.messageBytes, b.imageBytes, b.stackBytes};
    for (unsigned i = 0; i < 6; ++i) {
        // The first MiB contains the VMM bootstrap paging structures.
        if (starts[i] < (1ull << 20) || !Aligned(starts[i]) || !Range(starts[i], sizes[i], b.ramBytes)) return Result::InvalidRange;
        for (unsigned j = 0; j < i; ++j)
            if (Overlap(starts[i], sizes[i], starts[j], sizes[j])) return Result::Overlap;
    }
    if (!Aligned(b.imageBytes) || !Aligned(b.stackBytes) || b.stackBytes < PageBytes ||
        b.entryGpa < b.imageGpa || b.entryGpa - b.imageGpa >= b.imageBytes) return Result::InvalidRange;
    return Result::Ok;
}
inline Result ValidateMemoryMap(const MemoryRegion* regions, uint32_t count, uint64_t ramBytes) noexcept {
    if (!regions || !count || count > MaxRegions || ramBytes < MinRamBytes || ramBytes > MaxRamBytes || !Aligned(ramBytes)) return Result::InvalidMap;
    uint64_t end = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = regions[i];
        if (r.gpa != end || !Aligned(r.gpa) || !Aligned(r.bytes) || !Range(r.gpa, r.bytes, ramBytes) ||
            r.flags || r.reserved || static_cast<uint32_t>(r.kind) > static_cast<uint32_t>(MemoryKind::KernelStack)) return Result::InvalidMap;
        end = r.gpa + r.bytes;
    }
    return end == ramBytes ? Result::Ok : Result::InvalidMap;
}
inline bool Covered(const MemoryRegion* regions, uint32_t count, uint64_t gpa, uint64_t bytes, MemoryKind kind) noexcept {
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = regions[i];
        if (r.kind == kind && gpa >= r.gpa && gpa - r.gpa <= r.bytes && bytes <= r.bytes - (gpa - r.gpa)) return true;
    }
    return false;
}
inline Result ValidateLayout(const BootInfo& b, uint64_t selfGpa, uint64_t ramBytes, const MemoryRegion* regions) noexcept {
    auto result = ValidateBoot(b, selfGpa, ramBytes); if (result != Result::Ok) return result;
    result = ValidateMemoryMap(regions, b.memoryMapCount, b.ramBytes); if (result != Result::Ok) return result;
    if (!Covered(regions, b.memoryMapCount, 0, 1ull << 20, MemoryKind::Reserved) ||
        !Covered(regions, b.memoryMapCount, selfGpa, sizeof(BootInfo), MemoryKind::BootData) ||
        !Covered(regions, b.memoryMapCount, b.memoryMapGpa, uint64_t(b.memoryMapCount) * sizeof(MemoryRegion), MemoryKind::BootData) ||
        !Covered(regions, b.memoryMapCount, b.devicesGpa, uint64_t(b.deviceCount) * sizeof(Device), MemoryKind::BootData) ||
        !Covered(regions, b.memoryMapCount, b.messageGpa, b.messageBytes, MemoryKind::BootData) ||
        !Covered(regions, b.memoryMapCount, b.imageGpa, b.imageBytes, MemoryKind::KernelImage) ||
        !Covered(regions, b.memoryMapCount, b.stackGpa, b.stackBytes, MemoryKind::KernelStack)) return Result::InvalidMap;
    return Result::Ok;
}
inline Result ValidateDevices(const BootInfo& b, const Device* devices) noexcept {
    if (!devices || !b.deviceCount || b.deviceCount > MaxDevices) return Result::UnsupportedDevice;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < b.deviceCount; ++i) {
        const auto& d = devices[i];
        if (d.version != 1 || d.interrupt != NoInterrupt) return Result::UnsupportedDevice;
        uint32_t bit = 0;
        if (d.kind == DeviceKind::DebugConsole) {
            bit = 1;
            if (d.port != DebugPort || d.bufferGpa || d.bufferBytes) return Result::UnsupportedDevice;
        } else if (d.kind == DeviceKind::ControlChannel) {
            bit = 2;
            if (d.port != ControlPort || d.bufferGpa != b.messageGpa || d.bufferBytes != b.messageBytes) return Result::UnsupportedDevice;
        } else return Result::UnsupportedDevice;
        if (seen & bit) return Result::UnsupportedDevice;
        seen |= bit;
    }
    return seen == 3 ? Result::Ok : Result::UnsupportedDevice;
}
inline Result ValidateMessage(const MessageHeader& h, uint64_t packetBytes, uint64_t expectedBootId, uint64_t expectedSequence) noexcept {
    if (h.magic != MessageMagic) return Result::BadMagic;
    if (h.major != Major || h.minor != Minor) return Result::UnsupportedVersion;
    if (h.flags || h.reserved) return Result::ReservedBits;
    if (h.headerBytes != sizeof(MessageHeader) || h.payloadBytes > MaxMessageBytes - sizeof(MessageHeader) ||
        packetBytes != uint64_t(h.headerBytes) + h.payloadBytes) return Result::InvalidSize;
    if (!expectedBootId || h.bootId != expectedBootId) return Result::InvalidMessage;
    if (!expectedSequence || h.sequence != expectedSequence) return Result::SequenceMismatch;
    if (h.kind == MessageKind::Hello && h.payloadBytes == sizeof(HelloPayload)) return Result::Ok;
    if (h.kind == MessageKind::Diagnostic && h.payloadBytes && h.payloadBytes <= 1024) return Result::Ok;
    if (h.kind == MessageKind::StopResult && h.payloadBytes == sizeof(StopPayload)) return Result::Ok;
    return Result::InvalidMessage;
}
}
