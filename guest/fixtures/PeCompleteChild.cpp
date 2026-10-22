#define GuardianEntry GuardianPreviousChild
#include "PeLifetimeChild.cpp"
#undef GuardianEntry
#include "guardian/Exception.h"
extern "C" uint64_t GuardianFaultUd();
extern "C" void* _exception_info();
namespace {
using namespace guardian::exceptions;
bool Is(const char* expected){const auto* text=GetCommandLineA();for(uint32_t i=0;i<64;++i){if(text[i]!=expected[i])return false;if(!text[i])return true;}return false;}
int32_t BadFilter(Pointers* pointers){
    if(Is("badcontext"))pointers->context->rip=0x100000;
    if(Is("badmxcsr"))pointers->context->mxcsr=0xFFFFFFFF;
    if(Is("nested"))GuardianFaultUd();
    if(Is("badtarget"))RtlUnwindEx(reinterpret_cast<void*>(0xDEAD),reinterpret_cast<void*>(pointers->context->rip),pointers->record,nullptr,pointers->context,nullptr);
    return -1;
}
}
extern "C" uint32_t GuardianEntry(){
    if(Is("kernelscratch")){return static_cast<uint32_t>(*reinterpret_cast<const volatile uint64_t*>(0x110000));}
    if(Is("leakdll")){return LoadLibraryA("guardian-dynamic.dll")?17:224;}
    if(Is("attachfault") || Is("detachfault")){const auto module=LoadLibraryA("guardian-failing.dll");if(module && Is("detachfault"))return 0;if(module)FreeLibrary(module);return 225;}
    if(Is("unhandled")){GuardianFaultUd();return 221;}
    if(Is("badcontext") || Is("nested") || Is("noncontinuable") || Is("badmxcsr") || Is("badtarget")){
        __try {if(Is("noncontinuable"))RaiseException(0xE1234567,1,0,nullptr);else GuardianFaultUd();}
        __except(BadFilter(static_cast<Pointers*>(_exception_info()))){return 222;}
        return 223;
    }return GuardianPreviousChild();
}
