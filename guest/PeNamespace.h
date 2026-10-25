#pragma once
#include "PeLoader.h"
#include "guardian/PeVolume.h"
#ifdef GUARDIAN_WIN32_NATIVE
#include "ApiSet.h"
#endif
namespace guardian::pe {
class Catalog {
public:
    bool Open(const uint8_t*,uint32_t,uint32_t profile,Source*,uint32_t& count,const char* root=nullptr) noexcept;
    Namespace Resolver() noexcept;
    uint32_t Find(const char*,const char* importer=nullptr) const noexcept;
    bool Manifest(const uint8_t*,uint32_t,uint32_t& mask,uint32_t& mode) const noexcept;
    bool DefaultManifest(const uint8_t*,uint32_t) noexcept;
    uint64_t CreateContext(const char*) noexcept;
    bool Activate(uint32_t tid,uint64_t handle,uint64_t& cookie) noexcept;
    bool Deactivate(uint32_t tid,uint64_t cookie) noexcept;
    bool ReleaseContext(uint64_t handle) noexcept;
    void ThreadExit(uint32_t tid) noexcept;
    void CurrentThread(uint32_t tid) noexcept {tid_=tid;}
    uint32_t VersionMode() const noexcept {return versionMode_;}
    const char* RootName() const noexcept {return sourceCount_?sources_[0].name:nullptr;}
    const uint8_t* File(const char*,uint32_t& bytes) const noexcept;
private:
    uint32_t FindDepth(const char*,const char*,uint32_t) const noexcept;
#ifdef GUARDIAN_WIN32_NATIVE
    apiset::Namespace schema_;
#endif
    uint32_t ActiveMask() const noexcept;
    uint32_t Exact(const char*) const noexcept;
    struct Context {uint64_t id=0;uint32_t mask=0,references=0;bool released=false;};
    struct Stack {uint32_t tid=0,depth=0;uint64_t handles[8]{},cookies[8]{};};
    const uint8_t* data_=nullptr;const pevolume::Header* header_=nullptr;
    Source* sources_=nullptr;uint32_t sourceCount_=0,defaultMask_=0,versionMode_=0,tid_=0;
    uint64_t nextContext_=1,nextCookie_=1;Context contexts_[8]{};Stack stacks_[16]{};
};
}
