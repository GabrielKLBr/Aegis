#pragma once
#include "CpuKernel.h"
void ProcessInitialize(const guardian::abi::BootInfo&,void (*check)(bool,uint32_t) noexcept) noexcept;
void ProcessSelfTest() noexcept;
void ProcessPublish() noexcept;
bool ProcessActive() noexcept;
bool ProcessTrap(TrapFrame&,void* fx,uint64_t now,bool timer) noexcept;
extern "C" void ProcessRun(uint64_t rip,uint64_t rsp) noexcept;
extern "C" uint8_t ProcessUserStart,ProcessUserEnd,ProcessBootstrap,ProcessIdle;
