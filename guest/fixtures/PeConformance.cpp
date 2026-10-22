#ifdef GUARDIAN_NATIVE_REFERENCE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace platform {
using ::QueryPerformanceCounter;using ::QueryPerformanceFrequency;using ::GetTickCount64;using ::GetTickCount;
using ::GetSystemTimeAsFileTime;using ::QueryUnbiasedInterruptTime;using ::Sleep;using ::GetLastError;using ::SetLastError;
using ::GetProcessHeap;using ::HeapAlloc;using ::HeapReAlloc;using ::HeapFree;using ::VirtualAlloc;using ::VirtualFree;
using ::VirtualProtect;using ::VirtualQuery;using ::CreateMutexA;using ::WaitForSingleObject;using ::ReleaseMutex;
using ::CloseHandle;using ::CreateThread;using ::GetExitCodeThread;using ::GetCurrentThreadId;using ::GetCurrentProcessId;
using ::CreateFileA;using ::ReadFile;using ::WriteFile;using ::SetFilePointerEx;using ::GetFileSizeEx;using ::FlushFileBuffers;
using ::RaiseException;using ::GetStdHandle;
}
#else
// Keep the previous PE/TLS/thread/exception regression in the same guest boot.
#define GUARDIAN_COMPLETE_ENTRY GuardianEarlierConformanceProof
#include "PeCompleteConsole.cpp"
#undef GUARDIAN_COMPLETE_ENTRY
namespace platform = guardian::win;
#endif
#include "guardian/ConformanceProgram.h"
#include <intrin.h>
extern "C" uint64_t GuardianFaultUd();
extern "C" void GuardianFaultDivide();
extern "C" unsigned long _exception_code();
extern "C" void* _exception_info();
namespace {
using guardian::conformance::Api;
using guardian::conformance::Handle;
using WorkerCallback=guardian::conformance::Worker;
#ifdef GUARDIAN_NATIVE_REFERENCE
using FaultPointers=EXCEPTION_POINTERS;
int32_t Raised(FaultPointers* p) {
    const auto& r=*p->ExceptionRecord;
    return r.ExceptionCode==0xE12C0001 && r.NumberParameters==2 && r.ExceptionInformation[0]==7 && r.ExceptionInformation[1]==11?1:0;
}
int32_t Continue(FaultPointers* p) {
    if(p->ExceptionRecord->ExceptionCode!=0xC000001D)return 0;
    p->ContextRecord->Rip+=2;p->ContextRecord->Rax=0x1234;return -1;
}
#else
using FaultPointers=guardian::exceptions::Pointers;
int32_t Raised(FaultPointers* p) {
    const auto& r=*p->record;
    return r.code==0xE12C0001 && r.count==2 && r.information[0]==7 && r.information[1]==11?1:0;
}
int32_t Continue(FaultPointers* p) {
    if(p->record->code!=0xC000001D)return 0;
    p->context->rip+=2;p->context->gpr[0]=0x1234;return -1;
}
#endif
__declspec(noinline) void Finally(volatile uint32_t* cleaned) {
    __try {GuardianFaultDivide();} __finally {*cleaned=*cleaned+1;}
}
uint32_t Exception(uint32_t kind) {
    if(kind==0) {
        __try {
            const uint64_t arguments[2]={7,11};
#ifdef GUARDIAN_NATIVE_REFERENCE
            platform::RaiseException(0xE12C0001,0,2,reinterpret_cast<const ULONG_PTR*>(arguments));
#else
            platform::RaiseException(0xE12C0001,0,2,arguments);
#endif
        } __except(Raised(static_cast<FaultPointers*>(_exception_info()))) {return 1;}
        return 0;
    }
    if(kind==1) {
        uint64_t value=0;
        __try {value=GuardianFaultUd();}
        __except(Continue(static_cast<FaultPointers*>(_exception_info()))) {return 0;}
        return value==0x1234?1u:0u;
    }
    volatile uint32_t cleaned=0;
    __try {Finally(&cleaned);} __except(_exception_code()==0xC0000094?1:0) {return cleaned==1?1u:0u;}
    return 0;
}
Api Interface() {
    Api a{};
    a.counter=[](int64_t* value) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        LARGE_INTEGER native{};const bool ok=platform::QueryPerformanceCounter(&native)!=0;*value=native.QuadPart;return ok;
#else
        return platform::QueryPerformanceCounter(value)!=0;
#endif
    };
    a.frequency=[](int64_t* value) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        LARGE_INTEGER native{};const bool ok=platform::QueryPerformanceFrequency(&native)!=0;*value=native.QuadPart;return ok;
#else
        return platform::QueryPerformanceFrequency(value)!=0;
#endif
    };
    a.ticks=[]() -> uint64_t {return platform::GetTickCount64();};
    a.ticks32=[]() -> uint32_t {return platform::GetTickCount();};
    a.utc=[]() -> uint64_t {
#ifdef GUARDIAN_NATIVE_REFERENCE
        FILETIME time{};platform::GetSystemTimeAsFileTime(&time);return uint64_t(time.dwLowDateTime)|(uint64_t(time.dwHighDateTime)<<32);
#else
        platform::FileTime time{};platform::GetSystemTimeAsFileTime(&time);return uint64_t(time.low)|(uint64_t(time.high)<<32);
#endif
    };
    a.unbiased=[](uint64_t* value) {return platform::QueryUnbiasedInterruptTime(value)!=0;};
    a.sleep=[](uint32_t value) {platform::Sleep(value);};
    a.error=[]() -> uint32_t {return platform::GetLastError();};
    a.setError=[](uint32_t value) {platform::SetLastError(value);};
    a.heap=[]() -> Handle {return platform::GetProcessHeap();};
    a.allocate=[](Handle h,uint32_t flags,uint64_t bytes) {return platform::HeapAlloc(h,flags,bytes);};
    a.reallocate=[](Handle h,uint32_t flags,void* old,uint64_t bytes) {return platform::HeapReAlloc(h,flags,old,bytes);};
    a.release=[](Handle h,uint32_t flags,void* p) {return platform::HeapFree(h,flags,p)!=0;};
    a.virtualAllocate=[](uint64_t bytes) {return platform::VirtualAlloc(nullptr,bytes,0x3000,4);};
    a.virtualFree=[](void* p) {return platform::VirtualFree(p,0,0x8000)!=0;};
    a.protect=[](void* p,uint64_t bytes,uint32_t flags,uint32_t* old) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        DWORD previous=0;const bool ok=platform::VirtualProtect(p,bytes,flags,&previous)!=0;*old=previous;return ok;
