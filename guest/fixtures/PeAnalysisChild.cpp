#include "PeTls.h"
#include "PeEnvironmentCommon.h"
__declspec(thread) volatile uint32_t analysisChildTls=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*){if(reason==1)analysisChildTls=1;}
extern "C" uint32_t GuardianEntry(){
    using namespace guardian::win;if(analysisChildTls!=1)return 1330;
    if(!guardian::envapp::Console("13 CHILD EXECUTED\n"))return 1331;
    if(guardian::environment::Equal(GetCommandLineA(),"service"))for(uint32_t i=0;i<200;++i)Sleep(10);
    return 0;
}
