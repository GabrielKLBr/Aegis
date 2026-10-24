#define GUARDIAN_NATIVE_HOST_REFERENCE
#include "../guest/fixtures/NativeApi.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <stdexcept>
using namespace guardian::native;
namespace {
void Check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
template<class T>T Function(HMODULE dll,const char* name){auto p=GetProcAddress(dll,name);Check(p!=nullptr,name);return reinterpret_cast<T>(p);}
volatile uint32_t calls=0;void Apc(void* a,void* b,void* c){if(uint64_t(a)==11&&uint64_t(b)==22&&uint64_t(c)==33)++calls;}
}
int main(){try{
    const auto dll=GetModuleHandleW(L"ntdll.dll");Check(dll!=nullptr,"Native ntdll");
#define N(name) const auto name=Function<decltype(&guardian::native::name)>(dll,#name)
    N(RtlGetVersion);N(NtAllocateVirtualMemory);N(NtProtectVirtualMemory);N(NtFreeVirtualMemory);N(NtQueryVirtualMemory);
    N(NtCreateSection);N(NtMapViewOfSection);N(NtUnmapViewOfSection);N(NtClose);N(NtCreateEvent);N(NtSetEvent);N(NtWaitForSingleObject);N(NtDuplicateObject);
    N(NtCreateSemaphore);N(NtReleaseSemaphore);N(NtCreateTimer);N(NtSetTimer);N(NtCancelTimer);N(NtQueueApcThread);N(NtTestAlert);N(NtDelayExecution);N(NtQueryInformationProcess);N(NtQueryInformationThread);
    N(RtlCreateHeap);N(RtlAllocateHeap);N(RtlReAllocateHeap);N(RtlSizeHeap);N(RtlFreeHeap);N(RtlDestroyHeap);
    N(RtlNtStatusToDosError);
    Check(RtlNtStatusToDosError(NotImplemented)==1&&RtlNtStatusToDosError(Unsupported)==50&&RtlNtStatusToDosError(AccessViolation)==998&&RtlNtStatusToDosError(LengthMismatch)==24,"Native status error mapping");
    VersionInfo version;Check(!RtlGetVersion(&version),"Host version");const auto current=reinterpret_cast<void*>(CurrentProcess);
    ProcessBasic process;ThreadBasic thread;uint32_t returned=0;
    Check(!NtQueryInformationProcess(current,0,&process,sizeof(process),&returned)&&returned==sizeof(process)&&process.pid==GetCurrentProcessId(),"Process basic ABI");
    Check(!NtQueryInformationThread(reinterpret_cast<void*>(CurrentThread),0,&thread,sizeof(thread),&returned)&&thread.client.thread==GetCurrentThreadId(),"Thread basic ABI");
    void* base=nullptr;uint64_t bytes=12345;Check(!NtAllocateVirtualMemory(current,&base,0,&bytes,0x3000,4)&&bytes==16384,"VM page rounding");
    auto* data=static_cast<uint8_t*>(base);for(uint32_t i=0;i<bytes;++i)Check(!data[i],"VM zero fill");data[4096]=0x51;uint32_t old=0;
    MEMORY_BASIC_INFORMATION information{};uint64_t queried=0;Check(!NtQueryVirtualMemory(current,base,0,&information,sizeof(information),&queried)&&information.Protect==PAGE_READWRITE&&information.State==MEM_COMMIT,"VM query state");
    Check(!NtProtectVirtualMemory(current,&base,&bytes,2,&old)&&old==4&&data[4096]==0x51,"VM protection state");
    bytes=0;Check(!NtFreeVirtualMemory(current,&base,&bytes,0x8000),"VM release");
    Handle section=nullptr;int64_t maximum=8192;Check(!NtCreateSection(&section,0xF001F,nullptr,&maximum,4,0x8000000,nullptr),"Section create");
    void *a=nullptr,*b=nullptr;uint64_t na=0,nb=0;Check(!NtMapViewOfSection(section,current,&a,0,0,nullptr,&na,2,0,4)&&!NtMapViewOfSection(section,current,&b,0,0,nullptr,&nb,2,0,4),"Section views");
    *static_cast<uint32_t*>(a)=0x12345678;Check(*static_cast<uint32_t*>(b)==0x12345678&&!NtClose(section)&&!NtUnmapViewOfSection(current,a)&&*static_cast<uint32_t*>(b)==0x12345678&&!NtUnmapViewOfSection(current,b),"Shared section close/unmap lifetime");
    Handle event=nullptr,copy=nullptr;int64_t zero=0;Check(!NtCreateEvent(&event,0x1F0003,nullptr,0,0)&&NtWaitForSingleObject(event,0,&zero)==Timeout,"Event timeout");
    Check(!NtDuplicateObject(current,event,current,&copy,0,0,2)&&!NtClose(event)&&!NtSetEvent(copy,nullptr)&&!NtWaitForSingleObject(copy,0,&zero)&&!NtClose(copy),"Duplicate/event lifetime");
    Handle semaphore=nullptr;Check(!NtCreateSemaphore(&semaphore,0x1F0003,nullptr,0,2)&&!NtReleaseSemaphore(semaphore,1,nullptr)&&!NtWaitForSingleObject(semaphore,0,&zero)&&!NtClose(semaphore),"Semaphore");
    Handle timer=nullptr;int64_t due=-20000;Check(!NtCreateTimer(&timer,0x1F0003,nullptr,1)&&!NtSetTimer(timer,&due,nullptr,nullptr,0,0,nullptr)&&!NtWaitForSingleObject(timer,0,nullptr)&&!NtCancelTimer(timer,nullptr)&&!NtClose(timer),"Timer");
    Check(!NtQueueApcThread(reinterpret_cast<void*>(CurrentThread),reinterpret_cast<void*>(Apc),reinterpret_cast<void*>(11),reinterpret_cast<void*>(22),reinterpret_cast<void*>(33))&&!NtTestAlert()&&calls==1,"TestAlert APC and arguments");
    int64_t delay=-10000000;Check(!NtQueueApcThread(reinterpret_cast<void*>(CurrentThread),reinterpret_cast<void*>(Apc),reinterpret_cast<void*>(11),reinterpret_cast<void*>(22),reinterpret_cast<void*>(33))&&NtDelayExecution(1,&delay)==UserApc&&calls==2,"Alertable delay APC status");
    const auto heap=RtlCreateHeap(2,nullptr,65536,0,nullptr,nullptr);Check(heap!=nullptr,"Native heap creation");auto* block=static_cast<uint8_t*>(RtlAllocateHeap(heap,8,128));Check(block!=nullptr,"Native heap allocation");
    for(uint32_t i=0;i<128;++i)Check(!block[i],"Native heap zeroing");block[0]=0x51;block=static_cast<uint8_t*>(RtlReAllocateHeap(heap,8,block,256));
    Check(block&&block[0]==0x51&&RtlSizeHeap(heap,0,block)==256&&RtlFreeHeap(heap,0,block)&&!RtlDestroyHeap(heap),"Heap preservation and destruction");
    std::cout<<"{\"status\":\"PASS\",\"reference\":\"host native Windows\",\"nt\":["<<version.major<<','<<version.minor<<','<<version.build<<"],\"vm\":true,\"sections\":true,\"objects\":true,\"APC\":true,\"heap\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
