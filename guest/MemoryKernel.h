#pragma once
#include "CpuKernel.h"
void MemoryInitialize(const guardian::abi::BootInfo& boot, void (*check)(bool, uint32_t) noexcept) noexcept;
void MemorySelfTest() noexcept;
void MemoryRecordFault(const TrapFrame& f) noexcept;
void MemoryPublish() noexcept;
extern "C" uint64_t MemoryRunUser(uint64_t mode, uint64_t address, uint64_t value) noexcept;
extern "C" uint8_t MemoryUserStart, MemoryUserEnd;
