#include "PeTls.h"
__declspec(thread) volatile uint32_t threadValue=0x1357;
volatile uint32_t initialized=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*) {if(reason==1){threadValue+=1;initialized=1;}}
extern "C" int DllEntry(void*,uint32_t reason,void*) {if(reason==1){if(initialized!=1 || threadValue!=0x1358)return 0;initialized=2;}return 1;}
extern "C" __declspec(noinline) uint32_t GuardianAdd(uint32_t a,uint32_t b) {return initialized==2 && threadValue==0x1358?a+b:0;}
extern "C" __declspec(noinline) uint32_t GuardianOrdinal() {return 17;}
