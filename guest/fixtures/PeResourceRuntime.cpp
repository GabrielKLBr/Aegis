#define GUARDIAN_RUNTIME_BUILD
#include "PeRuntimeApi.h"
#include "guardian/Runtime.h"
extern "C" uint32_t GuardianRuntimeCall(guardian::runtime::Command*) noexcept;
namespace guardian::win {
namespace {
bool Copy(char* out,const char* in){if(!in)return false;for(unsigned i=0;i<128;++i){out[i]=in[i];if(!in[i])return true;}return false;}
Bool Call(runtime::Command& c){const auto status=GuardianRuntimeCall(&c);if(status)SetLastError(status==0xC0000034?1814u:87u);return !status;}
struct Resource {Handle id=nullptr,module=nullptr;};Resource cache[32]{};
}
extern "C" Handle GetModuleHandleA(const char* name){runtime::Command c;c.op=runtime::Op::ModuleHandle;if(name&&!Copy(c.path,name)){SetLastError(87);return nullptr;}return Call(c)?reinterpret_cast<Handle>(c.handle):nullptr;}
extern "C" Handle FindResourceExA(Handle module,const char* type,const char* name,uint16_t language){
    const auto t=reinterpret_cast<uint64_t>(type),n=reinterpret_cast<uint64_t>(name);if(t>65535||n>65535){SetLastError(50);return nullptr;}
    runtime::Command c;c.op=runtime::Op::ResourceFind;c.handle=reinterpret_cast<uint64_t>(module);c.access=static_cast<uint32_t>(t);c.length=static_cast<uint32_t>(n);c.flags=language?language:UINT32_MAX;
    return Call(c)?reinterpret_cast<Handle>(c.argument):nullptr;
}
extern "C" Handle FindResourceA(Handle module,const char* name,const char* type){return FindResourceExA(module,type,name,0);}
extern "C" Handle LoadResource(Handle module,Handle resource){runtime::Command c;c.op=runtime::Op::ResourceInfo;c.handle=reinterpret_cast<uint64_t>(module);c.argument=reinterpret_cast<uint64_t>(resource);
    if(!Call(c))return nullptr;for(auto& r:cache)if(!r.id||r.id==resource){r={resource,module};return resource;}SetLastError(8);return nullptr;}
extern "C" void* LockResource(Handle resource){for(auto& r:cache)if(r.id==resource){runtime::Command c;c.op=runtime::Op::ResourceInfo;c.handle=reinterpret_cast<uint64_t>(r.module);c.argument=reinterpret_cast<uint64_t>(resource);
    if(Call(c))return reinterpret_cast<void*>(c.argument);r={};return nullptr;}SetLastError(6);return nullptr;}
extern "C" Dword SizeofResource(Handle module,Handle resource){runtime::Command c;c.op=runtime::Op::ResourceInfo;c.handle=reinterpret_cast<uint64_t>(module);c.argument=reinterpret_cast<uint64_t>(resource);return Call(c)?c.transferred:0;}
extern "C" Handle CreateActCtxA(const ActCtxA* info){if(!info||info->cb!=sizeof(*info)||info->flags||info->architecture||info->language||info->directory||info->resource||info->application||info->module){SetLastError(87);return reinterpret_cast<Handle>(UINT64_MAX);}
    runtime::Command c;c.op=runtime::Op::ContextCreate;if(!Copy(c.path,info->source)||!Call(c))return reinterpret_cast<Handle>(UINT64_MAX);return reinterpret_cast<Handle>(c.handle);}
extern "C" Bool ActivateActCtx(Handle context,uint64_t* cookie){if(!cookie){SetLastError(87);return 0;}runtime::Command c;c.op=runtime::Op::ContextActivate;c.handle=reinterpret_cast<uint64_t>(context);if(!Call(c))return 0;*cookie=c.argument;return 1;}
extern "C" Bool DeactivateActCtx(Dword flags,uint64_t cookie){if(flags){SetLastError(87);return 0;}runtime::Command c;c.op=runtime::Op::ContextDeactivate;c.argument=cookie;return Call(c);}
extern "C" void ReleaseActCtx(Handle context){runtime::Command c;c.op=runtime::Op::ContextRelease;c.handle=reinterpret_cast<uint64_t>(context);Call(c);}
}
