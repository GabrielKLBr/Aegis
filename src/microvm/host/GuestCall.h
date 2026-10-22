#pragma once
#include "../vmm/MicroVmMonitor.h"
#include "GuestVirtualMemory.h"
#include <cstring>
namespace av { namespace microvm { namespace detail {
constexpr uint32_t AccessViolation=0xC0000005, InvalidParameter=0xC000000D, NotSupported=0xC00000BB;
class ObjectCall {
public:
    ObjectCall(MicroVm& vm, const GuestVirtualMemory& regions) : memory(vm.RamData(), vm.RamSize()), vm_(vm), regions_(regions) {
        const WHV_REGISTER_NAME names[]{WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterR8, WHvX64RegisterR9, WHvX64RegisterRsp};
        valid = vm.GetRegisters(names, 5, registers_);
    }
    uint64_t Arg(unsigned n) const { return registers_[n - 1].Reg64; }
    bool Access(uint64_t pointer, uint64_t bytes, uint8_t bits) const {
        return (!bytes || pointer) && vm_.CanAccessMemory(pointer, bytes, bits) && regions_.Allows(pointer, bytes, bits);
    }
    template<class T> bool Read(uint64_t pointer, T& value) const { return Access(pointer, sizeof(T), 1) && memory.Read(pointer, value); }
    bool Stack(unsigned n, uint64_t& value) const {
        const auto offset = 0x28ull + (n - 5ull) * 8, rsp = registers_[4].Reg64;
        return rsp <= UINT64_MAX - offset && Read(rsp + offset, value);
    }
    uint32_t String(uint64_t pointer, std::string& text, bool ntPath = false) const {
        if (!Access(pointer, 16, 1)) return AccessViolation;
        uint16_t length = 0, maximum = 0; uint64_t buffer = 0;
        memory.Read(pointer, length); memory.Read(pointer + 2, maximum); memory.Read(pointer + 8, buffer);
        if ((length & 1) || length > maximum) return InvalidParameter;
        if (!Access(buffer, length, 1)) return AccessViolation;
        std::wstring wide(length / 2, L'\0');
        if (length) std::memcpy(wide.data(), static_cast<const uint8_t*>(memory.Data()) + buffer, length);
        if (wide.find(L'\0') != std::wstring::npos) return 0xC0000033; // OBJECT_NAME_INVALID
        const int size = length ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr) : 0;
        if (length && !size) return 0xC0000033;
        text.resize(size);
        if (size) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr);
        if (ntPath && (text.rfind("\\??\\", 0) == 0 || text.rfind("\\\\?\\", 0) == 0)) text.erase(0, 4);
        return 0;
    }
    uint32_t Attributes(uint64_t pointer, uint32_t& flags, uint64_t& root, std::string& name, bool ntPath = false) const {
        flags = 0; root = 0; name.clear();
        if (!pointer) return 0;
        if (!Access(pointer, 48, 1)) return AccessViolation;
        uint32_t length = 0, attributes = 0; uint64_t string = 0, security = 0, quality = 0;
        memory.Read(pointer, length); memory.Read(pointer + 8, root); memory.Read(pointer + 16, string);
        memory.Read(pointer + 24, attributes); memory.Read(pointer + 32, security); memory.Read(pointer + 40, quality);
        if (length != 48) return InvalidParameter;
        if ((attributes & ~uint32_t(0x43)) || security || quality) return NotSupported;
        flags = attributes & 3;
        return string ? String(string, name, ntPath) : 0;
    }
    void Status(uint32_t status) {
        WHV_REGISTER_NAME name = WHvX64RegisterRax; WHV_REGISTER_VALUE value{}; value.Reg64 = status;
        vm_.SetRegisters(&name, 1, &value);
    }
    bool valid = false;
    GuestMemory memory;
private:
    MicroVm& vm_; const GuestVirtualMemory& regions_; WHV_REGISTER_VALUE registers_[5]{};
};
} } }
