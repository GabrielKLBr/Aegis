#pragma once
#include "guardian/Hardware.h"
#include "guardian/HardwareProjection.h"
#include "guardian/Abi.h"
#include <intrin.h>
namespace guardian::hardware {
inline bool CheckBootInventory(uint64_t ram) noexcept {
    const auto& info=*reinterpret_cast<const Info*>(InfoGpa);
    if(!info.magic){
        const auto* bytes=reinterpret_cast<const uint8_t*>(&info);
        for(size_t n=0;n<sizeof(info);++n)if(bytes[n])return false;
        return true; // Optional extension for previous boot contracts.
    }
    if(!Valid(info,ram))return false;
    char brand[49]{};int registers[4]{};
    for(unsigned leaf=0;leaf<3;++leaf){__cpuid(registers,static_cast<int>(0x80000002u+leaf));
        for(unsigned n=0;n<16;++n)brand[leaf*16+n]=static_cast<char>((static_cast<uint32_t>(registers[n/4])>>((n%4)*8))&255);}
    if(!Equal(brand,info.devices[0].effectiveModel))return false;
    __cpuid(registers,1);if(((static_cast<uint32_t>(registers[1])>>16)&255)!=info.vcpus||!(static_cast<uint32_t>(registers[2])&(1u<<31)))return false;
    const char message[]="hardware inventory v1 PASS ";
    for(size_t n=0;n<sizeof(message)-1;++n)__outbyte(abi::DebugPort,static_cast<unsigned char>(message[n]));
    const auto hash=Digest(info);const char digits[]="0123456789abcdef";
    for(unsigned n=0;n<16;++n)__outbyte(abi::DebugPort,static_cast<unsigned char>(digits[(hash>>((15-n)*4))&15]));
    __outbyte(abi::DebugPort,'\n');return true;
}
}
