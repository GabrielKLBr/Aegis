#pragma once
#include "Hardware.h"
namespace guardian::hardware {
inline char Fold(char c) noexcept {return c>='A'&&c<='Z'?char(c+'a'-'A'):c;}
inline bool Equal(const char* a,const char* b) noexcept {
    if(!a||!b)return false;
    for(uint32_t n=0;n<256;++n){if(Fold(a[n])!=Fold(b[n]))return false;if(!a[n])return true;}return false;
}
inline const char* MachineKey(const char* path) noexcept {
    const char* prefixes[]{"HKLM\\","\\Registry\\Machine\\"};
    for(const auto* prefix:prefixes){uint32_t n=0;while(prefix[n]&&path[n]&&Fold(prefix[n])==Fold(path[n]))++n;if(!prefix[n])return path+n;}
    return path;
}
struct RegistryValue {const char* key;const char* name;const char* value;};
inline constexpr uint32_t RegistryCount=15;
inline bool RegistryAt(const Info& i,uint32_t n,RegistryValue& v) noexcept {
    const char* cpu="HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    const char* bios="HARDWARE\\DESCRIPTION\\System\\BIOS";
    const char* video="HARDWARE\\DESCRIPTION\\System\\Video\\0";
    const char* disk="HARDWARE\\DESCRIPTION\\System\\Disk\\0";
    const char* requests="SOFTWARE\\Guardian\\Hardware";
    switch(n){
    case 0:v={cpu,"ProcessorNameString",i.devices[0].effectiveModel};break;
    case 1:v={bios,"BIOSVersion",i.devices[3].effectiveModel};break;
    case 2:v={bios,"BIOSVendor","Guardian"};break;
    case 3:v={bios,"BaseBoardProduct",i.devices[4].effectiveModel};break;
    case 4:v={bios,"SystemProductName",i.devices[4].effectiveModel};break;
    case 5:v={bios,"SystemSerialNumber",i.devices[4].syntheticId};break;
    case 6:v={video,"DeviceDesc",i.devices[1].effectiveModel};break;
    case 7:v={video,"InstanceId",i.devices[1].syntheticId};break;
    case 8:v={disk,"Model",i.devices[2].effectiveModel};break;
    case 9:v={disk,"SerialNumber",i.devices[2].syntheticId};break;
    default:if(n>=RegistryCount)return false;v={requests,n==10?"RequestedCpu":n==11?"RequestedGpu":n==12?"RequestedDisk":n==13?"RequestedBios":"RequestedBoard",i.devices[n-10].requestedModel};break;
    }return true;
}
inline bool RegistryKey(const Info& i,const char* path) noexcept {
    path=MachineKey(path);RegistryValue value{};
    for(uint32_t n=0;RegistryAt(i,n,value);++n)if(Equal(path,value.key))return true;return false;
}
inline const char* RegistryQuery(const Info& i,const char* path,const char* name) noexcept {
    path=MachineKey(path);RegistryValue value{};
    for(uint32_t n=0;RegistryAt(i,n,value);++n)if(Equal(path,value.key)&&Equal(name,value.name))return value.value;return nullptr;
}
inline uint32_t Length(const char* s) noexcept {uint32_t n=0;while(n<256&&s[n])++n;return n;}
// CPUID identity projection keeps the real instruction capabilities and keeps
// virtualization visible. A chosen brand is a label, not ISA emulation.
inline void CpuIdentity(const char* brand,uint32_t leaf,uint32_t sub,uint32_t& a,uint32_t& b,uint32_t& c,uint32_t& d) noexcept {
    if(leaf>=0x80000002&&leaf<=0x80000004){
        uint32_t values[4]{};const uint32_t start=(leaf-0x80000002)*16,n=Length(brand);
        for(uint32_t k=0;k<16;++k){const uint32_t index=start+k;const uint8_t value=index<n?uint8_t(brand[index]):0;values[k/4]|=uint32_t(value)<<((k%4)*8);}
        a=values[0];b=values[1];c=values[2];d=values[3];
    }else if(leaf==1){b=(b&0x0000FFFFu)|(1u<<16);c|=1u<<31;d&=~(1u<<28);}
    else if(leaf==0xB||leaf==0x1F){a=0;b=sub<2?1u:0u;c=(sub<2?(sub+1)<<8:0)|(sub&255);d=0;}
    else if(leaf==4||leaf==0x8000001D){a&=0x3FFFu;}
    else if(leaf==0x80000008){c&=~0xF0FFu;}
    else if(leaf==0x8000001E){a=0;b=0;c=0;d=0;}
}
inline constexpr uint32_t Rsmb=0x52534D42;
struct Firmware {
    uint8_t bytes[2048]{};uint32_t size=8;bool good=true;
    void Byte(uint8_t b) noexcept {if(size<sizeof(bytes))bytes[size++]=b;else good=false;}
    void Word(uint16_t v) noexcept {Byte(uint8_t(v));Byte(uint8_t(v>>8));}
    void Begin(uint8_t type,uint8_t length,uint16_t handle) noexcept {Byte(type);Byte(length);Word(handle);}
    void Zeros(uint32_t n) noexcept {for(uint32_t k=0;k<n;++k)Byte(0);}
    void String(const char* text) noexcept {for(uint32_t n=0;n<96&&text[n];++n)Byte(uint8_t(text[n]));Byte(0);}
};
inline Firmware Smbios(const Info& i) noexcept {
    Firmware f;f.bytes[1]=3;f.bytes[2]=2;
    f.Begin(0,0x18,0);f.Byte(1);f.Byte(2);f.Word(0);f.Byte(3);f.Byte(0);f.Byte(8);f.Zeros(7);f.Zeros(2);f.Byte(0xFF);f.Byte(0xFF);f.Byte(0xFF);f.Byte(0xFF);
    f.String("Guardian");f.String(i.devices[3].effectiveModel);f.String("01/01/2026");f.Byte(0);
    f.Begin(1,0x1B,1);f.Byte(1);f.Byte(2);f.Byte(3);f.Byte(4);
    const char* id=i.devices[4].syntheticId+3;
    for(uint32_t n=0;n<16;++n){const auto hex=[](char c) noexcept -> uint8_t{return c>='0'&&c<='9'?uint8_t(c-'0'):c>='a'&&c<='f'?uint8_t(c-'a'+10):0;};f.Byte(uint8_t((hex(id[n*2])<<4)|hex(id[n*2+1])));}
    f.Byte(2);f.Byte(0);f.Byte(0);f.String("Guardian");f.String(i.devices[4].effectiveModel);f.String("1");f.String(i.devices[4].syntheticId);f.Byte(0);
    f.Begin(2,0x0F,2);f.Byte(1);f.Byte(2);f.Byte(3);f.Byte(4);f.Byte(0);f.Byte(0);f.Byte(0);f.Word(0);f.Byte(0x0A);f.Byte(0);
    f.String("Guardian");f.String(i.devices[4].effectiveModel);f.String("1");f.String(i.devices[4].syntheticId);f.Byte(0);
    f.Begin(4,0x30,4);f.Byte(1);f.Byte(3);f.Byte(2);f.Byte(2);f.Zeros(8);f.Byte(3);f.Byte(0);f.Word(0);f.Word(0);f.Word(0);f.Byte(0x41);f.Byte(2);
    f.Word(0xFFFF);f.Word(0xFFFF);f.Word(0xFFFF);f.Byte(4);f.Byte(0);f.Byte(0);f.Byte(1);f.Byte(1);f.Byte(1);f.Word(4);f.Word(2);f.Word(1);f.Word(1);f.Word(1);
    f.String("CPU0");f.String("Virtual x64");f.String(i.devices[0].effectiveModel);f.String(i.devices[0].syntheticId);f.Byte(0);
    f.Begin(127,4,127);f.Byte(0);f.Byte(0);
    const uint32_t length=f.size-8;for(uint32_t n=0;n<4;++n)f.bytes[4+n]=uint8_t(length>>(n*8));
    return f;
}
}
