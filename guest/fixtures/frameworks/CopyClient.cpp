#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include "guardian/FrameworkFiles.h"
extern "C" const int FrameworkCopyClientAnchor=1411;
const void* volatile FrameworkCopyClientRelocation=&FrameworkCopyClientAnchor;
namespace {
void Print(const char* text){DWORD n=0,wrote=0;while(text[n])++n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,n,&wrote,nullptr);}
void Check(bool ok,const char* step){if(!ok){Print("COPY_FAIL: ");Print(step);Print("\n");ExitProcess(1);}}
uint64_t Digest(const wchar_t* path,uint64_t& size){HANDLE file=CreateFileW(path,GENERIC_READ,7,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"open copy");LARGE_INTEGER length{};Check(GetFileSizeEx(file,&length),"copy size");size=uint64_t(length.QuadPart);BYTE data[512]{};uint64_t hash=14695981039346656037ull;
    for(uint64_t at=0;at<size;){const DWORD n=size-at>512?512:DWORD(size-at);DWORD got=0;Check(ReadFile(file,data,n,&got,nullptr)&&got==n,"copy content");for(DWORD i=0;i<n;++i){hash^=data[i];hash*=1099511628211ull;}at+=n;}Check(CloseHandle(file)!=FALSE,"close copy");return hash;}
}
extern "C" void FrameworkCopyClient(){
    wchar_t source[128]{};Check(GetModuleFileNameW(nullptr,source,128)>0,"package path");uint64_t size=0;const auto hash=Digest(source,size);Check(size>512,"large package input");
    Check(CopyFileW(source,L"C:\\App\\copy1.exe",TRUE)!=FALSE,"first immutable copy");
    Check(CopyFileA("C:\\App\\copy1.exe","C:\\App\\copy2.exe",TRUE)!=FALSE,"copy an installed reference");uint64_t copySize=0;
    Check(Digest(L"C:\\App\\copy2.exe",copySize)==hash&&copySize==size,"reference content identity");
    Check(!CopyFileW(source,L"C:\\App\\copy1.exe",TRUE)&&GetLastError()==ERROR_FILE_EXISTS,"failIfExists");
    HANDLE denied=CreateFileW(L"C:\\App\\copy1.exe",GENERIC_WRITE,7,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(denied==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_ACCESS_DENIED,"immutable write rejected");
    Check(!CopyFileW(source,source,FALSE),"self copy rejected");Check(!CopyFileW(L"D:\\host.exe",L"C:\\App\\host.exe",TRUE),"outside namespace rejected");
    HANDLE open=CreateFileW(L"C:\\App\\copy2.exe",GENERIC_READ,7,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(open!=INVALID_HANDLE_VALUE,"shared copy open");
    Check(DeleteFileW(L"C:\\App\\copy2.exe")!=FALSE,"delete open reference");BYTE mz[2]{};DWORD got=0;Check(ReadFile(open,mz,2,&got,nullptr)&&got==2&&mz[0]=='M'&&mz[1]=='Z',"open copy survives unlink");Check(CloseHandle(open)!=FALSE,"close deleted copy");
    guardian::frameworks::FileReference forged;for(uint32_t i=0;i<128&&source[i];++i){char c=char(source[i]);if(c>='A'&&c<='Z')c+=32;forged.source[i]=c;}forged.fileBytes=size;forged.hash=hash^1;
    HANDLE file=CreateFileW(L"C:\\App\\forged.exe",GENERIC_WRITE,7,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"forged reference setup");DWORD wrote=0;Check(WriteFile(file,&forged,sizeof(forged),&wrote,nullptr)&&wrote==sizeof(forged),"forged reference write");CloseHandle(file);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};wchar_t command[]=L"C:\\App\\forged.exe";
    Check(!CreateProcessW(command,command,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)&&!process.hProcess&&!process.hThread,"unbound bytes cannot execute");Check(DeleteFileW(command)!=FALSE,"remove forged input");
    Check(DeleteFileW(L"C:\\App\\copy1.exe")!=FALSE,"remove installed copy");Print("PACKAGE_COPY_BOUNDARY_PASS\n");ExitProcess(0);
}
