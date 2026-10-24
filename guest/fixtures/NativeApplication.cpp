#include "NativeApi.h"
#include "PeRuntimeApi.h"
#include <intrin.h>
using namespace guardian::native;
extern "C" uint32_t GuardianNativeUnknown();
extern "C" uint32_t GuardianNativeCollision();
namespace {
Handle Current(){return reinterpret_cast<Handle>(CurrentProcess);}Handle Thread(){return reinterpret_cast<Handle>(CurrentThread);}
volatile uint32_t apcs=0,destructors=0;uint32_t flsIndex=0;
volatile uint32_t forcedDestructors=0;
void Apc(void* a,void* b,void* c){if(reinterpret_cast<uint64_t>(a)==11&&reinterpret_cast<uint64_t>(b)==22&&reinterpret_cast<uint64_t>(c)==33)++apcs;}
#ifndef GUARDIAN_NATIVE_REFERENCE
void FlsDelete(void* p){if(reinterpret_cast<uint64_t>(p)==77)++destructors;if(reinterpret_cast<uint64_t>(p)==99)++forcedDestructors;}
uint32_t ForcedWorker(void*){if(RtlFlsSetValue(flsIndex,reinterpret_cast<void*>(99)))return 93;
    NtTerminateThread(Thread(),0);return 94;}
uint32_t Worker(void*){ThreadBasic basic;uint32_t bytes=0;void* value=reinterpret_cast<void*>(1);
    if(NtQueryInformationThread(Thread(),0,&basic,sizeof(basic),&bytes)||basic.teb!=__readgsqword(0x30))return 91;
    if(RtlFlsGetValue(flsIndex,&value)||value||RtlFlsSetValue(flsIndex,reinterpret_cast<void*>(77)))return 92;return 0;}
#endif
int Run(){
    VersionInfo version;if(RtlGetVersion(&version)||!version.major||!version.build)return 1;
    ProcessBasic process;ThreadBasic thread;uint32_t written=0;
    if(NtQueryInformationProcess(Current(),0,&process,sizeof(process),&written)||written!=sizeof(process)||process.peb!=__readgsqword(0x60))return 2;
    if(NtQueryInformationThread(Thread(),0,&thread,sizeof(thread),&written)||thread.teb!=__readgsqword(0x30)||process.pid!=thread.client.process)return 3;
    Attributes empty;ClientId client{process.pid,0};Handle opened=nullptr;ProcessBasic openedInfo;
    if(NtOpenProcess(&opened,0x1000,&empty,&client)||NtQueryInformationProcess(opened,0,&openedInfo,sizeof(openedInfo),&written)||openedInfo.pid!=process.pid||NtClose(opened))return 41;
    client.thread=thread.client.thread;if(NtOpenThread(&opened,0x40,&empty,&client)||NtClose(opened))return 42;
    uint64_t system[8]{};if(NtQuerySystemInformation(0,system,sizeof(system),&written)||written!=sizeof(system))return 43;
    Unicode first,second;RtlInitUnicodeString(&first,reinterpret_cast<const uint16_t*>(L"Native"));RtlInitUnicodeString(&second,reinterpret_cast<const uint16_t*>(L"Native"));
    if(first.length!=12||first.maximum!=14||!RtlEqualUnicodeString(&first,&second,0)||RtlNtStatusToDosError(Denied)!=5||RtlNtStatusToDosError(NotImplemented)!=1||RtlNtStatusToDosError(AccessViolation)!=998)return 4;
    void* memory=nullptr;uint64_t bytes=12345;if(NtAllocateVirtualMemory(Current(),&memory,0,&bytes,0x3000,4)||bytes!=16384||!memory)return 5;
    auto* data=static_cast<volatile uint8_t*>(memory);for(uint32_t i=0;i<bytes;++i)if(data[i])return 6;data[4096]=0x51;
    guardian::memory::Information info{};uint64_t returned=0;if(NtQueryVirtualMemory(Current(),memory,0,&info,sizeof(info),&returned)||info.protect!=4||info.state!=0x1000||returned!=sizeof(info))return 7;
    uint32_t previous=0;if(NtProtectVirtualMemory(Current(),&memory,&bytes,2,&previous)||previous!=4||data[4096]!=0x51)return 8;
    bytes=0;if(NtFreeVirtualMemory(Current(),&memory,&bytes,0x8000))return 9;
    bytes=4096;if(NtAllocateVirtualMemory(Current(),reinterpret_cast<void**>(0x100000),0,&bytes,0x3000,4)!=AccessViolation)return 10;
    Handle section=nullptr;int64_t maximum=8192;if(NtCreateSection(&section,0xF001F,nullptr,&maximum,4,0x8000000,nullptr))return 11;
    void *a=nullptr,*b=nullptr;uint64_t sizeA=0,sizeB=0;
    if(NtMapViewOfSection(section,Current(),&a,0,0,nullptr,&sizeA,2,0,4)||NtMapViewOfSection(section,Current(),&b,0,0,nullptr,&sizeB,2,0,4))return 12;
    *static_cast<volatile uint32_t*>(a)=0x12345678;if(*static_cast<volatile uint32_t*>(b)!=0x12345678||NtClose(section))return 13;
    if(NtUnmapViewOfSection(Current(),a)||*static_cast<volatile uint32_t*>(b)!=0x12345678||NtUnmapViewOfSection(Current(),b))return 14;
    Handle event=nullptr;if(NtCreateEvent(&event,0x1F0003,nullptr,0,0))return 15;int64_t zero=0;
    if(NtWaitForSingleObject(event,0,&zero)!=Timeout||NtSetEvent(event,nullptr)||NtWaitForSingleObject(event,0,&zero)||NtResetEvent(event,nullptr))return 16;
    Handle duplicate=nullptr;if(NtDuplicateObject(Current(),event,Current(),&duplicate,0,0,2)||NtClose(event)||NtSetEvent(duplicate,nullptr)||NtClose(duplicate))return 17;
    Handle pair[2]{};uint32_t objectInfo[14]{};
    if(NtCreateEvent(&pair[0],0x1F0003,nullptr,0,1)||NtCreateEvent(&pair[1],0x1F0003,nullptr,0,0)||NtWaitForMultipleObjects(2,pair,1,0,&zero)||NtQueryObject(pair[0],0,objectInfo,sizeof(objectInfo),&written)||objectInfo[1]!=0x1F0003||!objectInfo[2]||NtClose(pair[0])||NtClose(pair[1]))return 44;
    Handle mutant=nullptr;int32_t previousCount=-1;
    if(NtCreateMutant(&mutant,0x1F0001,nullptr,1)||NtReleaseMutant(mutant,&previousCount)||previousCount||NtClose(mutant))return 45;
    Handle semaphore=nullptr;if(NtCreateSemaphore(&semaphore,0x1F0003,nullptr,0,2)||NtReleaseSemaphore(semaphore,1,nullptr)||NtWaitForSingleObject(semaphore,0,&zero)||NtClose(semaphore))return 18;
    Handle timer=nullptr;int64_t due=-20000;if(NtCreateTimer(&timer,0x1F0003,nullptr,1)||NtSetTimer(timer,&due,nullptr,nullptr,0,0,nullptr)||NtWaitForSingleObject(timer,0,nullptr)||NtCancelTimer(timer,nullptr)||NtClose(timer))return 19;
    if(NtQueueApcThread(Thread(),reinterpret_cast<void*>(Apc),reinterpret_cast<void*>(11),reinterpret_cast<void*>(22),reinterpret_cast<void*>(33))||NtTestAlert()||apcs!=1)return 20;
    int64_t delay=-10000000;if(NtQueueApcThread(Thread(),reinterpret_cast<void*>(Apc),reinterpret_cast<void*>(11),reinterpret_cast<void*>(22),reinterpret_cast<void*>(33))||NtDelayExecution(1,&delay)!=UserApc||apcs!=2)return 37;
    int64_t time=0,counter=0,frequency=0;if(NtQuerySystemTime(&time)||time<=0||NtQueryPerformanceCounter(&counter,&frequency)||counter<0||frequency<=0)return 21;
    Unicode path;RtlInitUnicodeString(&path,reinterpret_cast<const uint16_t*>(L"\\??\\C:\\native.bin"));Attributes attrs;attrs.name=reinterpret_cast<uint64_t>(&path);attrs.attributes=0x40;
    IoStatus io;Handle file=nullptr;if(NtCreateFile(&file,0xC0100000,&attrs,&io,nullptr,0,7,3,0x60,nullptr,0))return 22;
    const uint64_t content=0x123456789ABCDEF0;int64_t offset=0;uint64_t read=0;
    if(NtWriteFile(file,nullptr,nullptr,nullptr,&io,&content,8,&offset,nullptr)||io.information!=8||NtReadFile(file,nullptr,nullptr,nullptr,&io,&read,8,&offset,nullptr)||read!=content||NtClose(file))return 23;
    uint64_t standard[3]{};if(NtOpenFile(&file,0x80100000,&attrs,&io,7,0x60)||NtQueryInformationFile(file,&io,standard,sizeof(standard),5)||standard[1]!=8||NtClose(file)||NtFlushInstructionCache(Current(),reinterpret_cast<void*>(GuardianNativeUnknown),16))return 46;
#ifndef GUARDIAN_NATIVE_REFERENCE
    uint32_t caught=0;__try {guardian::exceptions::Record exception;exception.code=0xE0143001;RtlRaiseException(&exception);}__except(1){caught=1;}
    if(!caught)return 38;void* frames[4]{};uint32_t hash=0;if(!RtlCaptureStackBackTrace(0,4,frames,&hash)||!frames[0])return 39;
    Unicode keyPath,valueName;RtlInitUnicodeString(&keyPath,reinterpret_cast<const uint16_t*>(L"HKCU\\Software\\NativeProbe"));attrs.name=reinterpret_cast<uint64_t>(&keyPath);uint32_t disposition=0;Handle key=nullptr;
    if(NtCreateKey(&key,0xF003F,&attrs,0,nullptr,0,&disposition))return 24;
    if(NtOpenKey(&opened,1,&attrs)||NtClose(opened))return 47;
    RtlInitUnicodeString(&valueName,reinterpret_cast<const uint16_t*>(L"Value"));uint32_t number=0x1234,buffer[8]{};
    if(NtSetValueKey(key,&valueName,0,4,&number,4)||NtQueryValueKey(key,&valueName,2,buffer,sizeof(buffer),&written)||written!=16||buffer[1]!=4||buffer[2]!=4||buffer[3]!=number)return 25;
    if(NtDeleteValueKey(key,&valueName)||NtDeleteKey(key)||NtClose(key))return 26;
    const auto heap=RtlCreateHeap(2,nullptr,65536,0,nullptr,nullptr);auto* block=static_cast<uint8_t*>(RtlAllocateHeap(heap,8,128));if(!heap||!block)return 27;
    for(uint32_t i=0;i<128;++i)if(block[i])return 28;block[0]=0x51;block=static_cast<uint8_t*>(RtlReAllocateHeap(heap,8,block,256));if(!block||block[0]!=0x51||RtlSizeHeap(heap,0,block)!=256)return 29;
    if(!RtlFreeHeap(heap,0,block)||RtlDestroyHeap(heap))return 30;
    if(RtlFlsAlloc(FlsDelete,&flsIndex)||RtlFlsSetValue(flsIndex,reinterpret_cast<void*>(55)))return 31;
    Handle worker=nullptr;uint32_t previousSuspend=0;
    if(NtCreateThreadEx(&worker,0x1FFFFF,nullptr,Current(),reinterpret_cast<void*>(Worker),nullptr,1,0,0,0,nullptr)||!worker||NtResumeThread(worker,&previousSuspend)||previousSuspend!=1||NtWaitForSingleObject(worker,0,nullptr)||NtClose(worker)||destructors!=1)return 32;
    if(NtCreateThreadEx(&worker,0x1FFFFF,nullptr,Current(),reinterpret_cast<void*>(ForcedWorker),nullptr,0,0,0,0,nullptr)||NtWaitForSingleObject(worker,0,nullptr)||NtClose(worker)||forcedDestructors)return 40;
    void* value=nullptr;if(RtlFlsGetValue(flsIndex,&value)||reinterpret_cast<uint64_t>(value)!=55||RtlFlsFree(flsIndex)||forcedDestructors)return 33;
    Unicode module;RtlInitUnicodeString(&module,reinterpret_cast<const uint16_t*>(L"guardian-runtime-api.dll"));Handle handle=nullptr;
    if(LdrLoadDll(nullptr,nullptr,&module,&handle))return 34;Ansi symbol;RtlInitAnsiString(&symbol,"GetCurrentProcessId");void* procedure=nullptr;
    if(LdrGetProcedureAddress(handle,&symbol,0,&procedure)||!procedure||reinterpret_cast<uint32_t(*)()>(procedure)()!=process.pid||LdrUnloadDll(handle))return 35;
#endif
    if(GuardianNativeUnknown()!=NotImplemented||GuardianNativeCollision()!=NotImplemented)return 36;return 0;
}
}
extern "C" int GuardianEntry(){const auto result=Run();const char pass[]="Native NT matrix memory sections objects threads APCs files RTL Ldr PASS\n";
    uint32_t wrote=0;if(!result)guardian::win::WriteFile(guardian::win::GetStdHandle(uint32_t(-11)),pass,sizeof(pass)-1,&wrote,nullptr);return result;}
const void* volatile nativeRelocationAnchor=reinterpret_cast<const void*>(GuardianEntry);
