#include "Win32Api.h"
using namespace guardian;using namespace guardian::win;
namespace {const uint16_t* W(const wchar_t* p){return reinterpret_cast<const uint16_t*>(p);}}
extern "C" int GuardianEntry(){uint16_t value[32]{},path[128]{};
    if(GetEnvironmentVariableW(W(L"AEGIS_PHASE"),value,32)!=4||CompareStringOrdinal(value,-1,W(L"ação"),-1,0)!=2||!GetModuleFileNameW(nullptr,path,128))return 91;
    const auto* command=GetCommandLineW();bool child=false;for(uint32_t i=0;i<120&&command[i];++i)if(command[i]=='-'&&command[i+1]=='-'&&command[i+2]=='c')child=true;if(!child)return 92;
    auto file=CreateFileW(W(L"C:\\App\\phase14-4.bin"),0x80000000,7,nullptr,3,0x80,nullptr);uint64_t content=0;Dword count=0;
    if(file==reinterpret_cast<Handle>(UINT64_MAX)||!ReadFile(file,&content,8,&count,nullptr)||count!=8||content!=0xDEADBEEF||!CloseHandle(file)||!SetEnvironmentVariableW(W(L"AEGIS_PHASE"),W(L"child")))return 93;
    Dword caught=0;__try {RaiseException(0xE0144002,0,0,nullptr);}__except(1){caught=1;}return caught?37:94;}
const void* volatile win32ChildAnchor=reinterpret_cast<const void*>(GuardianEntry);
