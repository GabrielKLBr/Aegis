#include "PeTls.h"
#include "PeRuntimeApi.h"
#include <intrin.h>
bool Mode(const char* word){const auto* text=guardian::win::GetCommandLineA();for(uint32_t i=0;i<64;++i){if(text[i]!=word[i])return false;if(!text[i])return true;}return false;}
extern "C" void TlsCallback(void*,uint32_t reason,void*){if(reason==1 && Mode("attachfault"))__ud2();}
extern "C" int DllEntry(void*,uint32_t reason,void*){
    if(reason==1)return Mode("detachfault")?1:0;
    if(reason==0 && Mode("detachfault"))__ud2();return 1;
}
extern "C" uint32_t FailingSymbol(){return 1;}
