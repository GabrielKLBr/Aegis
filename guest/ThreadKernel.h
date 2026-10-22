#pragma once
#include "CpuKernel.h"
void ThreadInitialize(const guardian::abi::BootInfo&,void (*check)(bool,uint32_t) noexcept) noexcept;
void ThreadSelfTest() noexcept;
void ThreadPublish() noexcept;
bool ThreadActive() noexcept;
bool ThreadTrap(TrapFrame&,void* fx,uint64_t now,bool timer) noexcept;
extern "C" void ThreadRun(uint64_t rip,uint64_t rsp) noexcept;
extern "C" void ThreadSaveSegments(void* selectors) noexcept;
extern "C" void ThreadRestoreSegments(const void* selectors) noexcept;
extern "C" uint8_t ThreadUserStart,ThreadUserEnd,ThreadIdle,ThreadBootstrap,ThreadApcDispatcher,ThreadApcCallback;
