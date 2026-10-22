#pragma once
#include <stddef.h>
// Construct kernel objects in an already reserved, supervisor-only arena.
// These overloads do not allocate and require no C++ runtime.
inline void* operator new(size_t,void* storage) noexcept {return storage;}
inline void* operator new[](size_t,void* storage) noexcept {return storage;}
