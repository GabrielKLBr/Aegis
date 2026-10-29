#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
extern "C" BOOL WINAPI CompatCrtEntry(HINSTANCE,DWORD,void*){return TRUE;}
extern "C" int __cdecl puts(const char* text){if(!text)return -1;DWORD bytes=0;while(bytes<65536&&text[bytes])++bytes;if(bytes==65536)return -1;DWORD done=0;if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,bytes,&done,nullptr)||done!=bytes)return -1;return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"\n",1,&done,nullptr)&&done==1?0:-1;}
// Literal-only printf is enough for the pinned independent hello-world input.
// Format conversion remains unsupported, rather than invoking a host CRT.
extern "C" int __cdecl printf(const char* text,...){if(!text)return -1;DWORD bytes=0;while(bytes<65536&&text[bytes]){if(text[bytes]=='%'){SetLastError(ERROR_NOT_SUPPORTED);return -1;}++bytes;}if(bytes==65536)return -1;DWORD done=0;return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,bytes,&done,nullptr)&&done==bytes?int(done):-1;}
