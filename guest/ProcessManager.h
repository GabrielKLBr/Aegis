#pragma once
#include "guardian/Processes.h"
#include "MemoryManager.h"
#include "Scheduler.h"
namespace guardian::processes {
enum class State:uint8_t {Empty,Live,Dying};
struct Handle {uint64_t value=0;uint32_t object=0,access=0,attributes=0,reserved=0;};
struct Process {
    uint32_t id=0,generation=0,parent=0,processObject=0,tokenObject=0,committed=0,threadCount=0,exitCode=0;
    State state=State::Empty;Token token{};memory::Space space{};Handle handles[MaxHandles]{};
    uint32_t threads[MaxProcessThreads]{},threadObjects[MaxProcessThreads]{};
    uint64_t self=0,tokenHandle=0;
};
struct Object {
    uint32_t id=0,generation=0,refs=0,handles=0,raw=0,target=0,exitCode=0,session=0;
    Kind kind=Kind::Empty;bool exited=false,global=false,ownership=false;Security security{};Token token{};char name[64]{};
};
struct Statistics {uint64_t processes=0,objects=0,handles=0,references=0,committed=0,threads=0;bool consistent=true;};
class Manager {
public:
    void Configure(memory::Manager& memory,threads::Scheduler& scheduler) noexcept {memory_=&memory;scheduler_=&scheduler;}
    static bool ValidToken(const Token&) noexcept;
    static bool Restricted(const Token& parent,const Token& child) noexcept;
    static Security DefaultSecurity(const Token&,Kind) noexcept;
    static uint32_t Normalize(Kind,uint32_t desired,uint32_t& access) noexcept;
    static bool Access(const Token&,const Security&,Kind,uint32_t access) noexcept;
    Process* Get(uint32_t pid) noexcept;
    Process* At(uint32_t slot) noexcept {return slot<MaxProcesses && processes_[slot].state!=State::Empty?&processes_[slot]:nullptr;}
    const Process* Get(uint32_t pid) const noexcept {return const_cast<Manager*>(this)->Get(pid);}
    uint32_t CreateRoot(const Token&,uint32_t& pid) noexcept;
    uint32_t CreateChildProcess(uint32_t parent,const Token&,const uint64_t* inherit,uint32_t count,uint32_t& pid) noexcept;
    uint32_t Kill(uint32_t pid,uint32_t status) noexcept;
    bool Reap(uint32_t pid) noexcept; // Must be called after switching away from its CR3.
    uint32_t Attach(uint32_t pid,uint32_t tid,uint64_t& handle) noexcept;
    uint32_t ThreadExit(uint32_t tid,uint32_t code) noexcept;
    bool ReapThread(uint32_t tid) noexcept; // Kernel CR3 active; user stack/TEB released by caller.
    uint32_t Owner(uint32_t tid) const noexcept;
    uint32_t CreateObject(uint32_t pid,Kind,const char* name,const Security*,uint32_t desired,uint32_t attributes,uint64_t& handle,uint32_t initial=0,uint32_t maximum=1) noexcept;
    uint32_t Open(uint32_t pid,Kind,const char* name,uint32_t desired,uint64_t& handle) noexcept;
    uint32_t OpenProcess(uint32_t caller,uint32_t target,uint32_t desired,uint64_t& handle) noexcept;
    uint32_t OpenThread(uint32_t caller,uint32_t target,uint32_t desired,uint64_t& handle) noexcept;
    uint32_t Close(uint32_t pid,uint64_t handle) noexcept;
    uint32_t Flags(uint32_t pid,uint64_t handle,uint32_t flags) noexcept;
    uint32_t DuplicateHandle(uint32_t source,uint64_t handle,uint64_t targetProcess,uint32_t desired,uint32_t attributes,uint64_t& output) noexcept;
#ifdef GUARDIAN_NATIVE_NT
    uint32_t CloneHandle(uint32_t,uint64_t,uint64_t&) noexcept;
#endif
    uint32_t QueryHandle(uint32_t pid,uint64_t handle,Information&) noexcept;
    uint32_t QuerySecurity(uint32_t pid,uint64_t handle,Security&) noexcept;
    uint32_t SetDacl(uint32_t pid,uint64_t handle,const Security&) noexcept;
    uint32_t QueryToken(uint32_t pid,uint64_t handle,Token&) noexcept;
    uint32_t WaitHandles(uint32_t pid,uint32_t tid,const uint64_t* handles,uint32_t count,bool all,uint64_t deadline,bool alertable=false) noexcept;
    uint32_t Signal(uint32_t pid,uint64_t handle,uint32_t amount=1) noexcept;
    bool OwnsMutex(uint32_t pid,uint64_t handle,uint32_t tid) noexcept;
    uint32_t Unlock(uint32_t pid,uint64_t handle,uint32_t tid,bool shared=false) noexcept;
    uint32_t ArmTimer(uint32_t pid,uint64_t handle,uint64_t due,uint64_t period=0) noexcept;
    void Poll() noexcept;
    uint32_t Resolve(uint32_t pid,uint64_t handle,Kind kind,uint32_t required,Object*& out) noexcept;
    uint32_t Reference(uint32_t pid,uint64_t handle,uint32_t required,uint32_t& object) noexcept;
    bool Dereference(uint32_t object) noexcept;
    uint32_t Reserve(uint32_t pid,uint64_t& base,uint64_t bytes,uint32_t protection) noexcept;
    uint32_t Commit(uint32_t pid,uint64_t base,uint64_t bytes,uint32_t protection) noexcept;
    uint32_t Release(uint32_t pid,uint64_t base) noexcept;
    uint32_t CopyProcess(uint32_t caller,uint64_t process,uint64_t remote,uint64_t local,uint32_t bytes,bool write) noexcept;
    uint32_t TerminateProcess(uint32_t caller,uint64_t process,uint32_t status) noexcept;
    uint64_t Translate(uint32_t pid,uint64_t address,bool write=false) noexcept;
    Statistics Counts() const noexcept;
    void Budgets(int64_t objects=-1,int64_t handles=-1) noexcept {objectBudget_=objects;handleBudget_=handles;}
private:
    uint32_t ThreadExitRaw(uint32_t,uint32_t) noexcept;
    uint32_t Create(const Token&,uint32_t parent,const uint64_t*,uint32_t,uint32_t&) noexcept;
    Object* ObjectById(uint32_t id) noexcept;
    uint32_t NewObject(Kind,const Security&,uint32_t raw,uint32_t target) noexcept;
    uint32_t Insert(Process&,uint32_t object,uint32_t access,uint32_t attributes,uint64_t& out,uint64_t value=0) noexcept;
    Handle* Lookup(Process&,uint64_t) noexcept;
    bool Name(const Token&,const char*,bool& global,char* leaf) const noexcept;
    Object* FindName(const Token&,const char*) noexcept;
    void DropHandle(Handle&) noexcept;
    struct Waiting {uint32_t tid=0,pid=0,count=0;uint32_t objects[threads::MaxWait]{};};
    Process processes_[MaxProcesses]{};Object objects_[MaxObjects]{};Waiting waits_[threads::MaxThreads]{};
    memory::Manager* memory_=nullptr;threads::Scheduler* scheduler_=nullptr;
    uint64_t nextHandle_=0x100;int64_t objectBudget_=-1,handleBudget_=-1;bool consistent_=true;
    uint8_t scratch_[4096]{};
};
uint64_t ProcessChecks(Manager&,memory::Manager&,threads::Scheduler&,void (*check)(bool,uint32_t) noexcept) noexcept;
}
