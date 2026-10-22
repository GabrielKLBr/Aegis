#include "../guest/ProcessManager.h"
#include <cstdlib>
#include <iostream>
#include <memory>
void Check(bool b,uint32_t n) noexcept {if(!b){std::cerr<<"Process core failure "<<n<<'\n';std::exit(1);}}
int main(){
    auto memory=std::make_unique<guardian::memory::Manager>();auto processes=std::make_unique<guardian::processes::Manager>();auto scheduler=std::make_unique<guardian::threads::Scheduler>();
    constexpr uint64_t bytes=16<<20;auto ram=std::make_unique<uint8_t[]>(bytes);
    const guardian::abi::MemoryRegion map[]{{0,0x100000,guardian::abi::MemoryKind::Reserved,0,0},{0x100000,bytes-0x100000,guardian::abi::MemoryKind::Available,0,0}};
    Check(memory->physical.Initialize(reinterpret_cast<uint64_t>(ram.get()),bytes,map,2),0);
    guardian::memory::Space idle{};Check(memory->Create(idle),0);memory->Activate(&idle);processes->Configure(*memory,*scheduler);
    const auto count=guardian::processes::ProcessChecks(*processes,*memory,*scheduler,Check);Check(count>400,0);
    using namespace guardian;using namespace guardian::processes;
    uint32_t checks=0;const auto verify=[&](bool b){Check(b,0x1000+(++checks));};
    const auto free=memory->physical.Statistics().free;Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;uint32_t pid=0;
    verify(processes->CreateRoot(token,pid)==0);const auto main=scheduler->CreateThread();uint64_t mainHandle=0;verify(processes->Attach(pid,main,mainHandle)==0);
    uint64_t retained=0;uint32_t first=0;
    for(uint32_t i=0;i<64;++i){const auto tid=scheduler->CreateThread();uint64_t h=0;
        verify(tid && tid!=first && processes->Attach(pid,tid,h)==0);verify(!processes->ReapThread(tid));
        uint64_t query=0;verify(processes->DuplicateHandle(pid,h,processes->Get(pid)->self,Query,0,query)==0);
        verify(processes->WaitHandles(pid,main,&query,1,false,0)==Denied);Object* restricted=nullptr;
        verify(processes->Resolve(pid,query,Kind::Thread,Modify,restricted)==Denied && processes->Close(pid,query)==0);
        verify(processes->WaitHandles(pid,main,&h,1,false,threads::Never)==threads::Pending);
        verify(processes->ThreadExit(tid,100+i)==0 && scheduler->Get(main)->result==0);
        verify(processes->ReapThread(tid) && !processes->Owner(tid) && !scheduler->Get(tid) && processes->Get(pid)->threadCount==1);
        verify(!processes->ReapThread(tid) && processes->ThreadExit(tid,0)==Invalid);
        Object* object=nullptr;verify(processes->Resolve(pid,h,Kind::Thread,Query,object)==0 && object->exited && object->exitCode==100+i);
        verify(processes->WaitHandles(pid,main,&h,1,false,0)==0);
        if(!i){retained=h;first=tid;}else verify(processes->Close(pid,h)==0);
        verify(processes->Resolve(pid,retained,Kind::Thread,Query,object)==0 && object->exitCode==100 && object->target==first);
        verify(processes->Counts().consistent);
    }
    verify(processes->Close(pid,retained)==0);verify(!processes->ReapThread(main));
    const auto unhandled=scheduler->CreateThread();uint64_t closed=0;verify(processes->Attach(pid,unhandled,closed)==0);
    verify(processes->Close(pid,closed)==0 && processes->Owner(unhandled)==pid);
    verify(processes->ThreadExit(unhandled,42)==0 && processes->ReapThread(unhandled));
    verify(processes->Kill(pid,0)==0 && processes->Reap(pid));
    const auto final=processes->Counts();verify(final.consistent && !final.processes && !final.objects && !final.handles && !final.references && !final.threads && memory->physical.Statistics().free==free);
    std::cout<<checks<<" individual thread lifecycle checks PASS\n";
    std::cout<<count<<" freestanding process/security checks PASS\n";
    uint32_t apiChecks=0;const auto api=[&](bool b){Check(b,0x2000+(++apiChecks));};
    uint32_t owner=0,foreign=0;token.user=100;api(processes->CreateRoot(token,owner)==0);token.user=200;api(processes->CreateRoot(token,foreign)==0);
    const auto target=scheduler->CreateThread();uint64_t original=0,query=0,denied=0;Object* object=nullptr;
    api(processes->Attach(owner,target,original)==0);api(processes->OpenThread(owner,target,Query,query)==0);
    api(processes->Resolve(owner,query,Kind::Thread,Query,object)==0 && !object->exited);
    api(processes->Resolve(owner,query,Kind::Thread,Modify,object)==Denied);
    api(processes->WaitHandles(owner,target,&query,1,false,0)==Denied);
    api(processes->OpenThread(foreign,target,Query,denied)==Denied);api(processes->OpenThread(owner,UINT32_MAX,Query,denied)==InvalidHandle);
    api(processes->OpenThread(owner,target,0x80000,denied)==Invalid);
    const auto handles=processes->Counts().handles;processes->Budgets(-1,0);api(processes->OpenThread(owner,target,Query,denied)==NoMemory && processes->Counts().handles==handles);processes->Budgets();
    api(processes->ThreadExit(target,7)==0 && processes->ReapThread(target));api(processes->OpenThread(owner,target,Query,denied)==InvalidHandle);
    api(processes->Resolve(owner,query,Kind::Thread,Query,object)==0 && object->exited && object->exitCode==7);
    api(processes->Close(owner,query)==0 && processes->Close(owner,original)==0);
    api(processes->Kill(owner,0)==0 && processes->Reap(owner) && processes->Kill(foreign,0)==0 && processes->Reap(foreign));
    const auto clean=processes->Counts();api(clean.consistent && !clean.processes && !clean.objects && !clean.handles && !clean.references && !clean.threads && memory->physical.Statistics().free==free);
    std::cout<<apiChecks<<" native thread opening rights checks PASS\n";return 0;
}
