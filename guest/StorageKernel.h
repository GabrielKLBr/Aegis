#pragma once
#include "Volume.h"
#include "ExecutionContext.h"
void StorageInitialize(guardian::processes::Manager&,guardian::threads::Scheduler&,void (*)(bool,uint32_t) noexcept) noexcept;
void StorageBegin(uint32_t owner) noexcept;
void StorageExit(uint32_t owner) noexcept;
bool StorageDispatch(uint32_t pid,uint32_t tid,const TrapFrame&,ExecutionContext&,uint32_t& status) noexcept;
void StorageFinish() noexcept;
void StoragePublish() noexcept;
uint32_t StorageExecute(uint32_t pid,guardian::storage::Command&) noexcept;
