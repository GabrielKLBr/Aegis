#pragma once
#include "guardian/Io.h"
#include "guardian/StorageQuota.h"
#include "../state/State.h"
namespace guardian::io {
// Bounded host-side RAM devices. No files, host disk, OS handles or user paths.
class Backend {
public:
    explicit Backend(uint64_t boot) noexcept;
    void Save(av::microvm::state::Writer&) const;
    void Load(av::microvm::state::Reader&);
    const Bus& Enumeration() const noexcept {return bus_;}
    bool Exchange(Packet&) noexcept;
    void Fail(uint32_t device) noexcept {if(device && device<=DeviceCount)failed_[device-1]=true;}
    uint64_t Requests() const noexcept {return sequence_;}
    uint64_t ConsoleSize() const noexcept {return consoleUsed_;}
    uint64_t ChannelBytes() const noexcept {return channelBytes_;}
    uint64_t BlockTransferred() const noexcept {return blockBytes_;}
    const uint8_t* ConsoleData() const noexcept {return console_;}
    void QueryBacking(bool (*query)(void*,guardian::storage::BackingSpace&) noexcept) noexcept {query_=query;}
    void Storage(void* context,bool (*read)(void*,uint64_t,uint8_t*) noexcept,bool (*write)(void*,uint64_t,const uint8_t*) noexcept,bool (*flush)(void*) noexcept,uint32_t (*status)(void*) noexcept=nullptr) noexcept {status_=status;storage_=context;read_=read;write_=write;flush_=flush;bus_.devices[2].features=7;}
private:
    Bus bus_{};uint64_t sequence_=0,consoleUsed_=0,channelBytes_=0,blockBytes_=0;
    bool failed_[DeviceCount]{};
    uint8_t disk_[BlockBytes]{},console_[ConsoleBytes]{},channel_[MaxRequests][MaxTransfer]{};
    uint32_t lengths_[MaxRequests]{},head_=0,count_=0;
    void* storage_=nullptr;
    bool (*query_)(void*,guardian::storage::BackingSpace&) noexcept=nullptr;
    uint32_t (*status_)(void*) noexcept=nullptr;
    bool (*read_)(void*,uint64_t,uint8_t*) noexcept=nullptr;
    bool (*write_)(void*,uint64_t,const uint8_t*) noexcept=nullptr;
    bool (*flush_)(void*) noexcept=nullptr;
};
}
