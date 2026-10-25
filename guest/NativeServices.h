#pragma once
#include "guardian/NativeNt.h"
#include "ProcessManager.h"
#include "Volume.h"
#ifdef GUARDIAN_DESKTOP_NATIVE
#include "guardian/Exception.h"
#endif
namespace guardian::native {
struct Bridge {
    void* context=nullptr;
    bool (*executable)(void*,uint64_t) noexcept=nullptr;
    uint32_t (*thread)(void*,uint64_t,uint64_t,bool,uint32_t&,uint64_t&) noexcept=nullptr;
    uint32_t (*exit)(void*,uint32_t,bool) noexcept=nullptr;
    uint32_t (*extraClose)(void*,uint64_t) noexcept=nullptr;
#ifdef GUARDIAN_DESKTOP_NATIVE
    uint32_t (*threadContext)(void*,uint32_t,exceptions::Context&,bool) noexcept=nullptr;
    bool (*catalog)(void*,const char*,uint64_t,void*,uint32_t,uint32_t&,uint32_t&) noexcept=nullptr;
#endif
};
class Services {
public:
    bool Initialize(memory::Manager&,processes::Manager&,threads::Scheduler&,uint32_t pid,nt::Target,uint64_t peb,Bridge,storage::Volume* shared=nullptr) noexcept;
    uint32_t Execute(Op,const uint64_t*,uint32_t tid,uint64_t teb,uint64_t now,uint64_t utc,bool inApc=false) noexcept;
    bool Read(uint64_t,void*,uint32_t) noexcept;
    bool Write(uint64_t,const void*,uint32_t) noexcept;
    bool Probe(uint64_t,uint32_t,bool) noexcept;
    bool Exit() noexcept;
    uint64_t Calls() const noexcept {return calls_;}
    uint64_t Unknown() const noexcept {return unknown_;}
    storage::Volume& Files() noexcept {return shared_?*shared_:volume_;}
#ifdef GUARDIAN_WIN32_NATIVE
    uint32_t Win32(uint32_t,uint64_t*) noexcept;
#ifdef GUARDIAN_DESKTOP_NATIVE
    uint32_t DesktopThread(uint32_t,uint64_t*) noexcept;
#endif
#endif
private:
    struct Range {uint64_t base=0,bytes=0;int32_t section=-1;};
    struct Section {uint32_t references=0,access=0,pages=0,protection=0;uint64_t data[64]{};
#ifdef GUARDIAN_WIN32_NATIVE
        uint64_t file=0;uint32_t length=0;
#endif
    };
    struct Handle {uint64_t id=0,raw=0,position=0;uint32_t access=0,kind=0,index=0;char path[128]{};};
    Handle* Find(uint64_t) noexcept;
    Handle* New(uint32_t,uint32_t,uint64_t=0,uint32_t=0) noexcept;
    uint32_t Close(uint64_t) noexcept;
    void DropSection(uint32_t) noexcept;
#ifdef GUARDIAN_WIN32_NATIVE
    uint32_t FlushSection(Section&) noexcept;
#endif
    uint32_t Vm(Op,const uint64_t*) noexcept;
    uint32_t Sections(Op,const uint64_t*) noexcept;
    uint32_t Sync(Op,const uint64_t*,uint32_t,uint64_t,uint64_t,bool) noexcept;
    uint32_t Information(Op,const uint64_t*,uint32_t,uint64_t) noexcept;
    uint32_t Files(Op,const uint64_t*) noexcept;
    bool Text(uint64_t,char*,uint32_t) noexcept;
    uint32_t ObjectName(uint64_t,char*,uint32_t,bool=false) noexcept;
    bool Deadline(uint64_t,uint64_t,uint64_t,uint64_t&) noexcept;
    uint32_t Process(uint64_t,uint32_t&,uint32_t=0) noexcept;
    bool Output(uint64_t,uint64_t) noexcept;
    static bool Block(void*,io::Op,uint64_t,uint8_t*) noexcept;
    memory::Manager* memory_=nullptr;processes::Manager* processes_=nullptr;threads::Scheduler* scheduler_=nullptr;
    uint32_t pid_=0;nt::Target target_{};uint64_t peb_=0,nextHandle_=0x4E54000000000100ull,utc_=0,calls_=0,unknown_=0;
    Bridge bridge_{};Range ranges_[32]{};Section sections_[8]{};Handle handles_[32]{};
    storage::Volume* shared_=nullptr;bool cleanupOk_=true;storage::Volume volume_;uint8_t disk_[65536]{};
};
}
