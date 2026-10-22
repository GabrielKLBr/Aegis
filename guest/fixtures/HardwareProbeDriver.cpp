#include "DriverAbi.h"
#include "guardian/Driver.h"
#include <intrin.h>
using namespace guardian::wdm;
extern "C" {
__declspec(dllimport) uint32_t IoCreateDevice(DriverObject*,uint32_t,UnicodeString*,uint32_t,uint32_t,uint8_t,DeviceObject**);
__declspec(dllimport) void IoDeleteDevice(DeviceObject*);
__declspec(dllimport) void* ExAllocatePoolWithTag(uint32_t,uint64_t,uint32_t);
__declspec(dllimport) void ExFreePoolWithTag(void*,uint32_t);
}
namespace {
DeviceObject* device=nullptr;volatile uint32_t* memory=nullptr;
void ProbeUnload(DriverObject*){if(device){IoDeleteDevice(device);device=nullptr;}if(memory){ExFreePoolWithTag(const_cast<uint32_t*>(memory),0x32315647);memory=nullptr;}}
}
extern "C" uint32_t DriverEntry(DriverObject* driver,UnicodeString*){
#ifdef GUARDIAN_DRIVER_FAIL_ENTRY
    ProbeUnload(driver);return 0xC0000001u;
#else
    memory=static_cast<uint32_t*>(ExAllocatePoolWithTag(512,64,0x32315647));if(!memory)return 0xC000009Au;
    const auto status=IoCreateDevice(driver,0,nullptr,0x22,0,0,&device);if(status){ProbeUnload(driver);return status;}
    driver->unload=ProbeUnload;return 0;
#endif
}
extern "C" __declspec(dllexport) uint32_t GuardianDriverProbe(uint32_t value){
    if(!device||!memory)return UINT32_MAX;
    __outdword(guardian::driver::DevicePort,value);memory[0]=__indword(guardian::driver::DevicePort);return memory[0];
}
