#pragma once
#include "guardian/PeLimits.h"
#include <stdint.h>

namespace guardian::unwind {
// Virtual restoration only: never writes the inspected stack, resumes code,
// invokes handlers or uses addresses from the host operating system.
enum class Status:uint32_t {Ok,End,Invalid,Access,Unsupported,Limit};
struct Context {uint64_t gpr[16]{},rip=0;uint8_t xmm[16][16]{};};
struct Stack {uint64_t low,high;}; // committed bounds from the thread owner
struct Image {uint64_t base;uint32_t size,table,count;};
// Candidate metadata only; these addresses must never be called in CPL0.
enum class HandlerType:uint32_t {Exception=1,Termination=2};
struct Handler {
    uint64_t routine=0,data=0,establisher=0,imageBase=0;
    uint64_t functionBegin=0,functionEnd=0,unwindInfo=0;
};
struct Backend {
    bool (*image)(void*,uint64_t,Image&) noexcept;
    bool (*read)(void*,uint64_t,void*,uint32_t) noexcept;
    // Required only by InspectHandler. Consult sealed executable mappings.
    bool (*executable)(void*,uint64_t) noexcept=nullptr;
};
inline constexpr uint32_t MaxFrames=16,MaxSkip=32,MaxChain=32;
// On any result other than Ok, context remains byte-for-byte unchanged.
Status Step(const Backend&,void*,const Stack&,Context&) noexcept;
// Restores one virtual caller and describes the matching v1 language handler.
// A leaf, prolog, epilog or unmatched kind yields a zero Handler on success.
// Context AND handler remain unchanged on failure or End. No handler is run.
// Step/Capture retain their original rejection of handler-bearing records;
// accepting such images in the loader still requires a user-mode dispatcher.
Status InspectHandler(const Backend&,void*,const Stack&,HandlerType,Context&,Handler&) noexcept;
// Records callers after stepping out of the initial context. A normal image
// boundary terminates the walk. Any malformed frame discards the entire trace.
Status Capture(const Backend&,void*,const Stack&,const Context&,uint32_t skip,
               uint32_t count,uint64_t* frames,uint32_t& captured,uint32_t& hash,bool handlers=false) noexcept;
}
