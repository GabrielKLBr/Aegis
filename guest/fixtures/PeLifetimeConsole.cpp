// Keep the complete earlier runtime/31-worker proof in this new workload too.
#define GuardianEntry GuardianRuntimeProof
#include "PeRuntimeConsole.cpp"
#undef GuardianEntry
namespace {
uint32_t LifetimeProof(){
    StartupInfoA startup{};startup.cb=sizeof(startup);ProcessInformation info{};uint32_t code=0;
    char command[]="return";
    REQUIRE(!CreateProcessA("missing.exe",command,nullptr,nullptr,0,0,nullptr,nullptr,&startup,&info) && GetLastError()==2 && !info.process && !info.thread,100);
    REQUIRE(!CreateProcessA("guardian-lifetime-child.exe",command,nullptr,nullptr,1,0,nullptr,nullptr,&startup,&info) && GetLastError()==87,101);
    startup.flags=1;REQUIRE(!CreateProcessA("guardian-lifetime-child.exe",command,nullptr,nullptr,0,0,nullptr,nullptr,&startup,&info) && GetLastError()==87,102);startup.flags=0;
    const char* modes[]={"return","thread","worker","primary","process"};const uint32_t exits[]={11,33,45,66,22};
    const uint32_t initialThread[]={11,33,45,55,22};Handle retained=nullptr,retainedThread=nullptr;
    auto* canary=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),8,64));REQUIRE(canary,103);for(uint32_t i=0;i<64;++i)canary[i]=0xA5;
    const auto* mainTls=reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint64_t*>(__readgsqword(0x58))[1]);uint8_t original[256]{};for(uint32_t i=0;i<256;++i)original[i]=mainTls[i];
    for(uint32_t round=0;round<4;++round)for(uint32_t mode=0;mode<5;++mode){
        char text[32]{};for(uint32_t i=0;modes[mode][i];++i)text[i]=modes[mode][i];
        REQUIRE(CreateProcessA("GUARDIAN-LIFETIME-CHILD.EXE",text,nullptr,nullptr,0,4,nullptr,nullptr,&startup,&info),104);
        REQUIRE(info.process && info.thread && info.processId!=GetCurrentProcessId() && info.threadId!=GetCurrentThreadId(),105);
        REQUIRE(GetExitCodeProcess(info.process,&code) && code==259 && GetExitCodeThread(info.thread,&code) && code==259 && WaitForSingleObject(info.process,0)==0x102,106);
        REQUIRE(!ReleaseMutex(info.process) && GetLastError()==6 && !GetExitCodeThread(info.process,&code) && GetLastError()==6,107);
        REQUIRE(ResumeThread(info.thread)==1 && WaitForSingleObject(info.thread,UINT32_MAX)==0 && GetExitCodeThread(info.thread,&code) && code==initialThread[mode],108);
        REQUIRE(WaitForSingleObject(info.process,UINT32_MAX)==0 && GetExitCodeProcess(info.process,&code) && code==exits[mode] && WaitForSingleObject(info.process,0)==0,109);
        for(uint32_t i=0;i<64;++i)REQUIRE(canary[i]==0xA5,110);for(uint32_t i=0;i<256;++i)REQUIRE(mainTls[i]==original[i],111);
        REQUIRE(tlsValue==0x2469 && tlsAttached==31 && tlsDetached==31,112);
        if(!round && !mode){retained=info.process;retainedThread=info.thread;}
        else {REQUIRE(CloseHandle(info.thread) && CloseHandle(info.process),113);REQUIRE(!GetExitCodeProcess(info.process,&code) && GetLastError()==6,114);}
        REQUIRE(GetExitCodeProcess(retained,&code) && code==11 && GetExitCodeThread(retainedThread,&code) && code==11,115);
    }
    ProcessInformation pair[2]{};for(uint32_t i=0;i<2;++i)REQUIRE(CreateProcessA("guardian-lifetime-child.exe",command,nullptr,nullptr,0,4,nullptr,nullptr,&startup,&pair[i]),116);
    REQUIRE(pair[0].processId!=pair[1].processId && !CreateProcessA("guardian-lifetime-child.exe",command,nullptr,nullptr,0,4,nullptr,nullptr,&startup,&info) && GetLastError()==8,117);
    for(uint32_t i=0;i<2;++i)REQUIRE(ResumeThread(pair[i].thread)==1,118);
    for(uint32_t i=0;i<2;++i)REQUIRE(WaitForSingleObject(pair[i].process,UINT32_MAX)==0 && GetExitCodeProcess(pair[i].process,&code) && code==11 && CloseHandle(pair[i].thread) && CloseHandle(pair[i].process),119);
    REQUIRE(CreateProcessA("guardian-lifetime-child.exe",command,nullptr,nullptr,0,0,nullptr,nullptr,&startup,&info) && WaitForSingleObject(info.process,UINT32_MAX)==0 && GetExitCodeProcess(info.process,&code) && code==11 && CloseHandle(info.thread) && CloseHandle(info.process),121);
    guardian::runtime::Command malformed{};malformed.op=guardian::runtime::Op::ProcessCreate;malformed.argument=1;
    REQUIRE(GuardianRuntimeCall(&malformed)==0xC000000D,122);
    malformed={};malformed.op=guardian::runtime::Op::ProcessCreate;const char child[]="guardian-lifetime-child.exe";for(uint32_t i=0;i<sizeof(child);++i)malformed.path[i]=child[i];
    for(uint32_t i=0;i<128;++i)malformed.data[i]='x';REQUIRE(GuardianRuntimeCall(&malformed)==0xC000000D,123);
    REQUIRE(HeapFree(GetProcessHeap(),0,canary) && CloseHandle(retainedThread) && CloseHandle(retained),120);return 0;
}
}
namespace {
__declspec(noinline) uint32_t TraceLeaf(){
    void* frames[16]{};Dword hash=0;SetLastError(0xFACE);const auto n=GuardianCaptureProbe(0,frames,&hash);
    REQUIRE(n>=3 && n<=16 && GetLastError()==0xFACE,130);uint32_t sum=0;bool app=false,dll=false;
    const auto image=*reinterpret_cast<const uint64_t*>(__readgsqword(0x60)+0x10);
    for(uint32_t i=0;i<n;++i){const auto at=reinterpret_cast<uint64_t>(frames[i]);sum+=static_cast<uint32_t>(at);
        REQUIRE(at>=image && at<image+0x40000,131);app=app || at<image+0x20000;dll=dll || at>=image+0x20000;}
    REQUIRE(app && dll && sum==hash,132);
    void* skipped[16]{};const auto fewer=GuardianCaptureProbe(1,skipped,nullptr);REQUIRE(fewer+1==n && GetLastError()==0xFACE,133);
    for(uint32_t i=1;i<fewer;++i)REQUIRE(skipped[i]==frames[i+1],144);
    REQUIRE(reinterpret_cast<uint64_t>(frames[0])>=image+0x20000 && reinterpret_cast<uint64_t>(skipped[0])<image+0x20000,145);
    // Writable TEB fields are never the authority for the kernel's bounds.
    const auto top=__readgsqword(8),bottom=__readgsqword(16);__writegsqword(8,UINT64_MAX);__writegsqword(16,0);
    const auto bounded=RtlCaptureStackBackTrace(0,1,skipped,nullptr);__writegsqword(8,top);__writegsqword(16,bottom);REQUIRE(bounded==1 && GetLastError()==0xFACE,146);
    hash=99;REQUIRE(!RtlCaptureStackBackTrace(16,1,skipped,&hash) && !hash && GetLastError()==0xFACE,147);
    REQUIRE(RtlCaptureStackBackTrace(0,1,frames,&hash)==1 && hash==static_cast<uint32_t>(reinterpret_cast<uint64_t>(frames[0])),134);
    frames[0]=reinterpret_cast<void*>(0xDEAD);hash=99;REQUIRE(!RtlCaptureStackBackTrace(0,17,frames,&hash) && !hash && GetLastError()==87 && frames[0]==reinterpret_cast<void*>(0xDEAD),135);
    REQUIRE(!RtlCaptureStackBackTrace(17,1,frames,nullptr) && GetLastError()==87 && !RtlCaptureStackBackTrace(0,0,frames,nullptr),136);
    REQUIRE(!RtlCaptureStackBackTrace(0,1,nullptr,nullptr) && GetLastError()==87,137);
    guardian::runtime::Command bad{};bad.op=guardian::runtime::Op::StackCapture;bad.length=1;bad.handle=1;REQUIRE(GuardianRuntimeCall(&bad)==0xC000000D,138);
    bad={};bad.op=guardian::runtime::Op::StackCapture;bad.length=17;REQUIRE(GuardianRuntimeCall(&bad)==0xC000000D,139);return 0;
}
__declspec(noinline) uint32_t TraceMiddle(){volatile uint32_t guard=7;const auto code=TraceLeaf();guard=guard+1;return guard==8?code:140;}
uint32_t TraceWorker(void*){return TraceMiddle();}
uint32_t UnwindProof(){
    const auto main=TraceMiddle();if(main)return main;Handle workers[3]{};
    for(uint32_t i=0;i<3;++i){workers[i]=CreateThread(nullptr,0,TraceWorker,nullptr,4,nullptr);REQUIRE(workers[i],141);}
    for(auto worker:workers)REQUIRE(ResumeThread(worker)==1,142);
    for(auto worker:workers){Dword code=0;REQUIRE(WaitForSingleObject(worker,UINT32_MAX)==0 && GetExitCodeThread(worker,&code) && !code && CloseHandle(worker),143);}return 0;
}
}
#ifndef GUARDIAN_LIFETIME_ENTRY
#define GUARDIAN_LIFETIME_ENTRY GuardianEntry
#endif
extern "C" uint32_t GUARDIAN_LIFETIME_ENTRY(){const auto runtime=GuardianRuntimeProof();if(runtime)return runtime;const auto life=LifetimeProof();return life?life:UnwindProof();}
