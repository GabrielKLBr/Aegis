#pragma once
#include "ThreadKernel.h"
#include <intrin.h>
// Shared by the thread and process images: a context switch must restore the
// same state regardless of whether CR3 also changes.
struct alignas(16) ExecutionContext {
    TrapFrame frame{};uint8_t fx[512]{};uint64_t fs=0,gs=0;uint16_t segments[4]{0x23,0x23,0,0};uint64_t reserved=0;
};
static_assert(sizeof(ExecutionContext)==720 && offsetof(ExecutionContext,fx)%16==0);
inline void SeedContext(ExecutionContext& c) noexcept {c.fx[0]=0x7F;c.fx[1]=3;c.fx[24]=0x80;c.fx[25]=0x1F;c.fx[28]=0xFF;c.fx[29]=0xFF;}
inline void SaveContext(ExecutionContext& c,const TrapFrame& frame,const void* fx) noexcept {
    c.frame=frame;c.frame.rflags=(c.frame.rflags&0xCD5)|0x202;
    auto* from=static_cast<const uint8_t*>(fx);for(uint32_t i=0;i<512;++i)c.fx[i]=from[i];
    c.fs=__readmsr(0xC0000100);c.gs=__readmsr(0xC0000102);ThreadSaveSegments(c.segments);
}
inline void RestoreContext(const ExecutionContext& c,TrapFrame& frame,void* fx) noexcept {
    frame=c.frame;auto* to=static_cast<uint8_t*>(fx);for(uint32_t i=0;i<512;++i)to[i]=c.fx[i];
    const auto kernelGs=__readmsr(0xC0000101);ThreadRestoreSegments(c.segments);__writemsr(0xC0000101,kernelGs);
    __writemsr(0xC0000100,c.fs);__writemsr(0xC0000102,c.gs);
}