#else
        return platform::VirtualProtect(p,bytes,flags,old)!=0;
#endif
    };
    a.query=[](const void* p,guardian::memory::Information* out,uint64_t bytes) -> uint64_t {
#ifdef GUARDIAN_NATIVE_REFERENCE
        if(bytes<sizeof(*out))return 0;
        MEMORY_BASIC_INFORMATION native{};const auto result=platform::VirtualQuery(p,&native,sizeof(native));
        if(!result)return 0;
        *out={reinterpret_cast<uint64_t>(native.BaseAddress),reinterpret_cast<uint64_t>(native.AllocationBase),native.AllocationProtect,0,
              native.RegionSize,native.State,native.Protect,native.Type,0};return sizeof(*out);
#else
        return platform::VirtualQuery(p,out,bytes);
#endif
    };
    a.mutex=[](bool owned) -> Handle {return platform::CreateMutexA(nullptr,owned?1:0,nullptr);};
    a.wait=[](Handle h,uint32_t ms) -> uint32_t {return platform::WaitForSingleObject(h,ms);};
    a.unlock=[](Handle h) {return platform::ReleaseMutex(h)!=0;};
    a.close=[](Handle h) {return platform::CloseHandle(h)!=0;};
    a.thread=[](WorkerCallback entry,void* argument,uint32_t* id) -> Handle {
#ifdef GUARDIAN_NATIVE_REFERENCE
        DWORD tid=0;const auto handle=platform::CreateThread(nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(entry),argument,0,&tid);*id=tid;return handle;
#else
        return platform::CreateThread(nullptr,0,entry,argument,0,id);
#endif
    };
    a.exitCode=[](Handle h,uint32_t* code) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        DWORD value=0;const bool ok=platform::GetExitCodeThread(h,&value)!=0;*code=value;return ok;
#else
        return platform::GetExitCodeThread(h,code)!=0;
#endif
    };
    a.threadId=[]() -> uint32_t {return platform::GetCurrentThreadId();};
    a.processId=[]() -> uint32_t {return platform::GetCurrentProcessId();};
    a.open=[](const char* path,uint32_t access,uint32_t share,uint32_t disposition) -> Handle {
        return platform::CreateFileA(path,access,share,nullptr,disposition,0x80,nullptr);
    };
    a.read=[](Handle h,void* p,uint32_t bytes,uint32_t* transferred) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        DWORD n=0;const bool ok=platform::ReadFile(h,p,bytes,&n,nullptr)!=0;*transferred=n;return ok;
