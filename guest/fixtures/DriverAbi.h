#pragma once
#include <stdint.h>
#include <stddef.h>
namespace guardian::wdm {
struct UnicodeString {uint16_t length,maximum;uint32_t padding;uint16_t* buffer;};
struct DriverObject;
struct DeviceObject {DriverObject* owner;};
using Unload=void (*)(DriverObject*);
struct DriverObject {
    int16_t type,size;uint32_t padding;DeviceObject* device;
    uint32_t flags,padding2;void* start;uint32_t imageBytes,padding3;void* section;void* extension;
    UnicodeString name;UnicodeString* hardwareDatabase;void* fastIo;void* initialize;void* startIo;Unload unload;void* dispatch[28];
};
static_assert(sizeof(DriverObject)==336&&offsetof(DriverObject,unload)==104&&sizeof(UnicodeString)==16);
}
