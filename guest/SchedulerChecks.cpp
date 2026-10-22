#include "Scheduler.h"
namespace guardian::threads {
namespace { Scheduler reference; }
uint64_t SchedulerChecks(void (*check)(bool,uint32_t) noexcept) noexcept {
    auto& s=reference;uint32_t n=0;
#define VERIFY(x) check((x),++n)
    const auto a=s.CreateThread(3),b=s.CreateThread(3),c=s.CreateThread(7,true);
    VERIFY(a && b && c && s.Select()==a);s.Yield();VERIFY(s.Select()==b);s.Yield();
    uint32_t prev=0;VERIFY(s.Resume(c,prev)==Success && prev==1 && s.Select()==c);
    VERIFY(s.Suspend(c,prev)==Success && prev==0 && s.Select()==a);s.Yield();
    const auto event=s.CreateObject(Kind::AutoEvent),manual=s.CreateObject(Kind::ManualEvent),sem=s.CreateObject(Kind::Semaphore,0,2);
    uint32_t ids[]{event,sem};
    VERIFY(s.Wait(a,ids,2,true,Never)==Pending && s.Signal(event)==Success);
    VERIFY(s.Get(a)->state==State::Waiting && s.GetObject(event)->signaled);
    VERIFY(s.Signal(sem)==Success && s.Get(a)->result==Success && !s.GetObject(event)->signaled && !s.GetObject(sem)->count);
    VERIFY(s.Wait(a,&manual,1,false,Never)==Pending && s.Wait(b,&manual,1,false,Never)==Pending);
    VERIFY(s.Signal(manual)==Success && s.Get(a)->state==State::Ready && s.Get(b)->state==State::Ready);
    VERIFY(s.Reset(manual)==Success && s.Wait(a,&event,1,false,10)==Pending && s.Wait(b,&event,1,false,10)==Pending);
    VERIFY(s.Signal(event)==Success && s.Get(a)->result==Success && s.Get(b)->state==State::Waiting);
    VERIFY(s.Advance(10) && s.Get(b)->result==Timeout && !s.Advance(9));
    const auto mutex=s.CreateObject(Kind::Mutex),critical=s.CreateObject(Kind::Critical),srw=s.CreateObject(Kind::Srw);
    VERIFY(s.Wait(a,&mutex,1,false,Never)==Success && s.Wait(a,&mutex,1,false,Never)==Success);
    VERIFY(s.Wait(b,&mutex,1,false,Never)==Pending && s.Unlock(b,mutex)==NotOwned);
    VERIFY(s.Unlock(a,mutex)==Success && s.Get(b)->state==State::Waiting);
    VERIFY(s.Terminate(a,77)==Success && s.Get(b)->result==Abandoned && s.GetObject(mutex)->owner==b);
    VERIFY(s.Unlock(b,mutex)==Success && s.Reap(a) && !s.Get(a));
    const auto replacement=s.CreateThread();VERIFY(replacement && replacement!=a && s.CancelWait(a)==Invalid);
    VERIFY(s.Wait(b,&critical,1,false,Never)==Success && s.Wait(b,&critical,1,false,Never)==Success);
    VERIFY(s.Wait(replacement,&critical,1,false,Never)==Pending && s.Close(critical)==Busy);
    VERIFY(s.Unlock(b,critical)==Success && s.Get(replacement)->state==State::Waiting && s.Unlock(b,critical)==Success);
    VERIFY(s.GetObject(critical)->owner==replacement && s.Unlock(replacement,critical)==Success);
    VERIFY(s.Wait(b,&srw,1,false,Never,false,1)==Success && s.Wait(replacement,&srw,1,false,Never,false,1)==Success);
    VERIFY(s.Wait(c,&srw,1,false,Never)==Pending && s.Unlock(b,srw,true)==Success && s.Get(c)->state==State::Waiting);
    VERIFY(s.Unlock(replacement,srw,true)==Success && s.GetObject(srw)->owner==c);
    VERIFY(s.Unlock(b,srw)==NotOwned && s.Unlock(c,srw)==Success);
    VERIFY(s.Wait(b,&srw,1,false,Never,false,1)==Success && s.Wait(c,&srw,1,false,Never)==Pending);
    VERIFY(s.Wait(replacement,&srw,1,false,Never,false,1)==Pending && s.Unlock(b,srw,true)==Success && s.GetObject(srw)->owner==c);
    VERIFY(s.Unlock(c,srw)==Success && (s.GetObject(srw)->readers!=0) && s.Unlock(replacement,srw,true)==Success);
    VERIFY(s.Wait(b,&srw,1,false,Never,false,1)==Success && s.Wait(b,&srw,1,false,Never,false,1)==Invalid && s.Unlock(b,srw,true)==Success);
    const auto timer=s.CreateObject(Kind::AutoTimer);
    VERIFY(s.Arm(timer,30,10)==Success && s.Wait(b,&timer,1,false,30)==Pending);
    VERIFY(s.Advance(30) && s.Get(b)->result==Timeout && s.GetObject(timer)->signaled);
    VERIFY(s.Wait(b,&timer,1,false,Never)==Success && s.GetObject(timer)->due==40);
    VERIFY(s.CancelTimer(timer)==Success && s.Advance(100) && !s.GetObject(timer)->signaled);
    VERIFY(s.Delay(b,110)==Pending && s.Advance(110) && s.Get(b)->result==Success);
    VERIFY(s.Wait(b,&event,1,false,Never)==Pending && s.Alert(b)==Success && s.Get(b)->state==State::Waiting);
    VERIFY(s.CancelWait(b)==Success && s.Get(b)->result==Cancelled && s.Delay(b,200,true)==Alerted);
    VERIFY(s.QueueApc(b,0x600100,123)==Success && s.Delay(b,200,true)==UserApc && s.Get(b)->delivery && s.Get(b)->dispatched.argument==123);
    s.Get(b)->delivery=false;
    VERIFY(s.Delay(b,200,true)==Pending && s.QueueApc(b,0x600100,456)==Success && s.Get(b)->result==UserApc);
    s.Get(b)->delivery=false;
    for(uint32_t i=0;i<MaxApcs;++i) VERIFY(s.QueueApc(b,0x600100,i)==Success);
    VERIFY(s.QueueApc(b,0x600100,5)==NoMemory && s.Signal(sem,3)==Limit && !s.GetObject(sem)->count);
    VERIFY(s.Signal(sem,0)==Invalid && s.Wait(replacement,&sem,1,false,s.Now())==Timeout);
    uint32_t duplicate[]{sem,sem};VERIFY(s.Wait(replacement,duplicate,2,true,Never)==Invalid && s.Wait(replacement,ids,MaxWait+1,true,Never)==Invalid);
    VERIFY(s.Priority(c,8)==Invalid && s.Suspend(c,prev)==Success && s.Resume(c,prev)==Success);
    // Priority aging gives a low-priority runnable thread a bounded opportunity.
    VERIFY(s.Resume(c,prev)==Success && s.Priority(replacement,0)==Success && s.Priority(b,7)==Success);
    bool lowRan=false;
    for(unsigned i=0;i<80;++i) {s.Yield();VERIFY(s.Advance(111+i));if(s.Select()==replacement) lowRan=true;}
    VERIFY(lowRan);
    VERIFY(s.Terminate(b,0)==Success && s.Terminate(c,0)==Success && s.Terminate(replacement,0)==Success && s.AllDead());
    VERIFY(s.Reap(b)&&s.Reap(c)&&s.Reap(replacement));
    const uint32_t owned[]{event,manual,sem,mutex,critical,srw,timer};
    for(auto id:owned) VERIFY(s.Close(id)==Success);
    for(unsigned i=0;i<256;++i) {const auto t=s.CreateThread();VERIFY(t && s.Terminate(t,0)==Success && s.Reap(t));}
    uint32_t threads[MaxThreads]{};for(auto& t:threads) {t=s.CreateThread();VERIFY(t!=0);}VERIFY(!s.CreateThread());
    for(auto t:threads) VERIFY(s.Terminate(t,0)==Success && s.Reap(t));
    uint32_t objects[MaxObjects]{};for(auto& o:objects) {o=s.CreateObject(Kind::AutoEvent);VERIFY(o!=0);}VERIFY(!s.CreateObject(Kind::AutoEvent));
    for(auto o:objects) VERIFY(s.Close(o)==Success);
    // Deterministic competing orderings: signal before expiration, signal at
    // expiration, and cancellation before a later signal must not steal tokens.
    for(uint32_t iteration=0;iteration<128;++iteration) {
        const auto x=s.CreateThread(),y=s.CreateThread(),token=s.CreateObject(Kind::Semaphore,0,1);
        const auto due=s.Now()+10;
        VERIFY(x && y && token && s.Wait(x,&token,1,false,due)==Pending && s.Wait(y,&token,1,false,Never)==Pending);
        if(iteration%3==0) {
            VERIFY(s.Signal(token)==Success && s.Get(x)->result==Success && s.Advance(due) && s.Get(y)->state==State::Waiting);
            VERIFY(s.CancelWait(y)==Success && s.Get(y)->result==Cancelled);
        } else if(iteration%3==1) {
            VERIFY(s.Advance(due) && s.Get(x)->result==Timeout && s.Signal(token)==Success && s.Get(y)->result==Success);
        } else {
            VERIFY(s.CancelWait(x)==Success && s.Signal(token)==Success && s.Get(y)->result==Success && s.Get(x)->result==Cancelled);
        }
        VERIFY(s.Terminate(x,0)==Success && s.Terminate(y,0)==Success && s.Reap(x) && s.Reap(y) && s.Close(token)==Success);
    }
    return n;
#undef VERIFY
}
}
