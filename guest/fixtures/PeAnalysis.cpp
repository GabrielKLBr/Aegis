#include "PeTls.h"
#include "PeEnvironmentCommon.h"
using namespace guardian::win;
extern "C" uint32_t GuardianRuntimeCall(guardian::runtime::Command*) noexcept;
__declspec(thread) volatile uint32_t analysisTls=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*){if(reason==1)analysisTls=1;}
namespace {
bool Say(const char* text){return guardian::envapp::Console(text);}
uint32_t Failure(uint32_t code){guardian::environment::Text t;t.Add("13 FAIL ");t.Number(code);t.Add("\n");Say(t.bytes);return code;}
Dword Worker(void*){return 7;}
bool Rewrite(const char* path,bool rename){
    const auto file=CreateFileA(path,0xC0000000,7,nullptr,3,0,nullptr);if(file==guardian::envapp::Invalid())return false;
    const char replacement[]="Rewritten inert synthetic document\n";uint32_t done=0;
    const bool changed=WriteFile(file,replacement,sizeof(replacement)-1,&done,nullptr)!=0;
    if(!changed){const auto error=GetLastError();CloseHandle(file);guardian::environment::Text before;if(error!=5||!guardian::envapp::ReadText(path,before)||!guardian::environment::Equal(before.bytes,"Original synthetic document\n"))return false;return Say("13 WRITE DENIED; ORIGINAL INTACT\n");}
    if(done!=sizeof(replacement)-1||!FlushFileBuffers(file)||!CloseHandle(file))return false;
    if(rename){guardian::environment::Text target;target.Add(path);target.Add(".locked");if(!MoveFileA(path,target.bytes))return GetLastError()==5;guardian::environment::Text opened;if(!guardian::envapp::ReadText(target.bytes,opened)||!guardian::environment::Equal(opened.bytes,replacement))return false;}
    if(!rename){guardian::environment::Text opened;if(!guardian::envapp::ReadText(path,opened)||!guardian::environment::Equal(opened.bytes,replacement))return false;}
    return true;
}
}
extern "C" uint32_t GuardianEntry(){
    if(analysisTls!=1)return Failure(1300);uint32_t scenario=0;if(!GuardianAnalysisInfo(&scenario))return Failure(1301);
    guardian::runtime::Command malformed{};malformed.op=guardian::runtime::Op::ProcessCreate;const char child[]="guardian-lifetime-child.exe";for(uint32_t i=0;i<sizeof(child);++i)malformed.path[i]=child[i];for(uint32_t i=0;i<128;++i)malformed.data[i]='x';
    if(GuardianRuntimeCall(&malformed)!=0xC000000D)return Failure(1324);
    if(!Say("13 READY\n"))return Failure(1302);
    if(!GuardianNetworkEcho()&&GetLastError()!=5)return Failure(1323);
    if(!Rewrite("C:\\a.txt",scenario==0))return Failure(1303);
    if(!scenario){if(!Rewrite("C:\\b.txt",true)||!Rewrite("C:\\c.txt",true))return Failure(1304);
        auto root=reinterpret_cast<Handle>(guardian::runtime::CurrentUser);Handle key=nullptr;
        if(RegOpenKeyExA(root,"Lab\\Run",0,3,&key))return Failure(1305);
        const char value[]="guardian-analysis-child.exe";const auto result=RegSetValueExA(key,"OwnedStartup",0,3,reinterpret_cast<const uint8_t*>(value),sizeof(value));
        if(result&&result!=5)return Failure(1306);if(RegCloseKey(key))return Failure(1307);
        uint32_t service=0;if(GuardianCreateService("SyntheticService","guardian-analysis-child.exe",&service)){
            if(GuardianStartService(service)){Sleep(20);uint32_t state=0;if(!GuardianQueryService(service,&state)||state!=1||!GuardianStopService(service)||!GuardianQueryService(service,&state)||state)return Failure(1308);}
            else if(GetLastError()!=5)return Failure(1309);
        }else if(GetLastError()!=5)return Failure(1310);
        auto* region=VirtualAlloc(nullptr,4096,0x3000,4);if(region){uint32_t old=0;if(!VirtualProtect(region,4096,0x40,&old)&&GetLastError()!=5&&GetLastError()!=50&&GetLastError()!=87)return Failure(1311);if(!VirtualProtect(region,4096,2,&old)&&GetLastError()!=5)return Failure(1322);if(!VirtualFree(region,0,0x8000))return Failure(1312);}else if(GetLastError()!=5)return Failure(1313);
        const auto dll=LoadLibraryA("guardian-dynamic.dll");if(dll){if(!FreeLibrary(dll))return Failure(1314);}else if(GetLastError()!=5)return Failure(1315);
    }
    uint32_t tid=0;const auto thread=CreateThread(nullptr,0,Worker,nullptr,0,&tid);
    if(thread){uint32_t code=0;if(WaitForSingleObject(thread,UINT32_MAX)!=0||!GetExitCodeThread(thread,&code)||code!=7||!CloseHandle(thread))return Failure(1316);}else if(GetLastError()!=5)return Failure(1317);
    // Real ACL denial and an unsupported host-drive path are observable attempts.
    const auto missing=CreateFileA("Z:\\host-private.txt",0x80000000,3,nullptr,3,0,nullptr);if(missing!=guardian::envapp::Invalid())return Failure(1318);
    const char* readPath=scenario?"C:\\b.txt":"C:\\persist.bin";
    const auto readOnly=CreateFileA(readPath,0x80000000,7,nullptr,3,0,nullptr);if(readOnly==guardian::envapp::Invalid())return Failure(1319);
    uint32_t done=0;const uint8_t byte=0;if(WriteFile(readOnly,&byte,1,&done,nullptr)||GetLastError()!=5||!CloseHandle(readOnly))return Failure(1320);
    if(!Say("13 DONE\n"))return Failure(1321);return 0;
}
