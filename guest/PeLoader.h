#pragma once
#include <stdint.h>
#include "guardian/PeLimits.h"

namespace guardian::pe {
// Bounded PE32+ loader. All parsing and fixups run in the guest, without Win32,
// STL, host function addresses or executable patches. Mapping is transactional.
inline constexpr uint32_t MaxModules=peprofile::Modules,MaxSections=peprofile::Sections,MaxImageBytes=peprofile::ImageBytes;
enum class Error:uint32_t {Ok,InvalidImage,Unsupported,MissingModule,MissingSymbol,NoMemory,Access,Limit,Cycle,InvalidState};
enum class Protection:uint32_t {None,Read,ReadWrite,ExecuteRead};
struct Source {const char* name;const uint8_t* data;uint32_t bytes;};
struct Backend {
    // Map must either provide zeroed, writable, non-executable pages for the
    // entire image, or fail without allocating anything. Unmap removes it all.
    bool (*map)(void*,uint64_t,uint32_t) noexcept;
    bool (*read)(void*,uint64_t,void*,uint32_t) noexcept;
    bool (*write)(void*,uint64_t,const void*,uint32_t) noexcept;
    bool (*protect)(void*,uint64_t,uint32_t,Protection) noexcept;
    bool (*unmap)(void*,uint64_t) noexcept;
};
struct Namespace {
    void* context=nullptr;
    uint32_t (*find)(void*,const char*,const char*) noexcept=nullptr;
    bool (*manifest)(void*,const uint8_t*,uint32_t) noexcept=nullptr;
};
struct Section {uint32_t rva,bytes,raw,rawBytes,flags;};
struct Directory {uint32_t rva,bytes;};
struct Module {
    uint64_t base=0,preferred=0,entry=0;
    uint32_t size=0,headers=0,sectionCount=0,state=0,generation=0;
    Section sections[MaxSections]{};Directory directories[16]{};
    uint64_t tlsTemplate=0,tlsIndexAddress=0,tlsCallbacks[8]{};
    uint32_t tlsBytes=0,tlsZeroBytes=0,tlsCallbackCount=0,tlsIndex=0;
    uint32_t relocated=0,imports=0,functions=0,dependencies=0;
    bool dll=false,mapped=false,x86=false;
};
class Loader {
public:
    bool Configure(const Backend&,void*,const Source*,uint32_t,uint64_t arena,bool handlers=false) noexcept;
    bool Load(const char* name,uint32_t& index) noexcept;
    bool UnloadAll() noexcept;
    void ConfigureAudit(uint32_t pid,uint32_t user) noexcept {auditPid_=pid;auditUser_=user;}
    bool Acquire(const char*,uint32_t& index,uint32_t& added) noexcept;
    bool Release(uint32_t index,uint32_t& removable) noexcept;
    bool Remove(uint32_t mask) noexcept;
    bool Resolve(uint32_t index,const char*,uint32_t ordinal,uint64_t&) noexcept;
    void ConfigureNamespace(const Namespace& ns) noexcept {namespace_=ns;}
    bool Resource(uint32_t index,uint32_t type,uint32_t id,uint32_t language,uint64_t& address,uint32_t& bytes) noexcept;
    bool CancelResolution(uint32_t newlyMapped) noexcept;
    bool InitializationOrder(uint32_t mask,uint32_t* order,uint32_t& count) const noexcept;
    uint32_t References(uint32_t i) const noexcept {return i<count_?references_[i]:0;}
    uint32_t MappedMask() const noexcept;
    const Module* Get(uint32_t i) const noexcept {return i<count_ && modules_[i].state==2?&modules_[i]:nullptr;}
    Error Failure() const noexcept {return error_;}
#ifdef GUARDIAN_NATIVE_NT
    uint32_t FailureStage() const noexcept {return diagnosticStage_;}
    uint32_t FailureModule() const noexcept {return diagnosticModule_;}
    uint32_t FailureDetail() const noexcept {return diagnosticDetail_;}
#endif
    // Only the RX sections of successfully sealed modules are entry candidates.
    bool Executable(uint64_t address) const noexcept;
private:
    uint32_t auditPid_=0,auditUser_=0;
    bool LoadRaw(const char*,uint32_t&) noexcept;
    bool AcquireRaw(const char*,uint32_t&,uint32_t&) noexcept;
    bool Fail(Error e) noexcept {error_=e;return false;}
    bool LoadOne(uint32_t) noexcept;
    bool Headers(uint32_t) noexcept;
    bool Relocate(Module&) noexcept;
    bool Imports(Module&) noexcept;
    bool Exports(Module&) noexcept;
    bool Tls(Module&,uint32_t) noexcept;
    bool Exceptions(Module&) noexcept;
    bool Seal(Module&) noexcept;
    bool Symbol(Module&,const char*,uint32_t,uint64_t&,uint32_t depth=0) noexcept;
    bool Thunks(Module&,uint32_t,uint32_t,uint32_t,uint32_t) noexcept;
    bool DelayImports(Module&) noexcept;
    bool Resources(Module&,bool activate) noexcept;
    bool ResourceEntry(Module&,uint32_t,uint32_t,uint32_t,uint32_t&,uint32_t&) noexcept;
    bool ResourceTree(Module&,uint32_t,uint32_t,uint32_t&) noexcept;
    bool Span(const Module&,uint32_t,uint32_t,uint32_t flags=0x40000000) const noexcept;
    bool Read(const Module&,uint32_t,void*,uint32_t) noexcept;
    bool Write(const Module&,uint32_t,const void*,uint32_t) noexcept;
    bool String(const Module&,uint32_t,char*,uint32_t) noexcept;
    uint32_t Find(const char*,const char* importer=nullptr) const noexcept;
    bool Rollback(uint32_t keep) noexcept;
    uint32_t Reachable() const noexcept;
    Backend backend_{};void* context_=nullptr;const Source* sources_=nullptr;
    uint32_t count_=0;uint64_t arena_=0;Error error_=Error::InvalidState;Module modules_[MaxModules]{};
    uint32_t pinned_=0,references_[MaxModules]{};bool handlers_=false;
    Namespace namespace_{};
    uint32_t nextGeneration_=1;
#ifdef GUARDIAN_NATIVE_NT
    uint32_t diagnosticStage_=0,diagnosticModule_=0,diagnosticDetail_=0;
#endif
#ifdef GUARDIAN_SCALABLE_PE
    uint32_t relocationTargets_[peprofile::Relocations*2]{};
    uint8_t manifestBytes_[4096]{};
#endif
};
}
