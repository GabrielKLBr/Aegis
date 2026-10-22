// Wire contract shared by the host and the future guest kernel. No OS pointers.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace guardian::abi {
inline constexpr uint16_t Major = 1, Minor = 0;
inline constexpr uint32_t X64 = 0x8664, PageBytes = 4096;
inline constexpr uint64_t BootMagic = 0x31544F4F42415647ull; // "GVABOOT1", LE
inline constexpr uint32_t MessageMagic = 0x314D5647;         // "GVM1", LE
inline constexpr uint64_t MinRamBytes = 16ull << 20, MaxRamBytes = 1ull << 30;
inline constexpr uint32_t MaxRegions = 128, MaxDevices = 8, MaxMessageBytes = 4096;
inline constexpr uint16_t DebugPort = 0xE9, ControlPort = 0xEA;
inline constexpr uint32_t NoInterrupt = UINT32_MAX;

enum class Result : uint32_t {
    Ok, BadMagic, UnsupportedVersion, InvalidSize, ReservedBits, InvalidRange,
    Overlap, InvalidMap, UnsupportedDevice, InvalidMessage, SequenceMismatch
};
enum class MemoryKind : uint32_t { Reserved, Available, BootData, KernelImage, KernelStack };
enum class DeviceKind : uint32_t { DebugConsole = 1, ControlChannel = 2 };
enum class MessageKind : uint16_t { Hello = 1, Diagnostic = 2, StopResult = 3 };

struct BootInfo {
    uint64_t magic;
    uint16_t major, minor;
    uint32_t bytes;
    uint32_t machine, flags;
    uint64_t ramBytes, imageGpa, imageBytes, entryGpa, stackGpa, stackBytes;
    uint64_t memoryMapGpa;
    uint32_t memoryMapCount, memoryMapEntryBytes;
    uint64_t devicesGpa;
    uint32_t deviceCount, deviceEntryBytes;
    uint64_t messageGpa;
    uint32_t messageBytes, vcpuCount;
    uint64_t bootId;
    uint64_t reserved[4];
};
struct MemoryRegion {
    uint64_t gpa, bytes;
    MemoryKind kind;
    uint32_t flags;
    uint64_t reserved;
};
struct Device {
    DeviceKind kind;
    uint32_t version, port, interrupt;
    uint64_t bufferGpa, bufferBytes;
};
struct MessageHeader {
    uint32_t magic;
    uint16_t major, minor;
    MessageKind kind;
    uint16_t flags;
    uint32_t headerBytes, payloadBytes, reserved;
    uint64_t sequence, bootId;
};
struct HelloPayload { uint32_t machine, vcpuCount; uint64_t capabilities; };
struct StopPayload { uint32_t status, reserved; };

static_assert(sizeof(void*) == 8, "The initial ABI requires an x64 target");
static_assert(sizeof(BootInfo) == 160 && alignof(BootInfo) == 8);
static_assert(offsetof(BootInfo, ramBytes) == 24 && offsetof(BootInfo, bootId) == 120);
static_assert(sizeof(MemoryRegion) == 32 && offsetof(MemoryRegion, kind) == 16);
static_assert(sizeof(Device) == 32 && offsetof(Device, bufferGpa) == 16);
static_assert(sizeof(MessageHeader) == 40 && offsetof(MessageHeader, sequence) == 24);
static_assert(sizeof(HelloPayload) == 16 && sizeof(StopPayload) == 8);
}
