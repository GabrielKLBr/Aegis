#include <windows.h>
#include <cstring>
int main(){
#ifdef CRT_RETHROW
    try {try {throw 5;}catch(int){throw;}}catch(int code){return code==5?0:1;}
#else
    const auto copy=reinterpret_cast<BOOL (*)(const wchar_t*,const wchar_t*,BOOL)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CopyFileW"));
    if(!copy)return 1;copy(L"C:\\App\\missing",L"C:\\App\\copy",FALSE);return 2;
#endif
}
