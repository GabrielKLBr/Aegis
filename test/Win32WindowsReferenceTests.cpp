#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "../guest/Win32Unicode.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
void Check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void CALLBACK Work(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK){InterlockedIncrement(static_cast<volatile LONG*>(context));}
bool Seh(){DWORD caught=0;__try {RaiseException(0xE0144001,0,0,nullptr);}__except(EXCEPTION_EXECUTE_HANDLER){caught=1;}return caught!=0;}
int wmain(int argc,wchar_t**){if(argc>1)return 37;try{
    const char input[]="a\xC3\xA7\xC3\xA3o \xF0\x9F\x99\x82";uint16_t ours[32]{};wchar_t windows[32]{};uint32_t error=0;
    Check(guardian::nls::Multi(65001,8,input,-1,ours,32,error)==MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input,-1,windows,32)&&!std::memcmp(ours,windows,8*2),"UTF-8 UTF-16 native comparison");
    char left[32]{},right[32]{};Check(guardian::nls::Wide(65001,0x80,ours,-1,left,32,nullptr,nullptr,error)==WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,windows,-1,right,32,nullptr,nullptr)&&!std::strcmp(left,right),"UTF-16 UTF-8 native comparison");
    const char invalid[]{char(-64),char(-81)};Check(!guardian::nls::Multi(65001,8,invalid,2,ours,32,error)&&error==1113&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,invalid,2,windows,32)&&GetLastError()==error,"Invalid UTF-8 status comparison");
    const char euro[]{char(-128),0};Check(guardian::nls::Multi(1252,0,euro,-1,ours,32,error)==MultiByteToWideChar(1252,0,euro,-1,windows,32)&&ours[0]==uint16_t(windows[0])&&ours[0]==0x20AC,"Windows-1252 native comparison");
    Check(CompareStringOrdinal(L"ação",-1,L"AÇÃO",-1,TRUE)==CSTR_EQUAL&&guardian::nls::Upper(0xE7)==0xC7&&guardian::nls::Upper(0xE3)==0xC3,"Ordinal Latin-1 comparison");
    const auto api=LoadLibraryExW(L"api-ms-win-core-memory-l1-1-0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);Check(api&&api==GetModuleHandleW(L"kernelbase.dll")&&GetProcAddress(api,"VirtualAlloc"),"Native API set resolves to KERNELBASE");FreeLibrary(api);
    volatile LONG callbacks=0;auto* work=CreateThreadpoolWork(Work,const_cast<LONG*>(&callbacks),nullptr);Check(work!=nullptr,"Native thread pool");for(int i=0;i<3;++i)SubmitThreadpoolWork(work);WaitForThreadpoolWorkCallbacks(work,FALSE);CloseThreadpoolWork(work);Check(callbacks==3,"Native callbacks");
    std::array<wchar_t,32768> path{};Check(GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()))!=0,"Native executable path");std::wstring command=L"\""+std::wstring(path.data())+L"\" --child";STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    Check(CreateProcessW(path.data(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&pi)!=FALSE,"Native child creation");Check(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT&&ResumeThread(pi.hThread)==1,"Suspended child state");
    DWORD code=0;Check(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0&&GetExitCodeProcess(pi.hProcess,&code)&&code==37,"Native child exit");CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    Check(Seh(),"Native SEH");
    std::cout<<"Win32 native reference: Unicode, invalid input, API set host, pool, suspended child, waits and SEH PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
