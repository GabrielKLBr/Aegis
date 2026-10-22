// First own guest kernel: no Windows imports, CRT startup, allocation or loader.
#include "guardian/Validate.h"
#include "HardwareChecks.h"
#ifdef GUARDIAN_DRIVER_KERNEL
#include "DriverKernel.h"
#endif
#include <intrin.h>
#if defined(_WINDOWS_) || defined(_CPPUNWIND) || defined(_CPPRTTI) || !defined(_M_X64)
#error Guest kernel requires freestanding MSVC x64 configuration
#endif
using namespace guardian::abi;
namespace {
volatile uint64_t zeroProbe[4];
volatile uint64_t initializedProbe = 0x1020304050607080ull;

void Debug(const char* text) noexcept {
    for (unsigned i = 0; text[i] && i < 256; ++i) __outbyte(DebugPort, static_cast<unsigned char>(text[i]));
}
__declspec(noreturn) void Halt() noexcept { for (;;) __halt(); }
void Copy(volatile uint8_t* destination, const void* source, uint32_t bytes) noexcept {
    const auto* input = static_cast<const uint8_t*>(source);
    for (uint32_t i = 0; i < bytes; ++i) destination[i] = input[i];
}
void Send(const BootInfo& boot, uint64_t sequence, MessageKind kind, const void* payload, uint32_t bytes) noexcept {
    const MessageHeader header{MessageMagic, Major, Minor, kind, 0, sizeof(MessageHeader), bytes, 0, sequence, boot.bootId};
    auto* buffer = reinterpret_cast<volatile uint8_t*>(boot.messageGpa);
    Copy(buffer, &header, sizeof(header)); Copy(buffer + sizeof(header), payload, bytes);
    _ReadWriteBarrier(); __outbyte(ControlPort, 1);
}
__declspec(noinline) uint64_t CheckComputation(uint64_t value) noexcept { return value ^ initializedProbe; }
uint32_t SelfTest(const BootInfo& boot) noexcept {
    if (zeroProbe[0] || zeroProbe[1] || zeroProbe[2] || zeroProbe[3]) return 2;
    if (initializedProbe != 0x1020304050607080ull || CheckComputation(0x1020304050607080ull)) return 3;
    const auto stack = reinterpret_cast<uint64_t>(_AddressOfReturnAddress());
    if (stack < boot.stackGpa + PageBytes || stack >= boot.stackGpa + boot.stackBytes) return 4;
    zeroProbe[0] = boot.bootId; zeroProbe[3] = ~boot.bootId;
    if (zeroProbe[0] != boot.bootId || zeroProbe[3] != ~boot.bootId) return 5;
    const auto tail=boot.ramBytes-sizeof(uint64_t);
    if(Covered(reinterpret_cast<const MemoryRegion*>(boot.memoryMapGpa),boot.memoryMapCount,tail,sizeof(uint64_t),MemoryKind::Available)){
        auto* last=reinterpret_cast<volatile uint64_t*>(tail);const auto before=*last;
        *last=boot.bootId^0xA17E5C0DE1234567ull;_ReadWriteBarrier();
        if(*last!=(boot.bootId^0xA17E5C0DE1234567ull))return 6;
        *last=before;_ReadWriteBarrier();
    }
    return 0;
}
}
extern "C" __declspec(noreturn) void GuardianKernelEntry(const BootInfo* parameter) noexcept {
    Debug("Guardian kernel 0.1.0\n");
    if (reinterpret_cast<uint64_t>(parameter) != 0x100000) { Debug("invalid boot pointer\n"); Halt(); }
    const BootInfo boot = *parameter;
    if (ValidateBoot(boot, 0x100000, boot.ramBytes) != Result::Ok) { Debug("invalid boot header\n"); Halt(); }
    if(!guardian::hardware::CheckBootInventory(boot.ramBytes)){Debug("invalid hardware inventory\n");Halt();}
    const auto* regions = reinterpret_cast<const MemoryRegion*>(boot.memoryMapGpa);
    const auto* devices = reinterpret_cast<const Device*>(boot.devicesGpa);
    if (ValidateLayout(boot, 0x100000, boot.ramBytes, regions) != Result::Ok || ValidateDevices(boot, devices) != Result::Ok) {
        Debug("invalid boot tables\n"); Halt();
    }
    const HelloPayload hello{X64, 1, 0}; Send(boot, 1, MessageKind::Hello, &hello, sizeof(hello));
    auto status = SelfTest(boot);
#ifdef GUARDIAN_DRIVER_KERNEL
    if(!status)status=HardwareDriverRun(boot);
#endif
    const char* diagnostic = status ? "kernel self-test FAIL" : "kernel self-test PASS";
    uint32_t bytes = 0; while (diagnostic[bytes]) ++bytes;
    Send(boot, 2, MessageKind::Diagnostic, diagnostic, bytes);
    const StopPayload stop{status, 0}; Send(boot, 3, MessageKind::StopResult, &stop, sizeof(stop));
    Halt(); // A successful host stops at StopResult; reaching HLT is a backstop.
}
