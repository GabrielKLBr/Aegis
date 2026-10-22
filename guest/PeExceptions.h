#pragma once
#include "ExceptionDispatch.h"
#include "Placement.h"
#include "ExecutionContext.h"
struct PeExceptionEnvironment {
    guardian::unwind::Backend api{};void* context=nullptr;
    bool (*write)(void*,uint64_t,const void*,uint32_t) noexcept=nullptr;
    guardian::unwind::Stack stack{};uint64_t entry=0;
};
bool PeExceptionFault(uint32_t pid,uint32_t tid,ExecutionContext&,const PeExceptionEnvironment&) noexcept;
bool PeExceptionCall(uint32_t pid,uint32_t tid,const TrapFrame&,ExecutionContext&,const PeExceptionEnvironment&) noexcept;
bool PeExceptionBusy(uint32_t pid) noexcept;
void PeExceptionExit(uint32_t pid) noexcept;
uint32_t PeExceptionCode(uint64_t vector) noexcept;

uint32_t PeExceptionFailure(uint32_t,uint32_t) noexcept;

inline constexpr uint64_t PeExceptionScratch=0x110000,PeExceptionScratchBytes=0x20000;
void PeExceptionsInitialize() noexcept;
