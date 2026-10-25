#pragma once
#include "guardian/StorageQuota.h"
#include "ProcessManager.h"
namespace guardian::storage {
using Block=bool (*)(void*,io::Op,uint64_t,uint8_t*) noexcept;
struct Handle {uint64_t id=0;uint32_t owner=0,node=0,access=0,share=0;};
struct Watch {uint32_t id=0,generation=0,owner=0,key=0,mask=0,changes=0,event=0;bool subtree=false;};
class Volume {
public:
    uint32_t QueryKind(const processes::Token&,const char*,Kind&) noexcept;
    uint32_t CheckAccess(const processes::Token&,const char*,uint32_t,uint32_t* identity=nullptr) noexcept;
#ifdef GUARDIAN_SECURITY_NATIVE
    uint32_t ProvisionUser(const processes::Token&) noexcept;
    uint32_t PersistRegistry(const processes::Token&,const char*,const char*,const void*,uint32_t,bool remove) noexcept;
#endif
    void Configure(void* context,Block block,threads::Scheduler& scheduler) noexcept {context_=context;block_=block;scheduler_=&scheduler;}
    void ConfigureBacking(bool (*query)(void*,BackingSpace&) noexcept) noexcept {backing_=query;}
    void ConfigureClock(uint64_t (*now)(void*) noexcept) noexcept {clock_=now;}
    bool ConfigureQuota(uint64_t bytes) noexcept;
    Space QuerySpace() const noexcept;
    uint32_t Mount() noexcept;
    uint32_t Unmount() noexcept;
    uint32_t Flush() noexcept;
    uint32_t Execute(uint32_t owner,const processes::Token&,Command&) noexcept;
    void Exit(uint32_t owner) noexcept;
    uint32_t Wait(uint32_t owner,uint32_t watch,uint32_t tid) noexcept;
    uint64_t Sequence() const noexcept {return image_.header.sequence;}
    uint32_t Handles() const noexcept;
    uint32_t Watches() const noexcept;
    static bool Validate(const Image&) noexcept;
private:
    uint32_t ExecuteRaw(uint32_t,const processes::Token&,Command&) noexcept;
    void Describe(uint32_t,const processes::Token&,const Command&,char*,Node*&) noexcept;
    struct Path {uint32_t root=0,count=0;char parts[Depth][NameBytes]{};};
    static bool Parse(const char*,Path&) noexcept;
    static bool Name(const char*,char*,bool empty=false) noexcept;
    static bool Value(ValueType,const uint8_t*,uint32_t) noexcept;
    static bool Equal(const char*,const char*) noexcept;
    Node* Get(uint32_t) noexcept;
    Node* Child(uint32_t,const char*,Kind kind=Kind::Empty) noexcept;
    Node* New(const processes::Token&,uint32_t,Kind,const char*) noexcept;
    Node* Root(const processes::Token&,uint32_t,bool create=false) noexcept;
    uint32_t Resolve(const processes::Token&,const char*,Node*&,bool parent=false,char* leaf=nullptr,bool follow=true) noexcept;
    static bool Access(const processes::Token&,const Node&,uint32_t) noexcept;
    Handle* Find(uint32_t,uint64_t) noexcept;
    uint32_t Open(uint32_t,const processes::Token&,Command&) noexcept;
    uint32_t Close(uint32_t,uint64_t) noexcept;
    uint32_t FileIo(uint32_t,Command&) noexcept;
    uint32_t Registry(uint32_t,const processes::Token&,Command&) noexcept;
    uint32_t WatchKey(uint32_t,const processes::Token&,Command&) noexcept;
    uint32_t WatchResult(uint32_t,Command&,bool close) noexcept;
    void Notify(uint32_t,uint32_t) noexcept;
    void Clear(Node&) noexcept;
    void Garbage() noexcept;
    bool Shared(uint32_t,uint32_t,uint32_t) const noexcept;
    uint32_t Unlink(const processes::Token&,const char*) noexcept;
    void Format() noexcept;
    bool BackingReady(uint64_t records) const noexcept;
    uint32_t BlockError() const noexcept;
    bool (*backing_)(void*,BackingSpace&) noexcept=nullptr;
    uint32_t Times(uint32_t,Command&) noexcept;
    void Touch(Node&,bool write,bool created=false) noexcept;
    void ClearTimes(uint32_t slot) noexcept;
    uint64_t (*clock_)(void*) noexcept=nullptr;
    Image image_{},scratch_{};Handle handles_[MaxHandles]{};Watch watches_[MaxWatches]{};
#ifdef GUARDIAN_SECURITY_NATIVE
    Image transaction_{};
#endif
    void* context_=nullptr;Block block_=nullptr;threads::Scheduler* scheduler_=nullptr;
    uint64_t allocationLimit_=VolumeCapacity;
    uint64_t nextHandle_=4;uint32_t active_=1;bool mounted_=false,dirty_=false,formatting_=false;
};
uint64_t StorageChecks(Volume&,Volume&,threads::Scheduler&,void (*)(bool,uint32_t) noexcept) noexcept;
}
