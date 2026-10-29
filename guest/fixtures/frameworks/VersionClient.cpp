#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winver.h>
extern "C" const int FrameworkVersionClientAnchor=1411;
const void* volatile FrameworkVersionClientRelocation=&FrameworkVersionClientAnchor;
namespace {
void Print(const char* p){DWORD n=0,wrote=0;while(p[n])++n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),p,n,&wrote,nullptr);}
void Check(bool ok,const char* step){if(!ok){Print("VERSION_FAIL: ");Print(step);Print("\n");ExitProcess(1);}}
}
extern "C" void FrameworkVersionClient(){
    wchar_t path[512]{};const auto count=GetModuleFileNameW(nullptr,path,512);Check(count>0&&count<512,"module path");DWORD ignored=9;const auto size=GetFileVersionInfoSizeW(path,&ignored);Check(size>=52&&!ignored,"version size");
    void* data=HeapAlloc(GetProcessHeap(),0,size);Check(data!=nullptr,"version allocation");Check(GetFileVersionInfoW(path,0,size,data)!=FALSE,"version resource");
    VS_FIXEDFILEINFO* fixed=nullptr;UINT n=0;Check(VerQueryValueW(data,L"\\",reinterpret_cast<void**>(&fixed),&n)&&fixed&&n==sizeof(*fixed),"fixed information");
    Check(fixed->dwSignature==0xFEEF04BD&&fixed->dwFileVersionMS==0x000E000B&&fixed->dwFileVersionLS==1,"file version");
    wchar_t* text=nullptr;Check(VerQueryValueW(data,L"\\StringFileInfo\\040904b0\\FileDescription",reinterpret_cast<void**>(&text),&n)&&text&&n>5&&text[0]==L'A'&&text[n-1]==0,"string information");
    void* translation=nullptr;Check(VerQueryValueW(data,L"\\VarFileInfo\\Translation",&translation,&n)&&n==4,"translation");
    void* missing=reinterpret_cast<void*>(1);n=99;Check(!VerQueryValueW(data,L"\\StringFileInfo\\040904b0\\Missing",&missing,&n),"missing value");
    HeapFree(GetProcessHeap(),0,data);Print("VERSION_RESOURCE_PASS\n");ExitProcess(0);
}
