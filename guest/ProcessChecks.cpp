#include "ProcessManager.h"
namespace guardian::processes {
uint64_t ProcessChecks(Manager& m,memory::Manager& memory,threads::Scheduler& scheduler,void (*check)(bool,uint32_t) noexcept) noexcept {
    uint32_t n=0;
#define VERIFY(x) check((x),++n)
    const auto baseline=memory.physical.Statistics().free;
    Token aToken{};aToken.user=100;aToken.primaryGroup=10;aToken.session=1;aToken.integrity=2;aToken.privileges=CreateGlobal;aToken.groupCount=1;aToken.groups[0]={20,1};
    auto bToken=aToken;bToken.user=200;bToken.session=2;
    auto low=aToken;low.integrity=1;low.privileges=0;low.groups[0].flags=2;
    VERIFY(Manager::ValidToken(aToken) && Manager::Restricted(aToken,low));
    auto invalidParent=aToken;invalidParent.groupCount=MaxGroups+1;VERIFY(!Manager::Restricted(invalidParent,low));
    invalidParent=aToken;invalidParent.privileges=0x80000000;VERIFY(!Manager::Restricted(invalidParent,low));
    auto bad=low;bad.integrity=2;VERIFY(!Manager::Restricted(low,bad));bad=low;bad.user=200;VERIFY(!Manager::Restricted(aToken,bad));
    bad=low;bad.groupCount=0;VERIFY(!Manager::Restricted(low,bad));bad=low;bad.groups[0].flags=1;VERIFY(!Manager::Restricted(low,bad));
    auto acl=Manager::DefaultSecurity(aToken,Kind::Event);VERIFY(!Manager::Access(low,acl,Kind::Event,Modify) && Manager::Access(low,acl,Kind::Event,Wait));
    acl.aceCount=0;VERIFY(!Manager::Access(aToken,acl,Kind::Event,Query));acl.daclPresent=0;VERIFY(Manager::Access(bToken,acl,Kind::Event,Query));
    acl=Manager::DefaultSecurity(aToken,Kind::Event);acl.aceCount=2;acl.aces[1]=acl.aces[0];acl.aces[0]={Principal::Group,20,Modify,1};
    VERIFY(!Manager::Access(low,acl,Kind::Event,Modify));
    uint32_t a=0,b=0,c=0;
    VERIFY(m.CreateRoot(aToken,a)==Success && m.CreateRoot(bToken,b)==Success && a!=b);
    Token queried{};VERIFY(m.QueryToken(a,m.Get(a)->tokenHandle,queried)==Success && queried.user==100);
    VERIFY(m.QueryToken(a,m.Get(a)->self,queried)==TypeMismatch);
    uint64_t event=0,read=0,other=0,processB=0;
    auto shared=Manager::DefaultSecurity(aToken,Kind::Event);shared.aceCount=2;shared.aces[1]={Principal::Everyone,0,Wait|Query,0};
    VERIFY(m.CreateObject(a,Kind::Event,"\\Global\\Ready",&shared,GenericAll,0,event)==Success);
    VERIFY(m.CreateObject(a,Kind::Mutex,"\\Global\\Ready",nullptr,GenericAll,0,other)==TypeMismatch);
    VERIFY(m.Open(b,Kind::Event,"\\Global\\Ready",Wait|Query,other)==Success && m.Signal(b,other)==Denied);
    uint64_t untouched=0xBAD;VERIFY(m.Open(b,Kind::Event,"\\Global\\Ready",Modify,untouched)==Denied && untouched==0xBAD);
    VERIFY(m.OpenProcess(b,a,ReadMemory,untouched)==Denied && m.Close(b,event)==InvalidHandle);
    VERIFY(m.DuplicateHandle(a,event,m.Get(a)->self,Wait|Query,Inherit,read)==Success && read!=event);
    VERIFY(m.Signal(a,read)==Denied && m.DuplicateHandle(a,read,m.Get(a)->self,Modify,0,untouched)==Denied);
    VERIFY(m.CreateChildProcess(a,low,&event,1,c)==Denied && !c);
    uint64_t secondRead=0;VERIFY(m.DuplicateHandle(a,event,m.Get(a)->self,Wait|Query,Inherit,secondRead)==Success);
    const uint64_t inheritTwo[]{read,secondRead};const auto beforeInheritance=m.Counts();const auto beforeFrames=memory.physical.Statistics().free;
    m.Budgets(-1,3);VERIFY(m.CreateChildProcess(a,low,inheritTwo,2,c)==NoMemory && !c);m.Budgets();
    VERIFY(m.Counts().processes==beforeInheritance.processes && m.Counts().references==beforeInheritance.references && memory.physical.Statistics().free==beforeFrames);
    VERIFY(m.CreateChildProcess(a,low,&read,1,c)==Success && m.Get(c)->parent==a);
    Information info{};VERIFY(m.QueryHandle(c,read,info)==Success && info.access==(Wait|Query) && m.Signal(c,read)==Denied);
    uint32_t extra=0;VERIFY(m.QueryHandle(c,event,info)==InvalidHandle && m.CreateRoot(aToken,extra)==Quota);
    VERIFY(m.Kill(c,0)==Success && m.Reap(c));c=0;
    auto bsd=Manager::DefaultSecurity(bToken,Kind::Process);bsd.aceCount=2;bsd.aces[1]={Principal::User,100,Duplicate,0};
    VERIFY(m.SetDacl(b,m.Get(b)->self,bsd)==Success && m.OpenProcess(a,b,Duplicate,processB)==Success);
    uint64_t transferred=0;VERIFY(m.DuplicateHandle(a,event,processB,Wait|Query,0,transferred)==Success && m.QueryHandle(b,transferred,info)==Success);
    VERIFY(m.DuplicateHandle(a,event,processB,Modify,0,untouched)==Denied);
    VERIFY(m.Flags(a,read,ProtectClose)==Success && m.Close(a,read)==NotClosable && m.Flags(a,read,0)==Success);
    // A DACL change restricts subsequent opens, without changing granted handles.
    auto closed=shared;closed.aceCount=1;
    VERIFY(m.SetDacl(a,event,closed)==Success && m.Open(b,Kind::Event,"\\Global\\Ready",Wait,untouched)==Denied);
    VERIFY(m.QueryHandle(b,other,info)==Success && m.SetDacl(b,other,shared)==Denied);
    VERIFY(m.SetDacl(a,event,shared)==Success);
    Security inspected{};VERIFY(m.QuerySecurity(a,event,inspected)==Success && inspected.owner==100 && m.QuerySecurity(b,other,inspected)==Denied);
    uint32_t pinned=0;VERIFY(m.Reference(a,event,Query,pinned)==Success && m.Close(a,event)==Success);
    VERIFY(m.Dereference(pinned));event=0;
    uint64_t privateA=0,privateB=0;
    VERIFY(m.CreateObject(a,Kind::Event,"\\Session\\Same",nullptr,GenericAll,0,privateA)==Success && m.CreateObject(b,Kind::Event,"\\Session\\Same",nullptr,GenericAll,0,privateB)==Success);
    VERIFY(m.QueryHandle(a,privateA,info)==Success && info.owner==100 && m.QueryHandle(b,privateB,info)==Success && info.owner==200);
    VERIFY(m.CreateObject(a,Kind::Event,"\\Global\\bad\\path",nullptr,GenericAll,0,untouched)==Invalid);
    uint64_t va=memory::Arena,vb=va;
    VERIFY(m.Reserve(a,va,8192,memory::ReadWrite)==Success && m.Commit(a,va,4096,memory::ReadWrite)==Success);
    VERIFY(m.Reserve(b,vb,4096,memory::ReadWrite)==Success && m.Commit(b,vb,4096,memory::ReadWrite)==Success && m.Translate(a,va)!=m.Translate(b,vb));
    *static_cast<uint64_t*>(memory.physical.At(m.Translate(a,va,true)))=123;
    *static_cast<uint64_t*>(memory.physical.At(m.Translate(b,vb,true)))=456;
    auto broad=Manager::DefaultSecurity(bToken,Kind::Process);broad.aceCount=2;broad.aces[1]={Principal::User,100,ReadMemory|WriteMemory|Duplicate|Terminate,0};
    VERIFY(m.SetDacl(b,m.Get(b)->self,broad)==Success);
    uint64_t remote=0;VERIFY(m.OpenProcess(a,b,ReadMemory|WriteMemory,remote)==Success && m.CopyProcess(a,remote,vb,va,8,false)==Success);
    VERIFY(*static_cast<uint64_t*>(memory.physical.At(m.Translate(a,va)))==456);
    *static_cast<uint64_t*>(memory.physical.At(m.Translate(a,va,true)))=789;
    VERIFY(m.CopyProcess(a,remote,vb,va,8,true)==Success && *static_cast<uint64_t*>(memory.physical.At(m.Translate(b,vb)))==789);
    VERIFY(m.CopyProcess(a,remote,vb,va+4096,8,false)==Denied && m.CopyProcess(a,remote,UINT64_MAX,va,8,false)==Invalid);
    VERIFY(m.Commit(a,va,8192,memory::ReadWrite)==Success && m.Release(a,va)==Success && m.Release(b,vb)==Success);
    uint64_t huge=0;VERIFY(m.Reserve(a,huge,uint64_t(MaxPrivatePages+1)*4096,memory::ReadWrite)==Success && m.Commit(a,huge,uint64_t(MaxPrivatePages+1)*4096,memory::ReadWrite)==Quota && m.Get(a)->committed==0 && m.Release(a,huge)==Success);
    // Close the last handle while a wait is pending. The wait reference keeps
    // the named object alive; a different process can signal it after owner exit.
    uint64_t waiting=0;VERIFY(m.CreateObject(a,Kind::Event,"\\Global\\Pinned",&shared,GenericAll,0,waiting)==Success);
    const auto tid=scheduler.CreateThread();uint64_t threadHandle=0;VERIFY(tid && m.Attach(a,tid,threadHandle)==Success);
    VERIFY(m.WaitHandles(a,tid,&waiting,1,false,threads::Never)==threads::Pending && m.Close(a,waiting)==Success);
    uint64_t signal=0;VERIFY(m.Open(a,Kind::Event,"\\Global\\Pinned",Modify,signal)==Success && m.Signal(a,signal)==Success && m.Close(a,signal)==Success);
    VERIFY(scheduler.Get(tid)->state==threads::State::Ready);
    // Closing a mutex handle cannot destroy ownership before thread teardown.
    uint64_t mutex=0;VERIFY(m.CreateObject(a,Kind::Mutex,nullptr,nullptr,GenericAll,0,mutex)==Success);
    VERIFY(m.WaitHandles(a,tid,&mutex,1,false,threads::Never)==Success && m.Close(a,mutex)==Success);
    auto sharedSignal=shared;sharedSignal.aces[1].rights|=Modify;
    uint64_t ownerEvent=0,waitB=0;VERIFY(m.CreateObject(a,Kind::Event,"\\Global\\OwnerExit",&sharedSignal,GenericAll,0,ownerEvent)==Success && m.Open(b,Kind::Event,"\\Global\\OwnerExit",Wait,waitB)==Success);
    const auto tidB=scheduler.CreateThread();uint64_t handleB=0;VERIFY(tidB && m.Attach(b,tidB,handleB)==Success && m.WaitHandles(b,tidB,&waitB,1,false,threads::Never)==threads::Pending && m.Close(b,waitB)==Success);
    VERIFY(m.Kill(a,17)==Success && m.Reap(a) && m.QueryHandle(b,other,info)==Success);
    uint64_t afterExit=0;VERIFY(m.Open(b,Kind::Event,"\\Global\\OwnerExit",Modify,afterExit)==Success && m.Signal(b,afterExit)==Success && scheduler.Get(tidB)->state==threads::State::Ready && m.Close(b,afterExit)==Success);
    VERIFY(m.Kill(b,0)==Success && m.Reap(b));
    VERIFY(m.Counts().processes==0 && m.Counts().objects==0 && m.Counts().handles==0 && m.Counts().references==0 && m.Counts().consistent && memory.physical.Statistics().free==baseline);
    uint32_t limitPid=0;VERIFY(m.CreateRoot(aToken,limitPid)==Success);
    for(uint32_t i=0;i<MaxProcessThreads;++i){const auto t=scheduler.CreateThread();uint64_t h=0;VERIFY(t && m.Attach(limitPid,t,h)==Success && m.Close(limitPid,h)==Success);}
    const auto excessive=scheduler.CreateThread();uint64_t excessiveHandle=0;
    VERIFY(excessive && m.Attach(limitPid,excessive,excessiveHandle)==Quota && !excessiveHandle && scheduler.Terminate(excessive,0)==threads::Success && scheduler.Reap(excessive));
    VERIFY(m.Kill(limitPid,0)==Success && m.Reap(limitPid) && m.CreateRoot(aToken,limitPid)==Success);
    uint64_t handles[MaxHandles-2]{};
    for(auto& h:handles)VERIFY(m.CreateObject(limitPid,Kind::Event,nullptr,nullptr,GenericAll,0,h)==Success);
    untouched=0xBAD;VERIFY(m.CreateObject(limitPid,Kind::Event,"\\Session\\Full",nullptr,GenericAll,0,untouched)==Quota && untouched==0xBAD && m.Counts().handles==MaxHandles);
    for(auto h:handles)VERIFY(m.Close(limitPid,h)==Success);
    uint32_t pins[MaxObjects-2]{};
    for(auto& pin:pins){uint64_t h=0;VERIFY(m.CreateObject(limitPid,Kind::Event,nullptr,nullptr,GenericAll,0,h)==Success && m.Reference(limitPid,h,Query,pin)==Success && m.Close(limitPid,h)==Success);}
    VERIFY(m.Counts().objects==MaxObjects && m.CreateObject(limitPid,Kind::Event,nullptr,nullptr,GenericAll,0,untouched)==NoMemory);
    for(auto pin:pins)VERIFY(m.Dereference(pin));
    VERIFY(m.Kill(limitPid,0)==Success && m.Reap(limitPid) && memory.physical.Statistics().free==baseline);
    // Fail every physical allocation in process creation, then its object and
    // handle initialization positions. No partial process may survive.
    for(int64_t budget=0;budget<21;++budget){uint32_t pid=0;memory.physical.Budget(budget);VERIFY(m.CreateRoot(aToken,pid)==NoMemory && !pid);memory.physical.Budget(-1);VERIFY(!m.Counts().processes && !m.Counts().objects && memory.physical.Statistics().free==baseline);}
    for(int64_t budget=0;budget<2;++budget){uint32_t pid=0;m.Budgets(budget,-1);VERIFY(m.CreateRoot(aToken,pid)==NoMemory && !pid);m.Budgets();VERIFY(!m.Counts().objects && memory.physical.Statistics().free==baseline);m.Budgets(-1,budget);VERIFY(m.CreateRoot(aToken,pid)==NoMemory && !pid);m.Budgets();VERIFY(!m.Counts().objects && memory.physical.Statistics().free==baseline);}
    for(unsigned i=0;i<128;++i){uint32_t pid=0;VERIFY(m.CreateRoot(aToken,pid)==Success);uint64_t h=0;VERIFY(m.CreateObject(pid,Kind::Event,"\\Session\\Reused",nullptr,GenericAll,ProtectClose,h)==Success);VERIFY(m.Kill(pid,0)==Success && m.Reap(pid));}
    VERIFY(m.Counts().consistent && !m.Counts().processes && !m.Counts().references && memory.physical.Statistics().free==baseline && scheduler.AllDead());
    return n;
#undef VERIFY
}
}
