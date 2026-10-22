#include "PeTls.h"
#include "PeRuntimeApi.h"
#include <intrin.h>
using namespace guardian::win;
__declspec(thread) volatile uint32_t childTls=0x1001;
volatile uint32_t attached=0,detached=0;Handle gate=nullptr;
bool Equal(const char* a,const char* b){for(uint32_t i=0;i<128;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
extern "C" void TlsCallback(void*,uint32_t reason,void*){
    if(reason==1 || reason==2)childTls=childTls+1;if(reason==2)attached=attached+1;if(reason==3)detached=detached+1;
    if(reason==0){const auto* command=GetCommandLineA();
        if(Equal(command,"worker") && (attached!=1 || detached!=1))__ud2();
        if(Equal(command,"primary") && (attached!=1 || detached!=2))__ud2();
        if(Equal(command,"process") && (attached || detached))__ud2();
        if(Equal(command,"thread") && (attached || detached!=1))__ud2();}
}
#define REQUIRE(c,n) if(!(c))return n
uint32_t ExplicitWorker(void*){REQUIRE(childTls==0x1002 && GetLastError()==0,1001);SetLastError(0x123456);SwitchToThread();REQUIRE(GetLastError()==0x123456,1002);ExitThread(44);}
uint32_t RemainingWorker(void*){
    REQUIRE(WaitForSingleObject(gate,UINT32_MAX)==0x80 && detached==1 && childTls==0x1002 && GetProcessHeap(),1003);
    auto* b=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),8,64));REQUIRE(b,1004);for(uint32_t i=0;i<64;++i)REQUIRE(!b[i],1005);
    REQUIRE(HeapFree(GetProcessHeap(),0,b) && ReleaseMutex(gate) && CloseHandle(gate),1006);return 66;
}
extern "C" uint32_t GuardianEntry(){
    REQUIRE(childTls==0x1002 && GetCurrentProcessId() && GetCurrentThreadId() && GetLastError()==0 && GetProcessHeap(),1010);
    auto* b=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),8,64));REQUIRE(b,1011);for(uint32_t i=0;i<64;++i){REQUIRE(!b[i],1012);b[i]=0x5A;}
    REQUIRE(HeapFree(GetProcessHeap(),0,b),1013);const auto* command=GetCommandLineA();
    if(Equal(command,"return") || !*command)return 11;
    if(Equal(command,"thread"))ExitThread(33);
    if(Equal(command,"worker")){const auto h=CreateThread(nullptr,0,ExplicitWorker,nullptr,0,nullptr);uint32_t code=0;
        REQUIRE(h && WaitForSingleObject(h,UINT32_MAX)==0 && GetExitCodeThread(h,&code) && code==44 && CloseHandle(h),1014);return 45;}
    if(Equal(command,"primary")){gate=CreateMutexA(nullptr,0,nullptr);REQUIRE(gate && WaitForSingleObject(gate,0)==0,1015);
        const auto h=CreateThread(nullptr,0,RemainingWorker,nullptr,0,nullptr);REQUIRE(h && CloseHandle(h),1016);ExitThread(55);}
    if(Equal(command,"process")){const auto h=CreateThread(nullptr,0,ExplicitWorker,nullptr,4,nullptr);REQUIRE(h && HeapAlloc(GetProcessHeap(),8,32),1017);ExitProcess(22);}
    return 1099;
}
