#define GUARDIAN_RUNTIME_BUILD
#include "PeRuntimeApi.h"
#include "guardian/Runtime.h"
extern "C" uint32_t GuardianRuntimeCall(guardian::runtime::Command*) noexcept;
extern "C" uint32_t GuardianLoaderEnter();
extern "C" uint32_t GuardianLoaderLeave();
namespace guardian::win {
namespace {
using runtime::Command;using runtime::Op;
struct Call {uint64_t function,module,check;};
bool Text(char* out,const char* in){if(!in)return false;for(uint32_t i=0;i<128;++i){out[i]=in[i];if(!in[i])return true;}return false;}
Call Get(const Command& c,uint32_t i){Call call{};for(uint32_t j=0;j<sizeof(call);++j)reinterpret_cast<uint8_t*>(&call)[j]=c.data[i*sizeof(call)+j];return call;}
bool Callback(const Call& call,uint32_t reason){return reinterpret_cast<int (*)(void*,uint32_t,void*)>(call.function)(reinterpret_cast<void*>(call.module),reason,nullptr)!=0;}
}
extern "C" Handle LoadLibraryA(const char* name){
    Command c{};c.op=Op::LibraryLoad;
    if(!Text(c.path,name)){SetLastError(87);return nullptr;}
    if(GuardianLoaderEnter()){SetLastError(5);return nullptr;}
    constexpr uint32_t limit=peprofile::Extended?peprofile::InitCalls:16;
    auto status=GuardianRuntimeCall(&c);uint32_t completed=0;bool ok=!status && c.transferred<=limit;
    if(ok)for(;completed<c.transferred;++completed){const auto call=Get(c,completed);const auto result=Callback(call,1);if(call.check && !result){
#ifdef GUARDIAN_SCALABLE_PE
        Callback(call,0);
#endif
        ok=false;break;}}
    if(!status){if(!ok)while(completed)Callback(Get(c,--completed),0);
        const auto handle=c.handle;c={};c.op=Op::LibraryFinish;c.handle=handle;c.flags=ok?1u:0u;status=GuardianRuntimeCall(&c);}
    const bool unlock=GuardianLoaderLeave()==0;
    if(status || !ok || !unlock){SetLastError(status==0xC0000034?126:1114);return nullptr;}
    return reinterpret_cast<Handle>(c.handle);
}
extern "C" Bool FreeLibrary(Handle module){
    if(!module){SetLastError(6);return 0;}
    if(GuardianLoaderEnter()){SetLastError(5);return 0;}
    Command c{};c.op=Op::LibraryFree;c.handle=reinterpret_cast<uint64_t>(module);auto status=GuardianRuntimeCall(&c);
    if(!status){if(c.transferred>(peprofile::Extended?peprofile::InitCalls:16))status=0xC000000D;
        else {for(uint32_t i=0;i<c.transferred;++i)Callback(Get(c,i),0);
            c={};c.op=Op::LibraryFinishFree;c.handle=reinterpret_cast<uint64_t>(module);status=GuardianRuntimeCall(&c);}}
    const bool unlock=GuardianLoaderLeave()==0;
    if(status || !unlock){SetLastError(6);return 0;}return 1;
}
extern "C" Procedure GetProcAddress(Handle module,const char* symbol){
    Command c{};c.op=Op::LibraryGetProc;c.handle=reinterpret_cast<uint64_t>(module);
    const auto ordinal=reinterpret_cast<uint64_t>(symbol);
    if(ordinal && ordinal<=65535)c.argument=ordinal;else if(!Text(c.path,symbol)){SetLastError(87);return nullptr;}
    if(GuardianLoaderEnter()){SetLastError(5);return nullptr;}
    auto status=GuardianRuntimeCall(&c);const auto address=c.argument;
#ifdef GUARDIAN_SCALABLE_PE
    if(!status&&c.flags){uint32_t completed=0;bool ok=c.transferred<=peprofile::InitCalls;
        if(ok)for(;completed<c.transferred;++completed){const auto call=Get(c,completed);const auto result=Callback(call,1);if(call.check&&!result){Callback(call,0);ok=false;break;}}
        if(!ok)while(completed)Callback(Get(c,--completed),0);
        c={};c.op=Op::LibraryFinishGetProc;c.handle=reinterpret_cast<uint64_t>(module);c.flags=ok?1u:0u;
        status=GuardianRuntimeCall(&c);if(!ok&&!status)status=0xC000000D;
    }
#endif
    const bool unlock=GuardianLoaderLeave()==0;
    if(status || !unlock){SetLastError(127);return nullptr;}return reinterpret_cast<Procedure>(address);
}
}
extern "C" bool RuntimeDrainLibraries() noexcept {
    using namespace guardian;using namespace guardian::win;
    for(uint32_t i=0;i<=(peprofile::Extended?peprofile::Modules*64:256u);++i){runtime::Command c{};c.op=runtime::Op::LibraryNext;
        if(GuardianLoaderEnter())return false;const auto status=GuardianRuntimeCall(&c);if(GuardianLoaderLeave())return false;
        // Earlier kernels do not advertise dynamic loading and cannot have refs.
        if(!i && (status==0xC000000D || status==0xC00000BB))return true;
        if(status)return false;if(!c.handle)return true;if(!FreeLibrary(reinterpret_cast<Handle>(c.handle)))return false;
    }return false;
}
