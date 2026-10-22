#include "PeTls.h"
#include <intrin.h>
extern "C" __declspec(dllimport) uint32_t GuardianAdd(uint32_t,uint32_t);
extern "C" __declspec(dllimport) uint32_t GuardianOrdinal();
__declspec(thread) volatile uint32_t threadValue=0x2468;
volatile uint32_t initialized=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*) {if(reason==1){threadValue+=1;initialized=1;}}
extern "C" uint32_t GuardianEntry() {
    if(initialized!=1 || threadValue!=0x2469)return 1;
    const auto teb=__readgsqword(0x30),peb=__readgsqword(0x60);
    if(!teb || !peb || !__readgsqword(0x40) || !__readgsqword(0x48) || !*reinterpret_cast<const uint64_t*>(peb+0x10))return 4;
    if(reinterpret_cast<uint64_t>(&teb)>=__readgsqword(8) || reinterpret_cast<uint64_t>(&teb)<__readgsqword(0x10))return 5;
    return GuardianAdd(20,22)==42 && GuardianOrdinal()==17?0u:2u;
}
