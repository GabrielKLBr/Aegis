#pragma once
#include "guardian/Runtime.h"
#include "guardian/StorageQuota.h"
#include "guardian/HardwareProjection.h"
namespace guardian::runtime {
using Storage=uint32_t (*)(void*,uint32_t,const processes::Token&,storage::Command&) noexcept;
using ConsoleWrite=uint32_t (*)(void*,uint32_t,const uint8_t*,uint32_t,uint32_t&) noexcept;
class Services {
public:
    void Configure(void* context,Storage storage,ConsoleWrite console) noexcept {context_=context;storage_=storage;console_=console;}
    void ConfigureHardware(const hardware::Info* info) noexcept {hardware_=info;}
    uint32_t Execute(uint32_t pid,const processes::Token&,Command&) noexcept;
    bool Exit(uint32_t pid,const processes::Token&) noexcept;
    uint32_t Count(uint32_t pid=0) const noexcept;
private:
    struct Handle {uint64_t id=0,raw=0,position=0;uint32_t owner=0,access=0,kind=0;char path[128]{};};
    Handle* Find(uint32_t,uint64_t,uint32_t kind) noexcept;
    Handle* Free(uint32_t) noexcept;
    uint32_t KeyPath(uint32_t,const Command&,char*) noexcept;
    uint32_t KeyAccess(uint32_t,const processes::Token&,const char*,uint32_t) noexcept;
    uint32_t FileOpen(uint32_t,const processes::Token&,Command&) noexcept;
    uint32_t KeyOpen(uint32_t,const processes::Token&,Command&) noexcept;
    uint32_t Call(uint32_t,const processes::Token&,storage::Command&) noexcept;
    Handle handles_[MaxHandles]{};uint64_t next_=FirstHandle;
    void* context_=nullptr;Storage storage_=nullptr;ConsoleWrite console_=nullptr;
    const hardware::Info* hardware_=nullptr;
};
}
