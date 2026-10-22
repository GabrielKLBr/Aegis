// Only generated guest programs and virtual in-memory files are used here.
#include "MicroVmTestGuest.h"
#include <set>
using O = GuestObjects;
namespace {
constexpr uint64_t HandleOut = 0x6400, DuplicateOut = 0x6410, SectionOut = 0x6420;
constexpr uint64_t OA = 0x6800, US = 0x6840, Text = 0x7000, IOSB = 0x6A00;
constexpr uint64_t ObjectInfo = 0x6600, LengthOut = 0x6650, Flags = 0x6670, Timeout = 0x6680;
constexpr uint64_t Data = 0x7800, ReadData = 0x7900, ValueName = 0x6B00, ValueText = 0x7200;
void Unicode(Fixture& f, uint64_t us, uint64_t buffer, const std::wstring& value) {
    check(f.memory.Zero(us, 16), "unicode fixture outside RAM");
    f.memory.Write(us, static_cast<uint16_t>(value.size() * 2));
    f.memory.Write(us + 2, static_cast<uint16_t>(value.size() * 2 + 2)); f.memory.Write(us + 8, buffer);
    std::memcpy(static_cast<uint8_t*>(f.vm.RamData()) + buffer, value.data(), value.size() * 2);
}
void Attributes(Fixture& f, const std::wstring& path, uint64_t root = 0) {
    f.memory.Zero(OA, 48); f.memory.Write(OA, uint32_t(48)); f.memory.Write(OA + 8, root);
    f.memory.Write(OA + 16, US); f.memory.Write(OA + 24, uint32_t(0x40)); Unicode(f, US, Text, path);
}
uint64_t OpenFile(Fixture& f, const std::wstring& path, uint32_t desired, uint32_t disposition = 1) {
    Attributes(f, path); f.memory.Write(HandleOut, uint64_t(0xABCD));
    check(f.Call("NtCreateFile", {HandleOut, desired, OA, IOSB}, {0, 0x80, 0, disposition, 0x40, 0, 0}) == 0, "file open failed");
    return f.Read(HandleOut);
}
uint64_t Duplicate(Fixture& f, uint64_t handle, uint32_t desired, uint32_t options = 0) {
    check(f.Call("NtDuplicateObject", {UINT64_MAX, handle, UINT64_MAX, DuplicateOut}, {desired, 0, options}) == 0, "duplicate failed");
    return f.Read(DuplicateOut);
}
O::BasicInformation QueryObject(Fixture& f, uint64_t handle) {
    f.memory.Write(LengthOut, uint64_t(0xAABBCCDDFFFFFFFFull));
    check(f.Call("NtQueryObject", {handle, 0, ObjectInfo, 56}, {LengthOut}) == 0, "object query failed");
    check(f.Read(LengthOut) == 0xAABBCCDD00000038ull, "object return length was not exactly ULONG");
    return f.Read<O::BasicInformation>(ObjectInfo);
}
uint32_t Io(Fixture& f, const char* name, uint64_t handle, uint64_t buffer, uint64_t bytes) {
    return f.Call(name, {handle, 0, 0, 0}, {IOSB, buffer, bytes, 0, 0});
}
uint64_t Section(Fixture& f, uint64_t file, uint32_t desired = 0xF001F, uint32_t protect = 2) {
    check(f.Call("NtCreateSection", {SectionOut, desired, 0, 0}, {protect, 0x8000000, file}) == 0, "section creation failed");
    return f.Read(SectionOut);
}
uint64_t Map(Fixture& f, uint64_t section, uint32_t protect = 2) {
    f.memory.Write(BaseSlot, uint64_t(0)); f.memory.Write(SizeSlot, uint64_t(0));
    check(f.Call("NtMapViewOfSection", {section, UINT64_MAX, BaseSlot, 0}, {0, 0, SizeSlot, 2, 0, protect}) == 0, "view map failed");
    return f.Read(BaseSlot);
}
}
int main() {
    BOOL available = FALSE; UINT32 written = 0;
    if (FAILED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &available, sizeof(available), &written)) || !available) {
        std::cout << "SKIP: WHP unavailable; no settings changed\n"; return 77;
    }
    try {
        Fixture f;
        const std::string bytes = "0123456789ABCDEF";
        f.kernel.Fs().Seed("C:\\objects\\data.bin", std::vector<uint8_t>(bytes.begin(), bytes.end()));
        const auto file = OpenFile(f, L"\\??\\C:\\objects\\data.bin", 0xC0000000);
        check(f.kernel.Fs().OpenHandleCount() == 1, "guest open leaked raw filesystem handles");
        auto info = QueryObject(f, file); check(info.grantedAccess == 0x12019F && info.handleCount == 1, "file access normalization wrong");
        const auto readOnly = Duplicate(f, file, 1);
        check(f.kernel.Fs().OpenHandleCount() == 1, "duplicate opened a second file object");
        check(QueryObject(f, readOnly).handleCount == 2 && QueryObject(f, readOnly).grantedAccess == 1, "duplicate identity or rights wrong");
        check(Io(f, "NtReadFile", readOnly, ReadData, 4) == 0 && f.Read<uint32_t>(ReadData) == 0x33323130 && f.Read(IOSB + 8) == 4, "file read failed");
        check(Io(f, "NtReadFile", file, ReadData, 4) == 0 && f.Read<uint32_t>(ReadData) == 0x37363534, "duplicate did not share file position");
        f.memory.Write(Data, uint32_t(0xDEADBEEF));
        check(Io(f, "NtWriteFile", readOnly, Data, 4) == O::AccessDenied && f.Read(IOSB + 8) == 0, "read-only file was writable");
        check(f.kernel.Fs().Files().at("C:\\objects\\data.bin")[8] == '8', "denied write changed file data");
        f.memory.Write(DuplicateOut, uint64_t(0xABCDEF));
        check(f.Call("NtDuplicateObject", {UINT64_MAX, readOnly, UINT64_MAX, DuplicateOut}, {2, 0, 0}) == O::AccessDenied &&
            f.Read(DuplicateOut) == 0xABCDEF && QueryObject(f, file).handleCount == 2, "duplicate escalated or leaked a handle");
        check(f.Call("NtDuplicateObject", {UINT64_MAX, file, UINT64_MAX, UINT64_MAX - 4}, {0, 0, O::SameAccess}) == M::AccessViolation &&
            QueryObject(f, file).handleCount == 2, "bad duplicate output leaked a handle");
        check(f.Call("NtDuplicateObject", {UINT64_MAX, file, UINT64_MAX, DuplicateOut}, {0, 0, 1}) == O::NotSupported && QueryObject(f, file).handleCount == 2,
            "unsupported close-source changed object state");

        check(f.Call("NtCreateEvent", {HandleOut, 0x1F0003, 0, 1}, {0}) == 0, "synchronization event creation failed");
        const auto event = f.Read(HandleOut), eventAlias = Duplicate(f, event, 0, O::SameAccess);
        check(f.Call("NtSetEvent", {file, 0, 0, 0}) == O::TypeMismatch && Io(f, "NtReadFile", event, ReadData, 1) == O::TypeMismatch,
            "wrong object type accepted");
        const auto waitOnly = Duplicate(f, event, 0x100000);
        check(f.Call("NtSetEvent", {waitOnly, 0, 0, 0}) == O::AccessDenied, "wait-only event could be signaled");
        f.memory.Write(Timeout, int64_t(0));
        check(f.Call("NtWaitForSingleObject", {event, 0, Timeout, 0}) == 0x102, "unsignaled poll falsely succeeded");
        check(f.Call("NtWaitForSingleObject", {event, 0, 0, 0}) == O::NotSupported, "blocking wait was fabricated");
        f.memory.Write(OldSlot, uint64_t(0xAABBCCDDFFFFFFFFull));
        check(f.Call("NtSetEvent", {eventAlias, OldSlot, 0, 0}) == 0 && f.Read(OldSlot) == 0xAABBCCDD00000000ull, "previous state canary wrong");
        check(f.Call("NtWaitForSingleObject", {waitOnly, 0, Timeout, 0}) == 0 &&
            f.Call("NtWaitForSingleObject", {eventAlias, 0, Timeout, 0}) == 0x102, "auto-reset state was not shared/consumed");
        check(f.Call("NtWaitForSingleObject", {file, 0, Timeout, 0}) == O::NotSupported, "unsupported file wait was fabricated");
        check(f.Call("NtCreateEvent", {HandleOut, 0x1F0003, 0, 0}, {1}) == 0, "notification event creation failed");
        const auto manual = f.Read(HandleOut);
        check(f.Call("NtWaitForSingleObject", {manual, 0, Timeout, 0}) == 0 && f.Call("NtWaitForSingleObject", {manual, 0, Timeout, 0}) == 0,
            "manual-reset event was consumed");
        check(f.Call("NtResetEvent", {manual, 0, 0, 0}) == 0 && f.Call("NtWaitForSingleObject", {manual, 0, Timeout, 0}) == 0x102,
            "event reset failed");

        f.memory.Write(Flags, uint16_t(0x0101));
        check(f.Call("NtSetInformationObject", {event, 4, Flags, 2}) == 0 && f.Call("NtClose", {event, 0, 0, 0}) == O::NotClosable,
            "protect-close ignored");
        f.memory.Write(Flags, uint64_t(0xFFEEDDCCBBAA0000ull));
        check(f.Call("NtQueryObject", {event, 4, Flags, 2}, {LengthOut}) == 0 && f.Read(Flags) == 0xFFEEDDCCBBAA0101ull,
            "two-byte handle flags layout wrong");
        check(QueryObject(f, eventAlias).attributes == 0, "flags changed through shared object");
        f.memory.Write(Flags, uint16_t(0));
        check(f.Call("NtSetInformationObject", {event, 4, Flags, 2}) == 0 && f.Call("NtClose", {event, 0, 0, 0}) == 0 &&
            f.Call("NtSetEvent", {event, 0, 0, 0}) == O::InvalidHandle && f.Call("NtSetEvent", {eventAlias, 0, 0, 0}) == 0,
            "closing one alias destroyed the shared event or revived stale handle");
        check(f.Call("NtClearEvent", {eventAlias, UINT64_MAX, 0, 0}) == 0 &&
            f.Call("NtWaitForSingleObject", {eventAlias, 0, Timeout, 0}) == 0x102,
            "one-argument NtClearEvent inspected an unspecified second register");

        const auto section = Section(f, file, 0xF001F, 4);
        check(f.Call("NtClose", {file, 0, 0, 0}) == 0 && f.Call("NtClose", {file, 0, 0, 0}) == O::InvalidHandle &&
            Io(f, "NtReadFile", readOnly, ReadData, 4) == 0, "closing original file destroyed duplicate");
        check(f.Call("NtClose", {readOnly, 0, 0, 0}) == 0, "last guest file handle close failed");
        check(f.kernel.Fs().OpenHandleCount() == 1, "section did not retain the underlying file object");
        const auto view = Map(f, section);
        check(f.Read<uint64_t>(view) == 0x3736353433323130ull, "file section lost its backing after all file handles closed");
        check(QueryObject(f, section).handleCount == 1 && QueryObject(f, section).pointerCount >= 2, "view did not retain section object");
        const auto readSection = Duplicate(f, section, 4);
        const auto limitedView = Map(f, readSection);
        check(f.Protect(limitedView, 4096, 4) == M::SectionProtection, "view protection bypassed reduced section-handle rights");
        f.memory.Write(BaseSlot, uint64_t(0)); f.memory.Write(SizeSlot, uint64_t(0));
        check(f.Call("NtMapViewOfSection", {readSection, UINT64_MAX, BaseSlot, 0}, {0, 0, SizeSlot, 2, 0, 4}) == O::AccessDenied,
            "read-only section handle mapped a writable view");
        check(f.Call("NtCreateSection", {SectionOut, 0xF001F, 0, 0}, {2, 0x8000000, eventAlias}) == O::TypeMismatch,
            "event accepted as section backing file");
        check(f.Call("NtClose", {section, 0, 0, 0}) == 0 && f.Call("NtClose", {readSection, 0, 0, 0}) == 0 && f.Probe(view, 0) == 0x3736353433323130ull,
            "last section handle close destroyed live view");
        check(f.Call("NtUnmapViewOfSection", {UINT64_MAX, view + 17, 0, 0}) == 0, "view unmap failed");
        f.Probe(view, 0, 0);
        check(f.Call("NtUnmapViewOfSection", {UINT64_MAX, limitedView, 0, 0}) == 0, "last view unmap failed");
        check(f.kernel.Fs().OpenHandleCount() == 0, "last view leaked the backing file reference");

        Attributes(f, L"\\Registry\\Machine\\Software\\ObjectsTest");
        check(f.Call("NtCreateKey", {HandleOut, 7, OA, 0}, {0, 0, LengthOut}) == 0 && f.Read<uint32_t>(LengthOut) == 1,
            "new key create disposition wrong");
        const auto key = f.Read(HandleOut), queryKey = Duplicate(f, key, 1);
        check(f.Call("NtCreateKey", {HandleOut, 1, OA, 0}, {0, 0, LengthOut}) == 0 && f.Read<uint32_t>(LengthOut) == 2 &&
            f.Call("NtClose", {f.Read(HandleOut), 0, 0, 0}) == 0, "existing key create disposition wrong");
        Attributes(f, L"Child", queryKey);
        check(f.Call("NtCreateKey", {HandleOut, 1, OA, 0}, {0, 0, LengthOut}) == O::AccessDenied, "query-only parent could create a subkey");
        Attributes(f, L"Child", key);
        check(f.Call("NtCreateKey", {HandleOut, 1, OA, 0}, {0, 0, LengthOut}) == 0 && f.Read<uint32_t>(LengthOut) == 1 &&
            f.Call("NtClose", {f.Read(HandleOut), 0, 0, 0}) == 0, "relative key creation failed");
        Unicode(f, ValueName, ValueText, L"Value"); f.memory.Write(Data, uint32_t(0x1234));
        check(f.Call("NtSetValueKey", {key, ValueName, 0, 4}, {Data, 4}) == 0 &&
            f.Call("NtSetValueKey", {queryKey, ValueName, 0, 4}, {Data, 4}) == O::AccessDenied, "key rights not enforced");
        check(f.Call("NtClose", {key, 0, 0, 0}) == 0 && f.Call("NtQueryValueKey", {queryKey, ValueName, 2, ObjectInfo}, {32, LengthOut}) == 0 &&
            f.Read<uint32_t>(ObjectInfo + 12) == 0x1234, "duplicated registry key lost identity/data");
        check(f.Call("NtQueryValueKey", {eventAlias, ValueName, 2, ObjectInfo}, {32, LengthOut}) == O::TypeMismatch, "wrong registry handle type accepted");

        check(f.Allocate(0, 4096, 0x3000, 4) == 0, "output permission fixture allocation failed");
        const auto protectedOut = f.Read(BaseSlot); check(f.Protect(protectedOut, 4096, 2) == 0, "output protection failed");
        check(f.Call("NtQueryObject", {eventAlias, 0, protectedOut, 56}, {LengthOut}) == M::AccessViolation,
            "object query bypassed read-only guest output");
        check(f.Call("NtCreateEvent", {protectedOut, 0x1F0003, 0, 0}, {0}) == M::AccessViolation,
            "object creation accepted read-only handle output");
        check(f.Call("NtClose", {0, 0, 0, 0}) == O::InvalidHandle && f.Call("NtClose", {UINT64_MAX, 0, 0, 0}) == O::InvalidHandle,
            "invalid/pseudo handle close succeeded");
        std::cout << "PASS: ring-3 typed handles, access checks, aliases/shared file position and event state, close flags, file->section->view lifetimes, reduced view permissions, registry rights and ABI output canaries\n";
        guardian::hardware::Info hardware{};hardware.magic=guardian::hardware::Magic;hardware.version=1;hardware.bytes=sizeof(hardware);
        hardware.ntMajor=10;hardware.ntBuild=22631;hardware.count=5;hardware.ramBytes=f.vm.RamSize();hardware.vcpus=1;
        for(uint32_t n=0;n<5;++n){auto& d=hardware.devices[n];d.kind=static_cast<guardian::hardware::Kind>(n+1);
            std::memcpy(d.requestedModel,"Selected model",15);std::memcpy(d.effectiveModel,"Effective model",16);
            const auto id="gv-0123456789abcdef0123456789abcdef-"+std::to_string(n+1);std::memcpy(d.syntheticId,id.c_str(),id.size()+1);}
        f.kernel.ConfigureHardware(hardware);
        Attributes(f,L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
        check(f.Call("NtOpenKey",{HandleOut,3,OA,0})==0,"Profile CPU key absent");const auto cpuKey=f.Read(HandleOut);
        Unicode(f,ValueName,ValueText,L"ProcessorNameString");
        check(f.Call("NtQueryValueKey",{cpuKey,ValueName,2,ReadData},{128,LengthOut})==0&&f.Read<uint32_t>(ReadData+4)==1,"Profile CPU value absent");
        const auto* value=static_cast<uint8_t*>(f.vm.RamData())+ReadData+12;
        for(size_t n=0;n<=std::strlen(hardware.devices[0].effectiveModel);++n)check(value[n*2]==hardware.devices[0].effectiveModel[n]&&!value[n*2+1],"MiniNt CPU projection differs");
        check(f.Call("NtSetValueKey",{cpuKey,ValueName,0,1},{Data,4})==O::AccessDenied,"Profile identity is writable");
        check(f.Call("NtClose",{cpuKey,0,0,0})==0,"Profile key cleanup failed");
        Attributes(f,L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\nvlddmkm");
        check(f.Call("NtOpenKey",{HandleOut,1,OA,0})!=0,"Unloaded NVIDIA driver still advertised");
        constexpr uint64_t firmwareOut=0x8000;
        f.memory.Write(firmwareOut,guardian::hardware::Rsmb);f.memory.Write(firmwareOut+4,uint32_t(1));f.memory.Write(firmwareOut+8,uint32_t(0));
        check(f.Call("NtQuerySystemInformation",{76,firmwareOut,16,LengthOut})==0xC0000023,"Firmware short buffer accepted");
        const auto firmware=guardian::hardware::Smbios(hardware);
        check(f.Read<uint32_t>(LengthOut)==16+firmware.size&&f.Read<uint32_t>(firmwareOut+12)==firmware.size,"Firmware required size differs");
        check(f.Call("NtQuerySystemInformation",{76,firmwareOut,2064,LengthOut})==0&&std::memcmp(static_cast<uint8_t*>(f.vm.RamData())+firmwareOut+16,firmware.bytes,firmware.size)==0,"MiniNt firmware differs from inventory");
        check(f.Call("NtQuerySystemInformation",{0,firmwareOut,64,LengthOut})==0&&f.Read<uint32_t>(firmwareOut+12)==4096&&f.Read<uint32_t>(firmwareOut+56)==1,"MiniNt RAM/CPU count differs");
        std::cout<<"PASS: real CPL3 NtOpenKey/NtQueryValueKey/NtSetValueKey and firmware/basic-information projection\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
