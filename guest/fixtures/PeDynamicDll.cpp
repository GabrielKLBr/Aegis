#include "PeTls.h"
#include "PeRuntimeApi.h"
using namespace guardian::win;
__declspec(thread) volatile uint32_t dynamicTls=0x3140;
volatile uint32_t attachCount=0,threadAttach=0,threadDetach=0;
uint32_t* observer=nullptr;
extern "C" void TlsCallback(void*,uint32_t reason,void*){if(reason==1 || reason==2)++dynamicTls;}
extern "C" int DllEntry(void*,uint32_t reason,void*){
    if(reason==1){if(dynamicTls!=0x3141)return 0;++attachCount;}
    if(reason==2)++threadAttach;if(reason==3)++threadDetach;
    if(reason==0 && observer)++*observer;SwitchToThread();return 1;
}
extern "C" uint32_t DynamicValue(){return dynamicTls;}
extern "C" uint32_t DynamicStats(uint32_t kind){return kind==0?attachCount:(kind==1?threadAttach:threadDetach);}
extern "C" void DynamicBind(uint32_t* value){observer=value;}

extern "C" void DynamicRaise(){__try {RaiseException(0xE7654321,0,0,nullptr);}__finally {if(observer)++*observer;SwitchToThread();}}
