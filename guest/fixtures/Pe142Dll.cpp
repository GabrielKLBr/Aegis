#include <stdint.h>
#if PROBE_KIND==0
volatile uint32_t ledger[256]{},used=0;
extern "C" __declspec(dllexport) void Record(uint32_t value){if(used<256)ledger[used++]=value;}
extern "C" __declspec(dllexport) uint32_t RecordAt(uint32_t i){return i<used?ledger[i]:UINT32_MAX;}
extern "C" __declspec(dllexport) uint32_t RecordCount(uint32_t value){uint32_t n=0;for(uint32_t i=0;i<used;++i)if(ledger[i]==value)++n;return n;}
#else
extern "C" __declspec(dllimport) void Record(uint32_t);
#endif
#if PROBE_KIND==1
#include "PeTls.h"
__declspec(thread) volatile uint8_t tlsBlock[2048]={0x42};
extern "C" void TlsCallback(void*,uint32_t reason,void*){if(reason==1||reason==2)++tlsBlock[0];Record(10+reason);}
extern "C" uint32_t ProbeAlias(){return tlsBlock[0];}
extern "C" uint32_t ProbeData=0x12345678;
#elif PROBE_KIND==3
extern "C" __declspec(dllexport) uint32_t DelayValue(){return 0x5555;}
#elif PROBE_KIND==5
extern "C" __declspec(dllexport) uint32_t WidgetValue(){return WIDGET_VALUE;}
#elif PROBE_KIND==6
extern "C" __declspec(dllexport) uint32_t LateValue(){return 0x7777;}
#elif PROBE_KIND==4
extern "C" __declspec(dllexport) uint32_t RejectValue(){return 0;}
#endif
extern "C" int DllEntry(void*,uint32_t reason,void*){
#if PROBE_KIND==1
    Record(20+reason);return reason!=1||tlsBlock[0]==0x43;
#elif PROBE_KIND==4
    Record(60+reason);return reason!=1;
#elif PROBE_KIND==5
    Record(70+WIDGET_VALUE*10+reason);return 1;
#elif PROBE_KIND==6
    Record(100+reason);return 1;
#else
    (void)reason;return 1;
#endif
}
int (*volatile relocationAnchor)(void*,uint32_t,void*)=DllEntry;
