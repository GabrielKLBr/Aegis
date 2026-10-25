#pragma once
#include <stdint.h>
namespace guardian::peprofile {
#ifdef GUARDIAN_CRT_NATIVE
inline constexpr uint32_t SymbolCharacters=512;
#else
inline constexpr uint32_t SymbolCharacters=128;
#endif
#ifdef GUARDIAN_SCALABLE_PE
inline constexpr bool Extended=true;
#ifdef GUARDIAN_COMPONENT_NATIVE
inline constexpr uint32_t Modules=24;
#else
inline constexpr uint32_t Modules=16;
#endif
inline constexpr uint32_t Sections=32,ImageBytes=8u<<20,FileBytes=8u<<20;
#ifdef GUARDIAN_NATIVE_NT
inline constexpr uint32_t RuntimeFunctions=8192;
#else
inline constexpr uint32_t RuntimeFunctions=4096;
#endif
inline constexpr uint32_t Relocations=8192,Exports=8192,Imports=4096,TlsBytes=4096;
inline constexpr uint32_t PrivatePages=16384,Regions=256,InitCalls=16*9;
inline constexpr uint32_t RuntimeData=4096;
inline constexpr uint64_t ArenaBytes=256ull<<20,TebBytes=0x20000,TlsOffset=0x2000;
#else
inline constexpr bool Extended=false;
inline constexpr uint32_t Modules=4,Sections=16,ImageBytes=0x20000,FileBytes=0x20000;
inline constexpr uint32_t RuntimeFunctions=256,Relocations=256,Exports=256,Imports=256,TlsBytes=256;
inline constexpr uint32_t PrivatePages=128,Regions=64,RuntimeData=512,InitCalls=12;
inline constexpr uint64_t ArenaBytes=0x1000000,TebBytes=4096,TlsOffset=0x200;
#endif
static_assert(Modules<=32 && uint64_t(Modules)*ImageBytes+0x40000<ArenaBytes);
static_assert(InitCalls*24<=RuntimeData || !Extended);
}
