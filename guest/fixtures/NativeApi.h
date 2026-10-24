#pragma once
#include "guardian/NativeNt.h"
#include "guardian/Exception.h"
#ifdef GUARDIAN_NTDLL_BUILD
#define NTAPI_EXPORT __declspec(dllexport)
#else
#define NTAPI_EXPORT __declspec(dllimport)
#endif
namespace guardian::native {
using Status=uint32_t;using Handle=void*;
extern "C" {
NTAPI_EXPORT Status NtAllocateVirtualMemory(Handle,void**,uint64_t,uint64_t*,uint32_t,uint32_t);
NTAPI_EXPORT Status NtFreeVirtualMemory(Handle,void**,uint64_t*,uint32_t);
NTAPI_EXPORT Status NtProtectVirtualMemory(Handle,void**,uint64_t*,uint32_t,uint32_t*);
NTAPI_EXPORT Status NtQueryVirtualMemory(Handle,const void*,uint32_t,void*,uint64_t,uint64_t*);
NTAPI_EXPORT Status NtCreateSection(Handle*,uint32_t,Attributes*,int64_t*,uint32_t,uint32_t,Handle);
NTAPI_EXPORT Status NtMapViewOfSection(Handle,Handle,void**,uint64_t,uint64_t,int64_t*,uint64_t*,uint32_t,uint32_t,uint32_t);
NTAPI_EXPORT Status NtUnmapViewOfSection(Handle,void*);
NTAPI_EXPORT Status NtClose(Handle);NTAPI_EXPORT Status NtDuplicateObject(Handle,Handle,Handle,Handle*,uint32_t,uint32_t,uint32_t);
NTAPI_EXPORT Status NtQueryObject(Handle,uint32_t,void*,uint32_t,uint32_t*);
NTAPI_EXPORT Status NtQueryInformationProcess(Handle,uint32_t,void*,uint32_t,uint32_t*);
NTAPI_EXPORT Status NtQueryInformationThread(Handle,uint32_t,void*,uint32_t,uint32_t*);
NTAPI_EXPORT Status NtQuerySystemInformation(uint32_t,void*,uint32_t,uint32_t*);
NTAPI_EXPORT Status NtOpenProcess(Handle*,uint32_t,Attributes*,ClientId*);NTAPI_EXPORT Status NtOpenThread(Handle*,uint32_t,Attributes*,ClientId*);
NTAPI_EXPORT Status NtCreateThreadEx(Handle*,uint32_t,Attributes*,Handle,void*,void*,uint32_t,uint64_t,uint64_t,uint64_t,void*);
NTAPI_EXPORT Status NtResumeThread(Handle,uint32_t*);NTAPI_EXPORT Status NtTerminateThread(Handle,uint32_t);NTAPI_EXPORT Status NtTerminateProcess(Handle,uint32_t);
NTAPI_EXPORT Status NtCreateEvent(Handle*,uint32_t,Attributes*,uint32_t,uint8_t);NTAPI_EXPORT Status NtSetEvent(Handle,int32_t*);NTAPI_EXPORT Status NtResetEvent(Handle,int32_t*);
NTAPI_EXPORT Status NtCreateMutant(Handle*,uint32_t,Attributes*,uint8_t);NTAPI_EXPORT Status NtReleaseMutant(Handle,int32_t*);
NTAPI_EXPORT Status NtCreateSemaphore(Handle*,uint32_t,Attributes*,int32_t,int32_t);NTAPI_EXPORT Status NtReleaseSemaphore(Handle,int32_t,int32_t*);
NTAPI_EXPORT Status NtCreateTimer(Handle*,uint32_t,Attributes*,uint32_t);NTAPI_EXPORT Status NtSetTimer(Handle,int64_t*,void*,void*,uint8_t,int32_t,uint8_t*);NTAPI_EXPORT Status NtCancelTimer(Handle,uint8_t*);
NTAPI_EXPORT Status NtWaitForSingleObject(Handle,uint8_t,int64_t*);NTAPI_EXPORT Status NtWaitForMultipleObjects(uint32_t,Handle*,uint32_t,uint8_t,int64_t*);
NTAPI_EXPORT Status NtDelayExecution(uint8_t,int64_t*);NTAPI_EXPORT Status NtQueueApcThread(Handle,void*,void*,void*,void*);NTAPI_EXPORT Status NtTestAlert();
NTAPI_EXPORT Status NtQuerySystemTime(int64_t*);NTAPI_EXPORT Status NtQueryPerformanceCounter(int64_t*,int64_t*);NTAPI_EXPORT Status NtFlushInstructionCache(Handle,void*,uint64_t);
NTAPI_EXPORT Status NtCreateFile(Handle*,uint32_t,Attributes*,IoStatus*,int64_t*,uint32_t,uint32_t,uint32_t,uint32_t,void*,uint32_t);
NTAPI_EXPORT Status NtOpenFile(Handle*,uint32_t,Attributes*,IoStatus*,uint32_t,uint32_t);
NTAPI_EXPORT Status NtReadFile(Handle,Handle,void*,void*,IoStatus*,void*,uint32_t,int64_t*,uint32_t*);
NTAPI_EXPORT Status NtWriteFile(Handle,Handle,void*,void*,IoStatus*,const void*,uint32_t,int64_t*,uint32_t*);
NTAPI_EXPORT Status NtQueryInformationFile(Handle,IoStatus*,void*,uint32_t,uint32_t);
NTAPI_EXPORT Status NtCreateKey(Handle*,uint32_t,Attributes*,uint32_t,Unicode*,uint32_t,uint32_t*);NTAPI_EXPORT Status NtOpenKey(Handle*,uint32_t,Attributes*);
NTAPI_EXPORT Status NtSetValueKey(Handle,Unicode*,uint32_t,uint32_t,void*,uint32_t);NTAPI_EXPORT Status NtQueryValueKey(Handle,Unicode*,uint32_t,void*,uint32_t,uint32_t*);
NTAPI_EXPORT Status NtDeleteValueKey(Handle,Unicode*);NTAPI_EXPORT Status NtDeleteKey(Handle);
NTAPI_EXPORT Status RtlGetVersion(VersionInfo*);NTAPI_EXPORT void* RtlGetCurrentPeb();
NTAPI_EXPORT void RtlInitUnicodeString(Unicode*,const uint16_t*);NTAPI_EXPORT void RtlInitAnsiString(Ansi*,const char*);
NTAPI_EXPORT int32_t RtlCompareUnicodeString(const Unicode*,const Unicode*,uint8_t);NTAPI_EXPORT uint8_t RtlEqualUnicodeString(const Unicode*,const Unicode*,uint8_t);
NTAPI_EXPORT void* RtlCreateHeap(uint32_t,void*,uint64_t,uint64_t,void*,void*);NTAPI_EXPORT void* RtlDestroyHeap(void*);
NTAPI_EXPORT void* RtlAllocateHeap(void*,uint32_t,uint64_t);NTAPI_EXPORT uint8_t RtlFreeHeap(void*,uint32_t,void*);NTAPI_EXPORT void* RtlReAllocateHeap(void*,uint32_t,void*,uint64_t);NTAPI_EXPORT uint64_t RtlSizeHeap(void*,uint32_t,void*);
NTAPI_EXPORT Status RtlFlsAlloc(void (*)(void*),uint32_t*);NTAPI_EXPORT Status RtlFlsFree(uint32_t);NTAPI_EXPORT Status RtlFlsSetValue(uint32_t,void*);NTAPI_EXPORT Status RtlFlsGetValue(uint32_t,void**);
NTAPI_EXPORT uint32_t RtlNtStatusToDosError(Status);
#ifndef GUARDIAN_NATIVE_HOST_REFERENCE
NTAPI_EXPORT void RtlRaiseException(guardian::exceptions::Record*);
NTAPI_EXPORT uint16_t RtlCaptureStackBackTrace(uint32_t,uint32_t,void**,uint32_t*);
#endif
NTAPI_EXPORT Status LdrLoadDll(const uint16_t*,uint32_t*,Unicode*,Handle*);NTAPI_EXPORT Status LdrUnloadDll(Handle);
NTAPI_EXPORT Status LdrGetDllHandle(const uint16_t*,void*,Unicode*,Handle*);NTAPI_EXPORT Status LdrGetProcedureAddress(Handle,Ansi*,uint32_t,void**);
}
}
