#pragma once
#include <stdint.h>
#ifdef GUARDIAN_WIN32_NATIVE
bool Win32StorageFinish() noexcept;
#ifdef GUARDIAN_SECURITY_NATIVE
uint32_t ProcessCreatePeService(uint32_t,const char*,uint32_t&,uint64_t&) noexcept;
#endif
#endif
#include "ProcessManager.h"
#ifdef GUARDIAN_DESKTOP_NATIVE
#include "guardian/Exception.h"
uint32_t ProcessDesktopContext(uint32_t,uint32_t,guardian::exceptions::Context&,bool) noexcept;
#endif
uint32_t PeMappingChecks(guardian::processes::Manager&,guardian::memory::Manager&,void (*)(bool,uint32_t) noexcept) noexcept;
bool PePrepareProcess(guardian::processes::Manager&,guardian::memory::Manager&,uint32_t pid,uint64_t& startup,const char* childCommand=nullptr,const char* application=nullptr) noexcept;
bool PeOwnsProcess(uint32_t pid) noexcept;
bool PeExecutable(uint32_t pid,uint64_t rip) noexcept;
bool PeExitProcess(uint32_t pid,uint32_t code,bool fault=false) noexcept;
bool PeProcessPassed() noexcept;
bool PeIsRoot(uint32_t pid) noexcept;
bool PeProcessTerminating(uint32_t pid) noexcept;
uint64_t PeInitialTeb(uint32_t pid) noexcept;
void PeThreadGone(uint32_t pid,uint32_t tid) noexcept;
bool PeMarkTerminating(uint32_t pid) noexcept;
extern "C" uint8_t PeProcessBootstrap;
#ifdef GUARDIAN_RUNTIME_KERNEL
struct TrapFrame;struct ExecutionContext;
extern "C" uint8_t PeThreadBootstrap;
#ifdef GUARDIAN_LIFETIME_KERNEL
bool ProcessPeStack(uint32_t pid,uint32_t tid,uint64_t& low,uint64_t& high) noexcept;
extern "C" uint8_t PeThreadExitEntry,PeProcessExitEntry;
uint32_t ProcessRedirectPeExit(uint32_t pid,uint32_t tid,uint32_t code,bool whole) noexcept;
uint32_t ProcessBeginPeExit(uint32_t pid,uint32_t tid,uint32_t code) noexcept;
uint32_t ProcessCreatePeChild(uint32_t parent,const char* name,const char* command,bool suspended,uint32_t& pid,uint32_t& tid,uint64_t& processHandle,uint64_t& threadHandle) noexcept;
#endif
bool PePrepareThread(uint32_t pid,uint32_t tid,uint64_t stack,uint64_t entry,uint64_t argument,uint64_t& teb,uint64_t& startup) noexcept;
uint32_t ProcessCreatePeThread(uint32_t pid,uint64_t entry,uint64_t argument,bool suspended,uint32_t& tid,uint64_t& handle) noexcept;
bool ProcessYieldPeThread() noexcept;
uint32_t ProcessDelayPeThread(uint32_t tid,uint64_t deadline) noexcept;
uint32_t ProcessResumePeThread(uint32_t tid,uint32_t& previous) noexcept;
bool PeRuntimeDispatch(uint32_t pid,uint32_t tid,uint64_t now,const TrapFrame&,ExecutionContext&,uint32_t&) noexcept;
#endif

#ifdef GUARDIAN_COMPLETE_KERNEL
bool PeHandleException(uint32_t,uint32_t,ExecutionContext&) noexcept;
bool PeHandleContinuation(uint32_t,uint32_t,const TrapFrame&,ExecutionContext&) noexcept;
uint32_t ProcessPeTebs(uint32_t,uint32_t*,uint64_t*,uint32_t) noexcept;
#endif

uint32_t ProcessStopPeChild(uint32_t,uint32_t) noexcept;
#ifdef GUARDIAN_NATIVE_NT
bool PeNativePrepare(uint32_t,guardian::threads::Scheduler&) noexcept;
void PeNativeTick(uint64_t) noexcept;
bool PeNativeLegacy(uint32_t,uint64_t) noexcept;
uint32_t ProcessNativeTerminate(uint32_t,uint32_t,uint32_t,bool) noexcept;
bool PeNativeDispatch(uint32_t,uint32_t,uint64_t,const TrapFrame&,uint64_t,uint32_t&,bool=false) noexcept;
#endif