#else
        return platform::ReadFile(h,p,bytes,transferred,nullptr)!=0;
#endif
    };
    a.write=[](Handle h,const void* p,uint32_t bytes,uint32_t* transferred) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        DWORD n=0;const bool ok=platform::WriteFile(h,p,bytes,&n,nullptr)!=0;*transferred=n;return ok;
#else
        return platform::WriteFile(h,p,bytes,transferred,nullptr)!=0;
#endif
    };
    a.seek=[](Handle h,int64_t distance,int64_t* position,uint32_t origin) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        LARGE_INTEGER delta{},result{};delta.QuadPart=distance;const bool ok=platform::SetFilePointerEx(h,delta,&result,origin)!=0;
        *position=result.QuadPart;return ok;
#else
        return platform::SetFilePointerEx(h,distance,position,origin)!=0;
#endif
    };
    a.size=[](Handle h,int64_t* size) {
#ifdef GUARDIAN_NATIVE_REFERENCE
        LARGE_INTEGER result{};const bool ok=platform::GetFileSizeEx(h,&result)!=0;*size=result.QuadPart;return ok;
#else
        return platform::GetFileSizeEx(h,size)!=0;
#endif
    };
    a.flush=[](Handle h) {return platform::FlushFileBuffers(h)!=0;};
    a.exception=Exception;
    a.features=[]() -> uint32_t {
        int registers[4]{};__cpuid(registers,static_cast<int>(0x80000001u));
        uint32_t result=(static_cast<uint32_t>(registers[3])&(1u<<29))?1u:0u;
        if(static_cast<uint32_t>(registers[3])&(1u<<20))result|=2;
        __cpuid(registers,1);if(static_cast<uint32_t>(registers[3])&(1u<<26))result|=4;
        return result;
    };
    return a;
}
bool Emit(void*,uint32_t id,uint64_t actual) {
    char line[64]={'1','2','C',' '};uint32_t at=4;
    const auto number=[&](uint64_t value) {char reverse[24]{};uint32_t n=0;do{reverse[n++]=static_cast<char>('0'+value%10);value/=10;}while(value);while(n)line[at++]=reverse[--n];};
    number(id);line[at++]=' ';number(actual);line[at++]='\n';
#ifdef GUARDIAN_NATIVE_REFERENCE
    DWORD count=0;
#else
    uint32_t count=0;
#endif
    return platform::WriteFile(platform::GetStdHandle(static_cast<uint32_t>(-11)),line,at,&count,nullptr)!=0 && count==at;
}
}
#ifdef GUARDIAN_NATIVE_REFERENCE
int main(int argc,char** argv) {
    try {
        if(argc!=2)return 2;
        const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
        LARGE_INTEGER counter{};::QueryPerformanceCounter(&counter);
        const auto owned=root/("reference-"+std::to_string(::GetCurrentProcessId())+"-"+std::to_string(counter.QuadPart));
        if(!std::filesystem::create_directory(owned))return 2;
        using GetVersion=LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
        const auto version=reinterpret_cast<GetVersion>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        RTL_OSVERSIONINFOW nt{};nt.dwOSVersionInfoSize=sizeof(nt);if(!version||version(&nt)!=0)return 2;
        SYSTEM_INFO system{};::GetNativeSystemInfo(&system);
        if(sizeof(void*)!=8 || system.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64)return 2;
        const auto metadata="12C_META {\"schema\":1,\"backend\":\"native-windows\",\"architecture\":\"x64\",\"nt\":\""+
            std::to_string(nt.dwMajorVersion)+"."+std::to_string(nt.dwMinorVersion)+"."+std::to_string(nt.dwBuildNumber)+
            "\",\"pageBytes\":"+std::to_string(system.dwPageSize)+",\"programHash\":\""+GUARDIAN_CONFORMANCE_PROGRAM_ID+"\"}";
        std::cout<<metadata<<'\n'<<std::flush;
        auto api=Interface();const auto path=(owned/"conformance.bin").string();
        const auto failures=guardian::conformance::Run(api,path.c_str(),Emit,nullptr);
        std::cout<<"12C_DONE "<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
#else
extern "C" uint32_t GuardianEntry() {
    const auto previous=GuardianEarlierConformanceProof();if(previous)return previous;
    auto api=Interface();return guardian::conformance::Run(api,"C:\\phase12c.bin",Emit,nullptr);
}
#endif
