#pragma once
#include "PeRuntimeApi.h"
#include "NativeApi.h"
namespace guardian::win {
struct SecurityAttributes {Dword length;void* descriptor;Bool inherit;};
struct Overlapped {uint64_t internal=0,transferred=0;Dword offset=0,offsetHigh=0;Handle event=nullptr;};
struct SystemInfo {uint16_t architecture,reserved;Dword page;void* minimum;void* maximum;uint64_t mask;Dword processors,type,granularity;uint16_t level,revision;};
struct MemoryStatus {Dword length=sizeof(MemoryStatus),load=0;uint64_t total=0,available=0,totalPage=0,availablePage=0,totalVirtual=0,availableVirtual=0,extended=0;};
using PoolCallback=void (*)(void*,void*,void*);using IoCallback=void (*)(Dword,Dword,Overlapped*);
extern "C" {
RTAPI Handle GetCurrentProcess();RTAPI Handle GetCurrentThread();
RTAPI Bool CreateProcessW(const uint16_t*,uint16_t*,void*,void*,Bool,Dword,void*,const uint16_t*,StartupInfoA*,ProcessInformation*);
RTAPI uint16_t* GetCommandLineW();RTAPI Dword GetModuleFileNameA(Handle,char*,Dword);RTAPI Dword GetModuleFileNameW(Handle,uint16_t*,Dword);
RTAPI Handle LoadLibraryW(const uint16_t*);RTAPI Handle GetModuleHandleW(const uint16_t*);
RTAPI Handle HeapCreate(Dword,uint64_t,uint64_t);RTAPI Bool HeapDestroy(Handle);RTAPI uint64_t HeapSize(Handle,Dword,const void*);
RTAPI Handle CreateEventA(SecurityAttributes*,Bool,Bool,const char*);RTAPI Handle CreateEventW(SecurityAttributes*,Bool,Bool,const uint16_t*);
RTAPI Bool SetEvent(Handle);RTAPI Bool ResetEvent(Handle);RTAPI Dword WaitForMultipleObjects(Dword,const Handle*,Bool,Dword);RTAPI Dword SleepEx(Dword,Bool);
RTAPI Handle CreateFileW(const uint16_t*,Dword,Dword,void*,Dword,Dword,Handle);
RTAPI Bool GetOverlappedResult(Handle,Overlapped*,Dword*,Bool);RTAPI Bool CancelIoEx(Handle,Overlapped*);
RTAPI Bool ReadFileEx(Handle,void*,Dword,Overlapped*,IoCallback);RTAPI Bool WriteFileEx(Handle,const void*,Dword,Overlapped*,IoCallback);
RTAPI Handle CreateFileMappingW(Handle,SecurityAttributes*,Dword,Dword,Dword,const uint16_t*);
RTAPI Handle CreateFileMappingA(Handle,SecurityAttributes*,Dword,Dword,Dword,const char*);
RTAPI void* MapViewOfFile(Handle,Dword,Dword,Dword,uint64_t);RTAPI Bool UnmapViewOfFile(const void*);RTAPI Bool FlushViewOfFile(const void*,uint64_t);
RTAPI Bool CreateDirectoryW(const uint16_t*,SecurityAttributes*);RTAPI Bool CreateDirectoryA(const char*,SecurityAttributes*);
RTAPI Bool DeleteFileW(const uint16_t*);RTAPI Bool RemoveDirectoryW(const uint16_t*);RTAPI Bool RemoveDirectoryA(const char*);
RTAPI Dword GetFullPathNameW(const uint16_t*,Dword,uint16_t*,uint16_t**);RTAPI Dword GetFullPathNameA(const char*,Dword,char*,char**);
RTAPI Dword GetCurrentDirectoryW(Dword,uint16_t*);RTAPI Dword GetCurrentDirectoryA(Dword,char*);
RTAPI Bool SetCurrentDirectoryW(const uint16_t*);RTAPI Bool SetCurrentDirectoryA(const char*);
RTAPI Dword GetEnvironmentVariableW(const uint16_t*,uint16_t*,Dword);RTAPI Dword GetEnvironmentVariableA(const char*,char*,Dword);
RTAPI Bool SetEnvironmentVariableW(const uint16_t*,const uint16_t*);RTAPI Bool SetEnvironmentVariableA(const char*,const char*);
RTAPI uint16_t* GetEnvironmentStringsW();RTAPI char* GetEnvironmentStrings();RTAPI Bool FreeEnvironmentStringsW(uint16_t*);RTAPI Bool FreeEnvironmentStringsA(char*);
RTAPI int MultiByteToWideChar(Dword,Dword,const char*,int,uint16_t*,int);RTAPI int WideCharToMultiByte(Dword,Dword,const uint16_t*,int,char*,int,const char*,Bool*);
RTAPI Dword GetACP();RTAPI int CompareStringOrdinal(const uint16_t*,int,const uint16_t*,int,Bool);
RTAPI int LCMapStringEx(const uint16_t*,Dword,const uint16_t*,int,uint16_t*,int,void*,void*,int64_t);
RTAPI Bool GetVersionExW(native::VersionInfo*);RTAPI void GetSystemInfo(SystemInfo*);RTAPI void GetNativeSystemInfo(SystemInfo*);RTAPI Bool GlobalMemoryStatusEx(MemoryStatus*);
RTAPI void* CreateThreadpoolWork(PoolCallback,void*,void*);RTAPI void SubmitThreadpoolWork(void*);RTAPI void WaitForThreadpoolWorkCallbacks(void*,Bool);RTAPI void CloseThreadpoolWork(void*);
RTAPI Dword TlsAlloc();RTAPI Bool TlsFree(Dword);RTAPI Bool TlsSetValue(Dword,void*);RTAPI void* TlsGetValue(Dword);
RTAPI Dword FlsAlloc(void (*)(void*));RTAPI Bool FlsFree(Dword);RTAPI Bool FlsSetValue(Dword,void*);RTAPI void* FlsGetValue(Dword);
}
static_assert(sizeof(Overlapped)==32&&sizeof(SystemInfo)==48&&sizeof(MemoryStatus)==64);
}
