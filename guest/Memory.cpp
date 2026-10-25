// Minimal local byte routines for compiler-generated aggregate copies. Volatile
// accesses prevent an optimizing compiler from replacing a body with itself.
#include <stddef.h>
extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);
extern "C" void* memmove(void*, const void*, size_t);
extern "C" int memcmp(const void*, const void*, size_t);
#pragma function(memcpy, memset, memmove, memcmp)
extern "C" void* memcpy(void* destination, const void* source, size_t bytes) {
    auto* out = static_cast<volatile unsigned char*>(destination);
    const auto* in = static_cast<const volatile unsigned char*>(source);
    for (size_t i = 0; i < bytes; ++i) out[i] = in[i];
    return destination;
}
extern "C" void* memset(void* destination, int value, size_t bytes) {
    auto* out = static_cast<volatile unsigned char*>(destination);
    for (size_t i = 0; i < bytes; ++i) out[i] = static_cast<unsigned char>(value);
    return destination;
}
extern "C" void* memmove(void* destination, const void* source, size_t bytes) {
    auto* out = static_cast<volatile unsigned char*>(destination);
    const auto* in = static_cast<const volatile unsigned char*>(source);
    if (reinterpret_cast<size_t>(out) < reinterpret_cast<size_t>(in))
        for (size_t i = 0; i < bytes; ++i) out[i] = in[i];
    else
        while (bytes) { --bytes; out[bytes] = in[bytes]; }
    return destination;
}

extern "C" int memcmp(const void* first, const void* second, size_t bytes) {
    const auto* left = static_cast<const volatile unsigned char*>(first);
    const auto* right = static_cast<const volatile unsigned char*>(second);
    for (size_t i = 0; i < bytes; ++i) {
        if (left[i] != right[i]) return left[i] < right[i] ? -1 : 1;
    }
    return 0;
}
