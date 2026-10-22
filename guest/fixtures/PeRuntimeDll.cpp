#define GUARDIAN_RUNTIME_BUILD
#include "PeTls.h"
#include "PeRuntimeApi.h"
#include <intrin.h>
extern "C" bool RuntimeInitialize() noexcept;
extern "C" bool RuntimeShutdown(bool) noexcept;
__declspec(thread) volatile uint32_t tlsValue=0x1357;
volatile uint32_t activeCallback=0,attached=0,detached=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*) {
    if(activeCallback)__ud2();activeCallback=1;
    if(reason==1 || reason==2)++tlsValue;
    if(reason==2 || reason==3)guardian::win::SwitchToThread();
    if(activeCallback!=1)__ud2();activeCallback=0;
}
extern "C" int DllEntry(void*,uint32_t reason,void* reserved) {
    if(activeCallback)__ud2();activeCallback=1;
    if(reason==1){activeCallback=0;return tlsValue==0x1358 && RuntimeInitialize();}
    if(reason==2 || reason==3){if(tlsValue!=0x1358)__ud2();guardian::win::SwitchToThread();if(activeCallback!=1)__ud2();if(reason==2)attached=attached+1;else detached=detached+1;}
    activeCallback=0;if(reason==0 && ((!reserved && attached!=detached) || !RuntimeShutdown(reserved!=nullptr)))__ud2();return 1;
}
