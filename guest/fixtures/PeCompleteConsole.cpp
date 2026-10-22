#define GUARDIAN_LIFETIME_ENTRY GuardianPreviousProof
#include "PeLifetimeConsole.cpp"
#undef GUARDIAN_LIFETIME_ENTRY
extern "C" uint64_t GuardianFaultUd();
extern "C" void GuardianFaultDivide();
extern "C" unsigned long _exception_code();
extern "C" void* _exception_info();
namespace {
using namespace guardian::exceptions;
volatile uint32_t searched=0,cleaned=0,handled=0,continued=0;
int32_t ContinueFault(Pointers* p){
    if(p->record->code!=IllegalInstruction || p->record->count || p->context->cs!=0x2B || p->context->ss!=0x23)return 0;
    ++continued;p->context->rip+=2;p->context->gpr[0]=0x1234;SwitchToThread();return -1;
}
__declspec(noinline) uint32_t ContinueProof(){
    uint64_t value=0;
    __try {value=GuardianFaultUd();}
    __except(ContinueFault(static_cast<Pointers*>(_exception_info()))){return 201;}
    return value==0x1234 && continued==1?0:202;
}
int32_t SearchFilter(Pointers*){++searched;return 0;}
__declspec(noinline) void InnerProof(){
    __try {GuardianFaultDivide();}
    __except(SearchFilter(static_cast<Pointers*>(_exception_info()))){handled=100;}
}
__declspec(noinline) void FinallyProof(){__try {InnerProof();}__finally {++cleaned;SwitchToThread();}}
__declspec(noinline) uint32_t UnwindProofComplete(){
    __try {FinallyProof();return 203;}
    __except(_exception_code()==IntegerDivide?1:0){++handled;}
    return searched==1 && cleaned==1 && handled==1?0:204;
}
int32_t RaisedFilter(Pointers* p){return p->record->code==0xE1234567 && p->record->count==2 && p->record->information[0]==7 && p->record->information[1]==11?1:0;}
uint32_t RaiseProof(){
    __try {const uint64_t values[2]={7,11};RaiseException(0xE1234567,0,2,values);return 205;}
    __except(RaisedFilter(static_cast<Pointers*>(_exception_info()))){++handled;}
    return handled==2?0:206;
}
uint32_t FailureProof(){
    const char* modes[]={"unhandled","badcontext","nested","noncontinuable","badmxcsr","badtarget","kernelscratch"};
    const uint32_t expected[]={IllegalInstruction,InvalidDisposition,IllegalInstruction,NoncontinuableException,InvalidDisposition,InvalidUnwindTarget,AccessViolation};
    StartupInfoA startup{};startup.cb=sizeof(startup);
    for(uint32_t i=0;i<7;++i){ProcessInformation info{};uint32_t code=0;char command[64]{};
        for(uint32_t n=0;modes[i][n];++n)command[n]=modes[i][n];
        REQUIRE(CreateProcessA("guardian-lifetime-child.exe",command,nullptr,nullptr,0,0,nullptr,nullptr,&startup,&info),207);
        REQUIRE(WaitForSingleObject(info.process,UINT32_MAX)==0 && GetExitCodeProcess(info.process,&code) && code==expected[i],208+i);
        REQUIRE(CloseHandle(info.thread) && CloseHandle(info.process),214);
    }return 0;
}
struct ExistingData {volatile uint32_t phase=0;Handle module=nullptr;};
uint32_t ExistingDynamicThread(void* raw){auto& d=*static_cast<ExistingData*>(raw);d.phase=1;while(d.phase!=2)SwitchToThread();
    const auto module=LoadLibraryA("guardian-dynamic.dll");if(module!=d.module)return 230;
    const auto value=reinterpret_cast<uint32_t (*)()>(GetProcAddress(module,"DynamicValue"));
    return value && value()==0x3140 && FreeLibrary(module)?0:231;
}
uint32_t DynamicWorker(void* raw){const auto original=static_cast<Handle>(raw);
    for(uint32_t i=0;i<8;++i){const auto module=LoadLibraryA("GUARDIAN-DYNAMIC.DLL");if(module!=original)return 232;
        const auto value=reinterpret_cast<uint32_t (*)()>(GetProcAddress(module,reinterpret_cast<const char*>(1)));
        if(!value || value()!=0x3141)return 233;
        // Zero means there is no ready peer; it is a valid Win32 result.
        SwitchToThread();if(!FreeLibrary(module))return 233;}return 0;
}
uint32_t LoneYieldWorker(void*){
    // The parent waits for this worker. At least one yield must observe that
    // no other thread is ready; do not require a context switch to succeed.
    for(uint32_t i=0;i<256;++i)if(!SwitchToThread())return 0;
    return 254;
}
uint32_t LoneYieldProof(){
    const auto worker=CreateThread(nullptr,0,LoneYieldWorker,nullptr,0,nullptr);REQUIRE(worker,254);uint32_t code=UINT32_MAX;
    REQUIRE(WaitForSingleObject(worker,UINT32_MAX)==0 && GetExitCodeThread(worker,&code) && !code && CloseHandle(worker),255);return 0;
}
uint32_t DynamicProof(){
    REQUIRE(!LoadLibraryA("missing.dll") && GetLastError()==126 && !LoadLibraryA("../guardian-dynamic.dll"),234);
    REQUIRE(!FreeLibrary(nullptr) && GetLastError()==6,235);
    for(uint32_t round=0;round<4;++round){ExistingData data{};const auto existing=CreateThread(nullptr,0,ExistingDynamicThread,&data,0,nullptr);REQUIRE(existing,236);
        while(data.phase!=1)SwitchToThread();const auto module=LoadLibraryA("guardian-dynamic.dll");REQUIRE(module,237);data.module=module;
        const auto value=reinterpret_cast<uint32_t (*)()>(GetProcAddress(module,"DynamicValue"));
        const auto stats=reinterpret_cast<uint32_t (*)(uint32_t)>(GetProcAddress(module,"DynamicStats"));
        const auto bind=reinterpret_cast<void (*)(uint32_t*)>(GetProcAddress(module,"DynamicBind"));
        const auto raise=reinterpret_cast<void (*)()>(GetProcAddress(module,"DynamicRaise"));
        REQUIRE(value && stats && bind && raise && value()==0x3141 && stats(0)==1 && stats(1)==0 && stats(2)==0,238);
        REQUIRE(GetProcAddress(module,reinterpret_cast<const char*>(1))==reinterpret_cast<Procedure>(value),239);
        REQUIRE(!GetProcAddress(module,"MissingSymbol") && GetLastError()==127,240);
        const auto again=LoadLibraryA("guardian-dynamic.dll");REQUIRE(again==module && stats(0)==1 && FreeLibrary(again) && value()==0x3141,241);
        data.phase=2;uint32_t code=0;REQUIRE(WaitForSingleObject(existing,UINT32_MAX)==0 && GetExitCodeThread(existing,&code) && !code && CloseHandle(existing),242);
        Handle workers[2]{};for(auto& worker:workers){worker=CreateThread(nullptr,0,DynamicWorker,module,0,nullptr);REQUIRE(worker,243);}
        for(auto worker:workers)REQUIRE(WaitForSingleObject(worker,UINT32_MAX)==0 && GetExitCodeThread(worker,&code) && !code && CloseHandle(worker),244);
        REQUIRE(stats(1)==2 && stats(2)==3,245);uint32_t observer=0;bind(&observer);
        __try {raise();return 246;}__except(_exception_code()==0xE7654321?1:0){REQUIRE(observer==1,247);}
        REQUIRE(FreeLibrary(module) && observer==2 && !FreeLibrary(module) && GetLastError()==6,248);
        REQUIRE(!GetProcAddress(module,"DynamicValue") && GetLastError()==127,249);
        REQUIRE(!LoadLibraryA("guardian-failing.dll") && GetLastError()==1114,250);
        REQUIRE(!LoadLibraryA("guardian-failing.dll") && GetLastError()==1114,250);
    }
    StartupInfoA startup{};startup.cb=sizeof(startup);const char* modes[]={"attachfault","detachfault","leakdll"};
    for(uint32_t i=0;i<3;++i){ProcessInformation info{};char command[32]{};for(uint32_t n=0;modes[i][n];++n)command[n]=modes[i][n];uint32_t code=0;
        REQUIRE(CreateProcessA("guardian-lifetime-child.exe",command,nullptr,nullptr,0,0,nullptr,nullptr,&startup,&info),251);
        REQUIRE(WaitForSingleObject(info.process,UINT32_MAX)==0 && GetExitCodeProcess(info.process,&code) && code==(i==2?17u:IllegalInstruction),252);
        REQUIRE(CloseHandle(info.thread) && CloseHandle(info.process),253);}
    return 0;
}

}
#ifndef GUARDIAN_COMPLETE_ENTRY
#define GUARDIAN_COMPLETE_ENTRY GuardianEntry
#endif
extern "C" uint32_t GUARDIAN_COMPLETE_ENTRY(){
    auto result=GuardianPreviousProof();if(result)return result;
    result=ContinueProof();if(result)return result;result=UnwindProofComplete();if(result)return result;
    result=RaiseProof();if(result)return result;result=FailureProof();if(result)return result;result=LoneYieldProof();if(result)return result;return DynamicProof();
}
