#include "DriverAbi.h"
#include <stdint.h>
using namespace guardian::wdm;
namespace {
DeviceObject device{};alignas(16) uint8_t pool[512]{};uint32_t poolTag=0;
uint32_t devices=0,pools=0,created=0,deleted=0,allocated=0,freed=0,errors=0;
}
extern "C" uint32_t IoCreateDevice(DriverObject* driver,uint32_t extra,UnicodeString* name,uint32_t type,uint32_t characteristics,uint8_t exclusive,DeviceObject** out){
    if(!out)return 0xC000000Du;*out=nullptr;
    if(!driver||driver->size!=336||devices||extra||name||type!=0x22||characteristics||exclusive){++errors;return 0xC00000BBu;}
    device.owner=driver;driver->device=&device;devices=1;++created;*out=&device;return 0;
}
extern "C" void IoDeleteDevice(DeviceObject* object){if(object!=&device||!devices||!device.owner){++errors;return;}device.owner->device=nullptr;device={};devices=0;++deleted;}
extern "C" void* ExAllocatePoolWithTag(uint32_t type,uint64_t bytes,uint32_t tag){
    if((type!=0&&type!=512)||!bytes||bytes>sizeof(pool)||pools){++errors;return nullptr;}
    for(auto& b:pool)b=0;pools=1;poolTag=tag;++allocated;return pool;
}
extern "C" void ExFreePoolWithTag(void* pointer,uint32_t tag){if(pointer!=pool||!pools||tag!=poolTag){++errors;return;}for(auto& b:pool)b=0;pools=0;poolTag=0;++freed;}
extern "C" uint64_t GuardianApiState(){return uint64_t(devices)|(uint64_t(pools)<<8)|(uint64_t(created)<<16)|(uint64_t(deleted)<<24)|(uint64_t(allocated)<<32)|(uint64_t(freed)<<40)|(uint64_t(errors)<<48);}
extern "C" int ApiEntry(void*,uint32_t,void*){return 1;}
