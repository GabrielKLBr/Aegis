#pragma once
#include "ProcessManager.h"
#include "ExecutionContext.h"
#include "guardian/Io.h"
void IoInitialize(guardian::processes::Manager&,guardian::threads::Scheduler&,void (*)(bool,uint32_t) noexcept) noexcept;
void IoBegin(uint32_t owner) noexcept;
void IoTick() noexcept;
void IoExit(uint32_t owner) noexcept;
bool IoDispatch(uint32_t pid,uint32_t tid,const TrapFrame&,ExecutionContext&,uint32_t& status) noexcept;
void IoFinish() noexcept;
void IoPublish() noexcept;
bool IoBlockSync(guardian::io::Op,uint64_t offset,uint8_t* data) noexcept;
uint32_t IoConsoleSync(uint32_t owner,const uint8_t*,uint32_t,uint32_t&) noexcept;
