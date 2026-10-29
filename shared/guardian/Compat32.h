#pragma once
#include <stdint.h>
namespace guardian::compat32 {
inline constexpr uint32_t Magic=0x32335747,Version=1;
enum class Op:uint32_t {Info,Console,Allocate,Release,Clock,Exit,Module,Symbol};
struct Request {uint32_t magic=Magic,version=Version,bytes=sizeof(Request);Op op{};uint32_t args[8]{},value=0,error=0;};
static_assert(sizeof(Request)==56);
inline constexpr uint32_t Gate=0xC1,StartGate=0xC0;
}
