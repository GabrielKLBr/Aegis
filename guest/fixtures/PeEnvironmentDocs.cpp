#include "PeTls.h"
#include "PeEnvironmentCommon.h"
#include "guardian/Runtime.h"
using namespace guardian::win;
__declspec(thread) volatile uint32_t docsTls=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*){if(reason==1)docsTls=1;}
extern "C" uint32_t GuardianEntry(){
    using namespace guardian;using namespace envapp;
    Info info{};if(docsTls!=1||!GuardianEnvironmentInfo(&info)||!environment::Valid(info)||!info.catalogHash)return Fail(420);
    const auto* path=GetCommandLineA();if(!environment::DocumentAllowed(path))return Fail(421);
    Text document;if(!ReadText(path,document))return Fail(422);
    for(uint32_t i=0;i<document.size;++i){const auto c=uint8_t(document.bytes[i]);if((c<32&&c!=10&&c!=9)||c>126)return Fail(423);}
    if(environment::Equal(path,environment::DocumentPath(1))){
        const char prefix[]="item,units\n";if(document.size<sizeof(prefix)||!Same(document.bytes,prefix,sizeof(prefix)-1))return Fail(424);
        uint32_t rows=0;bool number=false,digit=false;
        for(uint32_t i=sizeof(prefix)-1;i<document.size;++i){const char c=document.bytes[i];if(c==','){if(number)return Fail(425);number=true;digit=false;}
            else if(c=='\n'){if(!number||!digit)return Fail(426);++rows;number=digit=false;}else if(number){if(c<'0'||c>'9')return Fail(427);digit=true;}}
        if(rows!=2||number)return Fail(428);
    }
    if(!Console("12E DOC OPEN\n")||!Console(document.bytes))return Fail(429);return 0;
}
