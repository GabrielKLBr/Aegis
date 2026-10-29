#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#ifndef FRAMEWORK_INSTALL_MODE
#define FRAMEWORK_INSTALL_MODE 0
#endif
extern "C" const int FrameworkInstallerAnchor=1411;
const void* volatile FrameworkInstallerRelocation=&FrameworkInstallerAnchor;
namespace {
constexpr wchar_t Target[]=L"C:\\App\\installed.exe",Key[]=L"Software\\MiniNT\\Framework14_11";
void Print(const char* text){DWORD n=0,wrote=0;while(text[n])++n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,n,&wrote,nullptr);}
void Check(bool ok,const char* step){if(!ok){Print("PORTABLE_FAIL: ");Print(step);Print("\n");ExitProcess(1);}}
#if FRAMEWORK_INSTALL_MODE != 2
uint64_t Digest(const wchar_t* path,uint64_t& size){HANDLE file=CreateFileW(path,GENERIC_READ,7,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"open payload");LARGE_INTEGER length{};Check(GetFileSizeEx(file,&length)&&length.QuadPart>512,"payload size");size=uint64_t(length.QuadPart);BYTE data[512]{};uint64_t hash=14695981039346656037ull;
    for(uint64_t at=0;at<size;){const DWORD n=size-at>512?512:DWORD(size-at);DWORD read=0;Check(ReadFile(file,data,n,&read,nullptr)&&read==n,"payload bytes");for(DWORD i=0;i<n;++i){hash^=data[i];hash*=1099511628211ull;}at+=n;}Check(CloseHandle(file)!=FALSE,"close payload");return hash;}
#endif
}
extern "C" void FrameworkInstaller(){
    wchar_t source[128]{};char config[128]{};HANDLE input=CreateFileW(L"C:\\App\\portable.cfg",GENERIC_READ,7,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(input!=INVALID_HANDLE_VALUE,"package configuration");DWORD got=0;Check(ReadFile(input,config,127,&got,nullptr)&&got>0&&got<127,"package source");CloseHandle(input);
    for(DWORD i=0;i<got;++i){Check(config[i]>=32&&config[i]<=126,"package path");source[i]=wchar_t(config[i]);}

#if FRAMEWORK_INSTALL_MODE == 2
    {Check(DeleteFileW(Target)!=FALSE,"uninstall file");HKEY key=nullptr;Check(RegOpenKeyExW(HKEY_CURRENT_USER,Key,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS,"uninstall registration");Check(RegDeleteValueW(key,L"InstallPath")==ERROR_SUCCESS,"delete install path");Check(RegDeleteValueW(key,L"Package")==ERROR_SUCCESS,"delete package");RegCloseKey(key);Check(RegDeleteKeyW(HKEY_CURRENT_USER,Key)==ERROR_SUCCESS,"delete registration");
        HANDLE removed=CreateFileW(Target,GENERIC_READ,7,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(removed==INVALID_HANDLE_VALUE,"file removed");Print("PORTABLE_UNINSTALL_PASS\n");ExitProcess(0);}

#else
    if constexpr(FRAMEWORK_INSTALL_MODE==0){Check(CopyFileW(source,Target,TRUE)!=FALSE,"install immutable package copy");HKEY key=nullptr;DWORD disposition=0;
        Check(RegCreateKeyExW(HKEY_CURRENT_USER,Key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,&disposition)==ERROR_SUCCESS,"create registration");
        Check(RegSetValueExW(key,L"InstallPath",0,REG_SZ,reinterpret_cast<const BYTE*>(Target),sizeof(Target))==ERROR_SUCCESS,"register path");
        Check(RegSetValueExW(key,L"Package",0,REG_SZ,reinterpret_cast<const BYTE*>(source),(got+1)*2)==ERROR_SUCCESS,"register package");RegCloseKey(key);
    }else {HKEY key=nullptr;Check(RegOpenKeyExW(HKEY_CURRENT_USER,Key,0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS,"persisted registration");wchar_t path[128]{};DWORD type=0,bytes=sizeof(path);
        Check(RegQueryValueExW(key,L"InstallPath",nullptr,&type,reinterpret_cast<BYTE*>(path),&bytes)==ERROR_SUCCESS&&type==REG_SZ&&bytes==sizeof(Target),"persisted path");for(uint32_t i=0;i<sizeof(Target)/sizeof(Target[0]);++i)Check(path[i]==Target[i],"path value");RegCloseKey(key);}
    uint64_t originalSize=0,installedSize=0;const auto original=Digest(source,originalSize),installed=Digest(Target,installedSize);Check(original==installed&&originalSize==installedSize,"installed bytes equal package");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};wchar_t command[]=L"C:\\App\\installed.exe";
    Check(CreateProcessW(Target,command,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=FALSE,"start installed application");
    Check(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"application completion");DWORD code=1;Check(GetExitCodeProcess(process.hProcess,&code)&&code==0,"application exit");CloseHandle(process.hThread);CloseHandle(process.hProcess);
    Print(FRAMEWORK_INSTALL_MODE==0?"PORTABLE_INSTALL_PASS\n":"PORTABLE_RESUME_PASS\n");ExitProcess(0);
#endif
}
