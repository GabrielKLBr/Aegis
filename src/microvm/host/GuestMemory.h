// Checked access to the existing identity-mapped guest RAM. No address addition
// is performed before checking the range, including for UINT64_MAX pointers.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace av { namespace microvm {
class GuestMemory {
public:
    GuestMemory(void* data, size_t bytes) : data_(static_cast<uint8_t*>(data)), bytes_(bytes) {}
    bool Contains(uint64_t address, uint64_t size) const {
        return data_ && address <= bytes_ && size <= bytes_ - address;
    }
    template<class T> bool Read(uint64_t address, T& value) const {
        static_assert(std::is_trivially_copyable<T>::value, "guest values must be plain data");
        if (!Contains(address, sizeof(T))) return false;
        std::memcpy(&value, data_ + static_cast<size_t>(address), sizeof(T));
        return true;
    }
    template<class T> bool Write(uint64_t address, const T& value) const {
        static_assert(std::is_trivially_copyable<T>::value, "guest values must be plain data");
        if (!Contains(address, sizeof(T))) return false;
        std::memcpy(data_ + static_cast<size_t>(address), &value, sizeof(T));
        return true;
    }
    bool Zero(uint64_t address, uint64_t size) const {
        if (!Contains(address, size)) return false;
        std::memset(data_ + static_cast<size_t>(address), 0, static_cast<size_t>(size));
        return true;
    }
    bool Copy(uint64_t to, uint64_t from, uint64_t size) const {
        if (!Contains(to, size) || !Contains(from, size)) return false;
        std::memmove(data_ + static_cast<size_t>(to), data_ + static_cast<size_t>(from), static_cast<size_t>(size));
        return true;
    }
    void* Data() const { return data_; }
    size_t Bytes() const { return bytes_; }
private:
    uint8_t* data_;
    size_t bytes_;
};
} }
