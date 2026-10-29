#define _KERNEL32_
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "guardian/Compat32.h"
namespace c=guardian::compat32;
extern "C" DWORD __cdecl GuardianCompatCall(c::Request*) noexcept;
namespace {DWORD lastError=0;const HANDLE heap=reinterpret_cast<HANDLE>(0x1234);
bool Call(c::Request& r){const auto status=GuardianCompatCall(&r);if(status){lastError=status;return false;}if(r.error){lastError=r.error;return false;}return true;}
DWORD Info(DWORD index){c::Request r;r.op=c::Op::Info;return Call(r)?r.args[index]:0;}
}
extern "C" BOOL WINAPI CompatDllMain(HINSTANCE,DWORD,void*){return TRUE;}
extern "C" DWORD WINAPI GetLastError(){return lastError;}
extern "C" void WINAPI SetLastError(DWORD value){lastError=value;}
extern "C" DWORD WINAPI GetCurrentThreadId(){return Info(1);}
extern "C" DWORD WINAPI GetCurrentProcessId(){return Info(0);}
extern "C" HANDLE WINAPI GetCurrentProcess(){return reinterpret_cast<HANDLE>(-1);}
extern "C" HANDLE WINAPI GetCurrentThread(){return reinterpret_cast<HANDLE>(-2);}
extern "C" BOOL WINAPI IsWow64Process(HANDLE process,PBOOL wow){if(!wow||process!=GetCurrentProcess()){lastError=ERROR_INVALID_PARAMETER;return FALSE;}*wow=TRUE;return TRUE;}
extern "C" HANDLE WINAPI GetStdHandle(DWORD id){return id==STD_OUTPUT_HANDLE||id==STD_ERROR_HANDLE?reinterpret_cast<HANDLE>(id):INVALID_HANDLE_VALUE;}
extern "C" BOOL WINAPI WriteFile(HANDLE h,const void* data,DWORD count,DWORD* done,OVERLAPPED* async){if(done)*done=0;if(async||!done){lastError=ERROR_INVALID_PARAMETER;return FALSE;}c::Request r;r.op=c::Op::Console;r.args[0]=uintptr_t(h);r.args[1]=uintptr_t(data);r.args[2]=count;if(!Call(r))return FALSE;*done=r.value;return TRUE;}
extern "C" void* WINAPI VirtualAlloc(void* base,SIZE_T bytes,DWORD type,DWORD protect){c::Request r;r.op=c::Op::Allocate;r.args[0]=uintptr_t(base);r.args[1]=bytes;r.args[2]=type;r.args[3]=protect;return Call(r)?reinterpret_cast<void*>(r.value):nullptr;}
extern "C" BOOL WINAPI VirtualFree(void* base,SIZE_T bytes,DWORD type){c::Request r;r.op=c::Op::Release;r.args[0]=uintptr_t(base);r.args[1]=bytes;r.args[2]=type;return Call(r);}
extern "C" HANDLE WINAPI GetProcessHeap(){return heap;}
extern "C" void* WINAPI HeapAlloc(HANDLE h,DWORD flags,SIZE_T bytes){if(h!=heap||flags&~HEAP_ZERO_MEMORY||bytes>0x100000||bytes>UINT32_MAX-16){lastError=ERROR_INVALID_PARAMETER;return nullptr;}auto* raw=static_cast<DWORD*>(VirtualAlloc(nullptr,bytes+16,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!raw)return nullptr;raw[0]=0x32334847;raw[1]=bytes;return raw+4;}
extern "C" BOOL WINAPI HeapFree(HANDLE h,DWORD flags,void* address){if(h!=heap||flags){lastError=ERROR_INVALID_PARAMETER;return FALSE;}if(!address)return TRUE;auto* raw=static_cast<DWORD*>(address)-4;if(raw[0]!=0x32334847){lastError=ERROR_INVALID_PARAMETER;return FALSE;}return VirtualFree(raw,0,MEM_RELEASE);}
extern "C" ULONGLONG WINAPI GetTickCount64(){c::Request r;r.op=c::Op::Clock;if(!Call(r))return 0;union {ULONGLONG value;DWORD words[2];} clock{};clock.words[0]=r.args[0];clock.words[1]=r.args[1];return clock.value;}
extern "C" DWORD WINAPI GetTickCount(){return DWORD(GetTickCount64());}
extern "C" void WINAPI Sleep(DWORD delay){const auto start=GetTickCount64();while(GetTickCount64()-start<delay){c::Request r;r.op=c::Op::Clock;if(!Call(r))return;}}
extern "C" BOOL WINAPI SwitchToThread(){Sleep(0);return TRUE;}
extern "C" HMODULE WINAPI GetModuleHandleW(const wchar_t* name){c::Request r;r.op=c::Op::Module;r.args[0]=uintptr_t(name);return Call(r)?reinterpret_cast<HMODULE>(r.value):nullptr;}
extern "C" FARPROC WINAPI GetProcAddress(HMODULE module,const char* name){c::Request r;r.op=c::Op::Symbol;r.args[0]=uintptr_t(module);r.args[1]=uintptr_t(name);return Call(r)?reinterpret_cast<FARPROC>(r.value):nullptr;}
extern "C" void WINAPI ExitProcess(UINT code){c::Request r;r.op=c::Op::Exit;r.args[0]=code;GuardianCompatCall(&r);for(;;){}}
// UTF-8/CP1252 conversions keep their implementation in the guest.
#include "../Win32Unicode.h"
extern "C" int WINAPI MultiByteToWideChar(UINT cp,DWORD flags,LPCCH in,int count,LPWSTR out,int cap){uint32_t e=0;const auto n=guardian::nls::Multi(cp,flags,in,count,reinterpret_cast<uint16_t*>(out),cap,e);if(!n)lastError=e;return n;}
extern "C" int WINAPI WideCharToMultiByte(UINT cp,DWORD flags,LPCWCH in,int count,LPSTR out,int cap,LPCCH replacement,LPBOOL used){uint32_t e=0;int changed=0;const auto n=guardian::nls::Wide(cp,flags,reinterpret_cast<const uint16_t*>(in),count,out,cap,replacement,&changed,e);if(used)*used=changed;if(!n)lastError=e;return n;}
struct Start {uint64_t entry,count;struct Call {uint64_t function,module,check;} calls[144];};
extern "C" void __cdecl GuardianCompatEntry(const Start* start){
    if(!start||start->count>144)ExitProcess(0xEE);
    uint32_t attached=0;
    for(;attached<start->count;++attached){const auto& call=start->calls[attached];using Init=BOOL(WINAPI*)(HINSTANCE,DWORD,void*);if(call.function>UINT32_MAX||call.module>UINT32_MAX)ExitProcess(0xEF);const auto ok=reinterpret_cast<Init>(uintptr_t(call.function))(reinterpret_cast<HINSTANCE>(uintptr_t(call.module)),DLL_PROCESS_ATTACH,nullptr);if(call.check&&!ok)ExitProcess(0xED);}
    const auto code=reinterpret_cast<int(__cdecl*)()>(uintptr_t(start->entry))();
    while(attached){const auto& call=start->calls[--attached];using Init=BOOL(WINAPI*)(HINSTANCE,DWORD,void*);reinterpret_cast<Init>(uintptr_t(call.function))(reinterpret_cast<HINSTANCE>(uintptr_t(call.module)),DLL_PROCESS_DETACH,nullptr);}
    ExitProcess(UINT(code));
}
