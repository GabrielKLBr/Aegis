#pragma once
#include <stdint.h>
#include "guardian/Exception.h"
#include "guardian/Memory.h"
#include "guardian/Environment.h"
#if defined(GUARDIAN_RUNTIME_BUILD)
#define RTAPI
#else
#define RTAPI __declspec(dllimport)
#endif
namespace guardian::win {
using Procedure=void (*)();using Handle=void*;using Bool=int;using Dword=uint32_t;using ThreadStart=Dword (*)(void*);
struct FileTime {uint32_t low,high;};
#ifdef GUARDIAN_SCALABLE_PE
struct ActCtxA {uint32_t cb,flags;const char* source;uint16_t architecture,language;const char* directory;const char* resource;const char* application;Handle module;};
static_assert(sizeof(ActCtxA)==56);
#endif
struct CriticalSection {void* debug;int32_t lockCount,recursion;Handle owner,semaphore;uint64_t spin;};
static_assert(sizeof(CriticalSection)==40);
struct ProcessInformation {Handle process,thread;Dword processId,threadId;};
struct StartupInfoA {Dword cb;char* reserved;char* desktop;char* title;Dword x,y,xSize,ySize,xChars,yChars,fill,flags;uint16_t show,reservedBytes;uint8_t* reservedData;Handle input,output,error;};
static_assert(sizeof(ProcessInformation)==24 && sizeof(StartupInfoA)==104);
extern "C" {
RTAPI Bool QueryPerformanceCounter(int64_t*);RTAPI Bool QueryPerformanceFrequency(int64_t*);
RTAPI uint64_t GetTickCount64();RTAPI Dword GetTickCount();RTAPI void GetSystemTimeAsFileTime(FileTime*);
RTAPI Bool QueryUnbiasedInterruptTime(uint64_t*);RTAPI void Sleep(Dword);
RTAPI void* VirtualAlloc(void*,uint64_t,Dword,Dword);RTAPI Bool VirtualFree(void*,uint64_t,Dword);
RTAPI Bool VirtualProtect(void*,uint64_t,Dword,Dword*);RTAPI uint64_t VirtualQuery(const void*,guardian::memory::Information*,uint64_t);
RTAPI Handle LoadLibraryA(const char*);RTAPI Bool FreeLibrary(Handle);RTAPI Procedure GetProcAddress(Handle,const char*);
#ifdef GUARDIAN_SCALABLE_PE
RTAPI Handle GetModuleHandleA(const char*);
RTAPI Handle FindResourceA(Handle,const char*,const char*);RTAPI Handle FindResourceExA(Handle,const char*,const char*,uint16_t);
RTAPI Handle LoadResource(Handle,Handle);RTAPI void* LockResource(Handle);RTAPI Dword SizeofResource(Handle,Handle);
RTAPI Handle CreateActCtxA(const ActCtxA*);RTAPI Bool ActivateActCtx(Handle,uint64_t*);RTAPI Bool DeactivateActCtx(Dword,uint64_t);RTAPI void ReleaseActCtx(Handle);
#endif
RTAPI void RaiseException(Dword,Dword,Dword,const uint64_t*);
RTAPI __declspec(noreturn) void RtlUnwindEx(void*,void*,guardian::exceptions::Record*,void*,guardian::exceptions::Context*,void*);
RTAPI uint16_t RtlCaptureStackBackTrace(Dword,Dword,void**,Dword*);
RTAPI uint16_t GuardianCaptureProbe(Dword,void**,Dword*);
RTAPI Dword GetLastError();RTAPI void SetLastError(Dword);
RTAPI Dword GetCurrentProcessId();RTAPI Dword GetCurrentThreadId();
RTAPI Bool GetFileTime(Handle,FileTime*,FileTime*,FileTime*);
RTAPI Bool SetFileTime(Handle,const FileTime*,const FileTime*,const FileTime*);
RTAPI Bool MoveFileA(const char*,const char*);RTAPI Bool DeleteFileA(const char*);
RTAPI Bool GuardianNetworkEcho();
RTAPI Bool GuardianAnalysisInfo(uint32_t*);
RTAPI Bool GuardianCreateService(const char*,const char*,Dword*);RTAPI Bool GuardianStartService(Dword);RTAPI Bool GuardianStopService(Dword);RTAPI Bool GuardianQueryService(Dword,Dword*);
RTAPI Bool GuardianEnvironmentInfo(guardian::environment::Info*);
RTAPI Bool GetDiskFreeSpaceExA(const char*,uint64_t*,uint64_t*,uint64_t*);
RTAPI Bool GetDiskFreeSpaceExW(const uint16_t*,uint64_t*,uint64_t*,uint64_t*);
RTAPI Dword GetSystemFirmwareTable(Dword,Dword,void*,Dword);
RTAPI Handle CreateThread(void*,uint64_t,ThreadStart,void*,Dword,Dword*);
RTAPI Dword ResumeThread(Handle);RTAPI Bool GetExitCodeThread(Handle,Dword*);RTAPI Bool SwitchToThread();
RTAPI Bool CreateProcessA(const char*,char*,void*,void*,Bool,Dword,void*,const char*,StartupInfoA*,ProcessInformation*);
RTAPI Bool GetExitCodeProcess(Handle,Dword*);RTAPI char* GetCommandLineA();
RTAPI __declspec(noreturn) void ExitThread(Dword);RTAPI __declspec(noreturn) void ExitProcess(Dword);
RTAPI Handle GetProcessHeap();RTAPI void* HeapAlloc(Handle,Dword,uint64_t);RTAPI void* HeapReAlloc(Handle,Dword,void*,uint64_t);RTAPI Bool HeapFree(Handle,Dword,void*);
RTAPI Handle GetStdHandle(Dword);
RTAPI Handle CreateFileA(const char*,Dword,Dword,void*,Dword,Dword,Handle);
RTAPI Bool ReadFile(Handle,void*,Dword,Dword*,void*);RTAPI Bool WriteFile(Handle,const void*,Dword,Dword*,void*);
RTAPI Bool SetFilePointerEx(Handle,int64_t,int64_t*,Dword);RTAPI Bool GetFileSizeEx(Handle,int64_t*);RTAPI Bool FlushFileBuffers(Handle);RTAPI Bool CloseHandle(Handle);
RTAPI Dword RegCreateKeyExA(Handle,const char*,Dword,char*,Dword,Dword,void*,Handle*,Dword*);
RTAPI Dword RegOpenKeyExA(Handle,const char*,Dword,Dword,Handle*);
RTAPI Dword RegSetValueExA(Handle,const char*,Dword,Dword,const uint8_t*,Dword);
RTAPI Dword RegQueryValueExA(Handle,const char*,Dword*,Dword*,uint8_t*,Dword*);RTAPI Dword RegCloseKey(Handle);
RTAPI Handle CreateMutexA(void*,Bool,const char*);RTAPI Dword WaitForSingleObject(Handle,Dword);RTAPI Bool ReleaseMutex(Handle);
RTAPI Bool InitializeCriticalSectionEx(CriticalSection*,Dword,Dword);RTAPI void EnterCriticalSection(CriticalSection*);RTAPI Bool TryEnterCriticalSection(CriticalSection*);RTAPI void LeaveCriticalSection(CriticalSection*);RTAPI void DeleteCriticalSection(CriticalSection*);
}
}
