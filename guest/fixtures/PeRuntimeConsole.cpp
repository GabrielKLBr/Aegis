#include "PeTls.h"
#include "PeRuntimeApi.h"
#include "guardian/Runtime.h"
#include "guardian/HardwareProjection.h"
#include <intrin.h>
using namespace guardian::win;
__declspec(thread) volatile uint32_t tlsValue=0x2468;
volatile uint32_t tlsAttached=0,tlsDetached=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*) {
    if(reason==1 || reason==2)tlsValue=tlsValue+1;
    if(reason==2)tlsAttached=tlsAttached+1;if(reason==3)tlsDetached=tlsDetached+1;
}
extern "C" uint32_t GuardianRuntimeCall(guardian::runtime::Command*) noexcept;
#define REQUIRE(condition,code) if(!(condition))return code
namespace {
struct WorkerData {uint32_t number,pid,tid;uint64_t teb,stack;};
CriticalSection sharedSection{};volatile uint32_t sharedCount=0;
uint32_t Worker(void* argument){
    auto& d=*static_cast<WorkerData*>(argument);
    REQUIRE(tlsValue==0x2469 && GetCurrentProcessId()==d.pid && GetCurrentThreadId()==d.tid && GetLastError()==0,60);
    d.teb=__readgsqword(0x30);d.stack=__readgsqword(8);uint8_t local=0;
    REQUIRE(d.teb && d.teb!=0x620000 && reinterpret_cast<uint64_t>(&local)<d.stack && reinterpret_cast<uint64_t>(&local)>=__readgsqword(16),61);
    tlsValue=0x5000+d.number;SetLastError(0x7000+d.number);
    for(uint32_t i=0;i<12;++i){
        auto* b=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),8,32));REQUIRE(b,62);
        for(uint32_t j=0;j<32;++j){REQUIRE(b[j]==0,63);b[j]=static_cast<uint8_t>(d.number+j);}
        SwitchToThread();EnterCriticalSection(&sharedSection);const auto previous=sharedCount;
        REQUIRE(sharedSection.recursion==1 && sharedSection.owner==reinterpret_cast<Handle>(uint64_t(d.tid)),64);
        SwitchToThread();sharedCount=previous+1;
        auto* grown=static_cast<uint8_t*>(HeapReAlloc(GetProcessHeap(),8,b,96));REQUIRE(grown,65);
        for(uint32_t j=0;j<96;++j)REQUIRE(grown[j]==(j<32?static_cast<uint8_t>(d.number+j):0),66);
        LeaveCriticalSection(&sharedSection);REQUIRE(HeapFree(GetProcessHeap(),0,grown),67);SwitchToThread();
        REQUIRE(tlsValue==0x5000+d.number && GetLastError()==0x7000+d.number,68);
    }return 0x100+d.number;
}
uint32_t Abandon(void* argument){return WaitForSingleObject(argument,UINT32_MAX)==0?0xABC:0xBAD;}
uint32_t ThreadProof(){
    REQUIRE(InitializeCriticalSectionEx(&sharedSection,0,0),69);Handle retained=nullptr;uint32_t retainedCode=0;
    const auto mainTid=GetCurrentThreadId();SetLastError(0x1234);
    const auto* mainDllTls=reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint64_t*>(__readgsqword(0x58))[1]);
    uint8_t originalDllTls[256]{};for(uint32_t i=0;i<256;++i)originalDllTls[i]=mainDllTls[i];
    REQUIRE(!CreateThread(nullptr,0,reinterpret_cast<ThreadStart>(0x100000),nullptr,0,nullptr) && GetLastError()==87,70);
    REQUIRE(!CreateThread(nullptr,1,Worker,nullptr,0,nullptr) && GetLastError()==87,71);
    REQUIRE(!CreateThread(nullptr,0,Worker,nullptr,1,nullptr) && GetLastError()==87,72);
    for(uint32_t round=0;round<10;++round){
        WorkerData data[3]{};Handle handles[3]{};EnterCriticalSection(&sharedSection);
        for(uint32_t i=0;i<3;++i){data[i].number=round*3+i+1;data[i].pid=GetCurrentProcessId();
            handles[i]=CreateThread(nullptr,0,Worker,&data[i],4,&data[i].tid);uint32_t code=0;
            REQUIRE(handles[i] && data[i].tid && data[i].tid!=mainTid && GetExitCodeThread(handles[i],&code) && code==259 && WaitForSingleObject(handles[i],0)==0x102,73);
            REQUIRE(!ReleaseMutex(handles[i]) && GetLastError()==6,74);
        }
        REQUIRE(!CreateThread(nullptr,0,Worker,nullptr,4,nullptr) && GetLastError()==8,75);
        for(uint32_t i=0;i<3;++i)REQUIRE(ResumeThread(handles[i])==1,76);
        REQUIRE(ResumeThread(handles[0])==0,77);SwitchToThread();REQUIRE(WaitForSingleObject(handles[0],0)==0x102,78);LeaveCriticalSection(&sharedSection);
        for(uint32_t i=0;i<3;++i){uint32_t code=0;
            REQUIRE(WaitForSingleObject(handles[i],UINT32_MAX)==0 && GetExitCodeThread(handles[i],&code) && code==0x100+data[i].number,79);
            REQUIRE(WaitForSingleObject(handles[i],0)==0,80);
            if(!round && !i){retained=handles[i];retainedCode=code;}else {REQUIRE(CloseHandle(handles[i]),81);REQUIRE(!GetExitCodeThread(handles[i],&code) && GetLastError()==6,82);}
        }
        REQUIRE(data[0].teb!=data[1].teb && data[0].teb!=data[2].teb && data[1].teb!=data[2].teb && data[0].stack!=data[1].stack && data[0].stack!=data[2].stack && data[1].stack!=data[2].stack,83);
        uint32_t code=0;REQUIRE(GetExitCodeThread(retained,&code) && code==retainedCode && WaitForSingleObject(retained,0)==0,84);
        for(uint32_t i=0;i<256;++i)REQUIRE(mainDllTls[i]==originalDllTls[i],92);
        REQUIRE(tlsValue==0x2469 && GetCurrentThreadId()==mainTid && tlsAttached==(round+1)*3 && tlsDetached==tlsAttached,85);
    }
    REQUIRE(sharedCount==360 && CloseHandle(retained),86);DeleteCriticalSection(&sharedSection);REQUIRE(!sharedSection.semaphore,87);
    const auto mutex=CreateMutexA(nullptr,0,nullptr);const auto worker=CreateThread(nullptr,0,Abandon,mutex,0,nullptr);uint32_t code=0;
    REQUIRE(mutex && worker && WaitForSingleObject(worker,UINT32_MAX)==0 && GetExitCodeThread(worker,&code) && code==0xABC,88);
    REQUIRE(WaitForSingleObject(mutex,0)==0x80 && ReleaseMutex(mutex) && CloseHandle(mutex) && CloseHandle(worker),89);
    REQUIRE(tlsAttached==31 && tlsDetached==31,90);SetLastError(0x1234);return 0;
}
}
extern "C" uint32_t GuardianEntry(){
    uint64_t available=0,total=0,free=0,wideAvailable=0;
    REQUIRE(GetDiskFreeSpaceExA("C:\\",&available,&total,&free) && total==16384 && available<=free && free<=total,144);
    const uint16_t diskRoot[]={'c',':','\\',0};
    REQUIRE(GetDiskFreeSpaceExW(diskRoot,&wideAvailable,nullptr,nullptr) && wideAvailable==available,145);
    REQUIRE(!GetDiskFreeSpaceExA("D:\\",&available,&total,&free) && GetLastError()==87,146);
    // Old contracts have no optional hardware extension. Profiled boots must
    // expose the same CPU label through actual CPL3 API calls and CPUID.
    const uint32_t firmwareBytes=GetSystemFirmwareTable(guardian::hardware::Rsmb,0,nullptr,0);
    if(firmwareBytes){
        uint8_t firmware[2048]{};char brand[49]{};uint8_t registryBrand[64]{};
        REQUIRE(firmwareBytes<=sizeof(firmware),130);
        REQUIRE(GetSystemFirmwareTable(guardian::hardware::Rsmb,0,firmware,1)==firmwareBytes && !firmware[0],131);
        REQUIRE(GetSystemFirmwareTable(guardian::hardware::Rsmb,0,firmware,sizeof(firmware))==firmwareBytes && firmware[1]==3,132);
        REQUIRE(!GetSystemFirmwareTable(guardian::hardware::Rsmb,1,firmware,sizeof(firmware)),133);
        for(uint32_t n=0;n<3;++n){int values[4]{};__cpuid(values,static_cast<int>(0x80000002u+n));for(uint32_t k=0;k<16;++k)brand[n*16+k]=reinterpret_cast<char*>(values)[k];}
        Handle hardwareKey=nullptr;const auto machine=reinterpret_cast<Handle>(guardian::runtime::LocalMachine);
        REQUIRE(RegOpenKeyExA(machine,"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",0,1,&hardwareKey)==0,134);
        uint32_t type=0,size=0;REQUIRE(RegQueryValueExA(hardwareKey,"ProcessorNameString",nullptr,&type,nullptr,&size)==0 && size<=sizeof(registryBrand) && type==1,135);
        REQUIRE(RegQueryValueExA(hardwareKey,"ProcessorNameString",nullptr,&type,registryBrand,&size)==0,136);
        for(uint32_t n=0;n<size;++n)REQUIRE(registryBrand[n]==static_cast<uint8_t>(brand[n]),137);
        REQUIRE(RegSetValueExA(hardwareKey,"ProcessorNameString",0,1,registryBrand,size)==5 && RegCloseKey(hardwareKey)==0,138);
        uint32_t at=8;bool found=false;
        while(at+4<=firmwareBytes){const auto kind=firmware[at],length=firmware[at+1];REQUIRE(length>=4 && at+length<=firmwareBytes,139);
            uint32_t end=at+length;while(end+1<firmwareBytes && (firmware[end]||firmware[end+1]))++end;REQUIRE(end+1<firmwareBytes,140);
            if(kind==4){uint32_t index=1,start=at+length;while(index<firmware[at+16] && start<end){while(start<end && firmware[start])++start;++start;++index;}
                REQUIRE(index==firmware[at+16],141);for(uint32_t n=0;n<49;++n){REQUIRE(start+n<=end && firmware[start+n]==static_cast<uint8_t>(brand[n]),142);if(!brand[n])break;}found=true;}
            at=end+2;
        }
        REQUIRE(found && at==firmwareBytes,143);
    }
    REQUIRE(tlsValue==0x2469 && GetCurrentProcessId() && GetCurrentThreadId(),10);
    SetLastError(0x1234);REQUIRE(GetLastError()==0x1234,11);
    const auto threadResult=ThreadProof();if(threadResult)return threadResult;
    const auto heap=GetProcessHeap();auto* data=static_cast<uint8_t*>(HeapAlloc(heap,8,64));REQUIRE(data && !(reinterpret_cast<uint64_t>(data)&15),12);
    for(uint32_t i=0;i<64;++i){REQUIRE(!data[i],13);data[i]=static_cast<uint8_t>(i+1);}
    data=static_cast<uint8_t*>(HeapReAlloc(heap,8,data,256));REQUIRE(data,14);
    for(uint32_t i=0;i<256;++i)REQUIRE(data[i]==(i<64?i+1:0),15);
    REQUIRE(!HeapReAlloc(heap,0,data,16384),16);for(uint32_t i=0;i<64;++i)REQUIRE(data[i]==i+1,17);
    REQUIRE(HeapFree(heap,0,data) && !HeapFree(heap,0,data) && GetLastError()==87,18);
    CriticalSection cs{};REQUIRE(InitializeCriticalSectionEx(&cs,0,0),19);EnterCriticalSection(&cs);EnterCriticalSection(&cs);REQUIRE(TryEnterCriticalSection(&cs) && cs.recursion==3,20);
    LeaveCriticalSection(&cs);LeaveCriticalSection(&cs);LeaveCriticalSection(&cs);REQUIRE(!cs.recursion && !cs.owner && cs.lockCount==-1,21);DeleteCriticalSection(&cs);REQUIRE(!cs.semaphore,22);
    const auto mutex=CreateMutexA(nullptr,0,nullptr);REQUIRE(mutex && WaitForSingleObject(mutex,0)==0 && WaitForSingleObject(mutex,0)==0,23);
    REQUIRE(ReleaseMutex(mutex) && ReleaseMutex(mutex),24);REQUIRE(!ReleaseMutex(mutex) && GetLastError()==288 && CloseHandle(mutex),51);REQUIRE(WaitForSingleObject(mutex,0)==UINT32_MAX && GetLastError()==6,25);
    const auto invalid=reinterpret_cast<Handle>(UINT64_MAX);uint32_t bytes=0;
    REQUIRE(CreateFileA("C:\\absent-runtime.bin",0x80000000,3,nullptr,3,0,nullptr)==invalid && GetLastError()==2,26);
    const auto file=CreateFileA("C:\\runtime.bin",0xC0000000,3,nullptr,4,0,nullptr);REQUIRE(file!=invalid,27);const bool recovered=GetLastError()==183;
    uint32_t prior=0;if(recovered)REQUIRE(ReadFile(file,&prior,4,&bytes,nullptr) && bytes==4 && prior>0,28);
    const auto root=reinterpret_cast<Handle>(guardian::runtime::CurrentUser);Handle key=nullptr;uint32_t disposition=0;
    SetLastError(0x4321);REQUIRE(RegCreateKeyExA(root,"Runtime",0,nullptr,0,0x2001F,nullptr,&key,&disposition)==0 && GetLastError()==0x4321,29);
    uint32_t value=0,n=4,type=0;const auto query=RegQueryValueExA(key,"Counter",nullptr,&type,reinterpret_cast<uint8_t*>(&value),&n);
    REQUIRE(recovered?(query==0 && type==4 && n==4 && value==prior):(query==2),30);
    value=prior+1;REQUIRE(SetFilePointerEx(file,0,nullptr,0) && WriteFile(file,&value,4,&bytes,nullptr) && bytes==4,31);
    int64_t size=0;REQUIRE(GetFileSizeEx(file,&size) && size==4 && SetFilePointerEx(file,-4,nullptr,2),32);
    uint32_t read=0;REQUIRE(ReadFile(file,&read,4,&bytes,nullptr) && bytes==4 && read==value,33);REQUIRE(ReadFile(file,&read,4,&bytes,nullptr) && !bytes,34);
    REQUIRE(!SetFilePointerEx(file,-1,nullptr,0) && GetLastError()==131,35);
    SetLastError(0x4321);REQUIRE(RegSetValueExA(key,"Counter",0,4,reinterpret_cast<const uint8_t*>(&value),4)==0 && GetLastError()==0x4321,36);
    uint8_t shortBuffer=0xAA;n=1;REQUIRE(RegQueryValueExA(key,"Counter",nullptr,&type,&shortBuffer,&n)==234 && n==4 && shortBuffer==0xAA,37);
    n=0;REQUIRE(RegQueryValueExA(key,"Counter",nullptr,&type,nullptr,&n)==0 && n==4 && type==4,38);
    Handle readOnly=nullptr;REQUIRE(RegOpenKeyExA(root,"Runtime",0,1,&readOnly)==0,39);
    REQUIRE(RegSetValueExA(readOnly,"Counter",0,4,reinterpret_cast<const uint8_t*>(&prior),4)==5 && RegCloseKey(readOnly)==0,40);
    REQUIRE(CreateFileA("C:\\runtime.bin",0x80000000,0,nullptr,3,0,nullptr)==invalid && GetLastError()==32,47);
    REQUIRE(CreateFileA("C:\\runtime.bin",0xC0000000,3,nullptr,1,0,nullptr)==invalid && GetLastError()==80,48);
    SetLastError(0x4321);REQUIRE(RegSetValueExA(key,"String",0,1,reinterpret_cast<const uint8_t*>("x"),2)==50 && GetLastError()==0x4321,49);
    guardian::runtime::Command malformed{};malformed.version=0;REQUIRE(GuardianRuntimeCall(&malformed)==0xC000000D,50);
    REQUIRE(FlushFileBuffers(file) && CloseHandle(file) && RegCloseKey(key)==0,41);
    REQUIRE(!ReadFile(file,&read,4,&bytes,nullptr) && GetLastError()==6 && !bytes,42);
    REQUIRE(GuardianRuntimeCall(reinterpret_cast<guardian::runtime::Command*>(0x100000))==0xC0000022,43);
    const char text[]="PE RUNTIME OK\n";REQUIRE(WriteFile(GetStdHandle(uint32_t(-11)),text,sizeof(text)-1,&bytes,nullptr) && bytes==sizeof(text)-1,44);
    // The kernel must also reclaim handles intentionally left at process exit.
    REQUIRE(CreateFileA("C:\\runtime-leak.bin",0xC0000000,3,nullptr,4,0,nullptr)!=invalid,45);
    REQUIRE(RegOpenKeyExA(root,"Runtime",0,1,&readOnly)==0,46);
    return 0;
}
