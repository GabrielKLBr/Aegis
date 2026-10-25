#include "Win32Api.h"
using namespace guardian::win;
extern "C" uint32_t GuardianEntry();
extern "C" void* volatile CrtLauncherAnchor=reinterpret_cast<void*>(&GuardianEntry);
extern "C" uint32_t GuardianEntry(){const auto* path=reinterpret_cast<const uint16_t*>(L"C:\\App\\guardian-crt.exe");char16_t command[]=u"\"C:\\App\\guardian-crt.exe\" \"hello world\" --child";
    StartupInfoA startup{};startup.cb=sizeof(startup);ProcessInformation child{};
    if(!CreateProcessW(path,reinterpret_cast<uint16_t*>(command),nullptr,nullptr,0,0,nullptr,nullptr,&startup,&child))return 1;
    const auto wait=WaitForSingleObject(child.process,15000);Dword code=99;if(wait||!GetExitCodeProcess(child.process,&code))return 2;
    if(!CloseHandle(child.thread)||!CloseHandle(child.process)||code)return 3;
    const char text[]="CRT 14.5 quoted argv child PASS\n";Dword done=0;return WriteFile(GetStdHandle(Dword(-11)),text,sizeof(text)-1,&done,nullptr)?0u:4u;}
