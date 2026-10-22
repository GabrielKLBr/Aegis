#include "guardian/Validate.h"
#include <iostream>
#include <stdexcept>
using namespace guardian::abi;
extern "C" uint32_t GuardianCheckBoot(const BootInfo*, uint64_t, uint64_t, const MemoryRegion*) noexcept;
extern "C" uint32_t GuardianCheckMessage(const MessageHeader*, uint64_t, uint64_t, uint64_t) noexcept;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
BootInfo Boot() {
    BootInfo b{};
    b.magic = BootMagic; b.major = Major; b.minor = Minor; b.bytes = sizeof(b); b.machine = X64;
    b.ramBytes = MinRamBytes; b.imageGpa = 0x200000; b.imageBytes = 0x10000; b.entryGpa = b.imageGpa;
    b.stackGpa = 0x400000; b.stackBytes = 0x10000;
    b.memoryMapGpa = 0x101000; b.memoryMapCount = 7; b.memoryMapEntryBytes = sizeof(MemoryRegion);
    b.devicesGpa = 0x102000; b.deviceCount = 2; b.deviceEntryBytes = sizeof(Device);
    b.messageGpa = 0x103000; b.messageBytes = MaxMessageBytes; b.vcpuCount = 1; b.bootId = 42;
    return b;
}
void BootContract() {
    auto b = Boot(); constexpr uint64_t self = 0x100000;
    MemoryRegion regions[]{
        {0, 0x100000, MemoryKind::Reserved, 0, 0},
        {0x100000, 0x100000, MemoryKind::BootData, 0, 0},
        {0x200000, 0x10000, MemoryKind::KernelImage, 0, 0},
        {0x210000, 0x1F0000, MemoryKind::Available, 0, 0},
        {0x400000, 0x10000, MemoryKind::KernelStack, 0, 0},
        {0x410000, 0x10000, MemoryKind::Reserved, 0, 0},
        {0x420000, MinRamBytes - 0x420000, MemoryKind::Available, 0, 0}
    };
    check(GuardianCheckBoot(&b, self, MinRamBytes, regions) == 0, "valid boot contract rejected by guest object");
    check(GuardianCheckBoot(nullptr, self, MinRamBytes, regions) != 0, "null boot accepted");
    auto bad = b; bad.major++;
    check(ValidateBoot(bad, self, MinRamBytes) == Result::UnsupportedVersion, "unknown boot ABI accepted");
    bad = b; bad.reserved[3] = 1;
    check(ValidateBoot(bad, self, MinRamBytes) == Result::ReservedBits, "reserved boot data accepted");
    bad = b; bad.memoryMapCount = UINT32_MAX;
    check(ValidateBoot(bad, self, MinRamBytes) == Result::InvalidSize, "unbounded memory map accepted");
    bad = b; bad.stackGpa = UINT64_MAX - 4095;
    check(ValidateBoot(bad, self, MinRamBytes) == Result::InvalidRange, "overflowing stack accepted");
    bad = b; bad.stackGpa = b.imageGpa;
    check(ValidateBoot(bad, self, MinRamBytes) == Result::Overlap, "overlapping boot buffers accepted");
    bad = b; bad.entryGpa = b.imageGpa + b.imageBytes;
    check(ValidateBoot(bad, self, MinRamBytes) == Result::InvalidRange, "entry outside image accepted");
    check(ValidateBoot(b, 0x70000, MinRamBytes) == Result::InvalidRange, "bootstrap paging overwritten");
    check(ValidateBoot(b, self, MinRamBytes * 2) == Result::InvalidRange, "reported RAM differs from backing");
    regions[3].gpa++;
    check(ValidateLayout(b, self, MinRamBytes, regions) == Result::InvalidMap, "map gap/unaligned range accepted");
    regions[3].gpa--; regions[2].kind = MemoryKind::Available;
    check(ValidateLayout(b, self, MinRamBytes, regions) == Result::InvalidMap, "kernel image marked allocatable accepted");
    regions[2].kind = MemoryKind::KernelImage; regions[0].kind = MemoryKind::Available;
    check(ValidateLayout(b, self, MinRamBytes, regions) == Result::InvalidMap, "bootstrap marked allocatable accepted");
    Device devices[]{
        {DeviceKind::DebugConsole, 1, DebugPort, NoInterrupt, 0, 0},
        {DeviceKind::ControlChannel, 1, ControlPort, NoInterrupt, b.messageGpa, b.messageBytes}
    };
    check(ValidateDevices(b, devices) == Result::Ok, "valid device table rejected");
    devices[1].bufferGpa++;
    check(ValidateDevices(b, devices) == Result::UnsupportedDevice, "device channel disagrees with boot");
    devices[1] = devices[0];
    check(ValidateDevices(b, devices) == Result::UnsupportedDevice, "duplicate device accepted");
    devices[1].kind = static_cast<DeviceKind>(999);
    check(ValidateDevices(b, devices) == Result::UnsupportedDevice, "unknown device accepted");
}
void MessageContract() {
    MessageHeader h{MessageMagic, Major, Minor, MessageKind::Hello, 0, sizeof(MessageHeader), sizeof(HelloPayload), 0, 1, 42};
    const uint64_t bytes = sizeof(MessageHeader) + sizeof(HelloPayload);
    check(GuardianCheckMessage(&h, bytes, 42, 1) == 0, "valid message rejected by guest object");
    check(GuardianCheckMessage(&h, bytes - 1, 42, 1) == static_cast<uint32_t>(Result::InvalidSize), "truncated message accepted");
    check(GuardianCheckMessage(&h, bytes, 42, 2) == static_cast<uint32_t>(Result::SequenceMismatch), "replayed sequence accepted");
    check(GuardianCheckMessage(&h, bytes, 43, 1) == static_cast<uint32_t>(Result::InvalidMessage), "message from other boot accepted");
    auto bad = h; bad.payloadBytes = UINT32_MAX;
    check(ValidateMessage(bad, UINT64_MAX, 42, 1) == Result::InvalidSize, "unbounded packet accepted");
    bad = h; bad.flags = 1;
    check(ValidateMessage(bad, bytes, 42, 1) == Result::ReservedBits, "unknown flag accepted");
    bad = h; bad.kind = static_cast<MessageKind>(999);
    check(ValidateMessage(bad, bytes, 42, 1) == Result::InvalidMessage, "unknown operation accepted");
    bad = h; bad.kind = MessageKind::Diagnostic; bad.payloadBytes = 1025;
    check(ValidateMessage(bad, sizeof(MessageHeader) + 1025, 42, 1) == Result::InvalidMessage, "unbounded diagnostic accepted");
    h.kind = MessageKind::StopResult; h.payloadBytes = sizeof(StopPayload);
    check(ValidateMessage(h, sizeof(MessageHeader) + sizeof(StopPayload), 42, 1) == Result::Ok, "valid stop envelope rejected");
    check(!Range(UINT64_MAX - 1, 4, UINT64_MAX), "overflowing range accepted");
}
}
int main() {
    try {
        BootContract(); MessageContract();
        std::cout << "PASS: boot layout, memory ownership, device descriptors, bounded messages, versioning and sequence checks through freestanding object\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
