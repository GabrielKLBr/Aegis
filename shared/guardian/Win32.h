#pragma once
#include <stdint.h>
namespace guardian::win32 {
inline constexpr uint64_t ParametersOffset=0x400,EnvironmentOffset=0xB00;
inline constexpr uint32_t EnvironmentCharacters=512;
struct Request {uint32_t magic=0x34424B47,version=1,bytes=sizeof(Request),op=0;uint64_t args[8]{};};
struct String {uint16_t length=0,maximum=0;uint32_t padding=0;uint64_t buffer=0;};
struct Parameters {uint32_t maximum=sizeof(Parameters),length=sizeof(Parameters),flags=1,debug=0;
    uint64_t console=0;uint32_t consoleFlags=0,padding=0;uint64_t input=0,output=0,error=0;
    String directory;uint64_t directoryHandle=0;String dllPath,imagePath,command;uint64_t environment=0;};
static_assert(sizeof(Request)==80&&sizeof(String)==16&&sizeof(Parameters)==136);
}
