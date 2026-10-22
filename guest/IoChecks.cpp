#include "IoManager.h"
namespace guardian::io {
namespace {
struct Fake {uint32_t starts=0,stops=0,calls=0,released=0;bool startFails=false,fail=false,oversize=false,busy=false,unknown=false;uint8_t disk[512]{};};
bool Start(void* p,uint32_t) noexcept {auto& f=*static_cast<Fake*>(p);if(f.startFails)return false;++f.starts;return true;}
void Stop(void* p,uint32_t) noexcept {++static_cast<Fake*>(p)->stops;}
void Release(void* p,uint32_t pin) noexcept {static_cast<Fake*>(p)->released+=pin;}
bool BusyDevice(void* p,uint32_t) noexcept {return static_cast<Fake*>(p)->busy;}
uint32_t Transfer(void* p,uint32_t,Op op,uint64_t,const uint8_t* in,uint32_t n,uint8_t* out,uint32_t& bytes) noexcept {
    auto& f=*static_cast<Fake*>(p);++f.calls;if(f.fail)return Removed;if(f.unknown)return 0xC0000001;if(f.oversize){bytes=n+1;return Success;}
    if(op!=Op::Flush)for(uint32_t i=0;i<n;++i){if(op==Op::Write)f.disk[i]=in[i];else out[i]=f.disk[i];}bytes=n;return Success;
}
}
uint64_t IoChecks(Manager& m,threads::Scheduler& s,void (*check)(bool,uint32_t) noexcept) noexcept {
    uint32_t n=0;
#define VERIFY(x) check((x),++n)
    Fake fake{};m.Configure(s,&fake,Release,BusyDevice);Driver d{};d.device=3;d.start=Start;d.stop=Stop;d.dispatch=Transfer;d.context=&fake;
    uint32_t driver=0,id=0,bytes=0;uint8_t data[512]{},out[512]{};
    for(uint32_t field=0;field<8;++field){auto bad=d;switch(field){case 0:++bad.version;break;case 1:--bad.bytes;break;case 2:bad.machine=0x14C;break;case 3:bad.device=4;break;case 4:bad.flags=1;break;case 5:bad.start=nullptr;break;case 6:bad.stop=nullptr;break;default:bad.dispatch=nullptr;break;}VERIFY(m.Load(bad,driver)==Incompatible && !driver && !fake.starts);}
    fake.startFails=true;VERIFY(m.Load(d,driver)==DeviceError && !m.Statistics().drivers);fake.startFails=false;
    VERIFY(m.Load(d,driver)==Success && driver && m.Load(d,id)==Busy);
    VERIFY(m.Submit(1,3,Op::Read,nullptr,511,0,0,id)==Invalid && !id);
    VERIFY(m.Submit(1,3,Op::Read,nullptr,512,UINT64_MAX,0,id)==Invalid);
    VERIFY(m.Submit(1,3,Op::Write,nullptr,512,0,0,id)==Invalid);
    VERIFY(m.Submit(1,4,Op::Read,nullptr,512,0,0,id)==Invalid);
    VERIFY(m.Submit(0,3,Op::Read,nullptr,512,0,0,id)==Invalid);
    data[0]=77;VERIFY(m.Submit(1,3,Op::Write,data,512,0,1,id)==Pending && !fake.calls);data[0]=88;
    VERIFY(m.Collect(1,id,out,512,bytes)==Pending && m.Cancel(2,id)==NotFound && m.Unload(driver)==Busy);
    const auto tid=s.CreateThread();VERIFY(tid && m.Wait(1,id,tid)==threads::Pending);
    m.Pump(1);VERIFY(fake.disk[0]==77 && s.Get(tid)->state==threads::State::Ready && m.Cancel(1,id)==TooLate);
    VERIFY(m.Collect(1,id,nullptr,0,bytes)==Success && bytes==512 && fake.released==1 && m.Collect(1,id,out,512,bytes)==NotFound);
    VERIFY(m.Submit(1,3,Op::Read,nullptr,512,0,0,id)==Pending);m.Pump();
    VERIFY(m.Collect(1,id,out,511,bytes)==Small && m.Get(1,id));VERIFY(m.Collect(1,id,out,512,bytes)==Success && out[0]==77);
    const auto calls=fake.calls;VERIFY(m.Submit(1,3,Op::Write,data,512,0,2,id)==Pending && m.Cancel(1,id)==Success);
    VERIFY(m.Wait(1,id,tid)==Success && m.Collect(1,id,nullptr,0,bytes)==Cancelled && !bytes && fake.released==3);m.Pump();VERIFY(fake.calls==calls);
    uint32_t ids[MaxRequests]{};
    for(uint32_t i=0;i<MaxRequests;++i)VERIFY(m.Submit(i<PerOwner?1u:2u,3,Op::Read,nullptr,512,0,0,ids[i])==Pending);
    VERIFY(m.Submit(1,3,Op::Read,nullptr,512,0,0,id)==Quota && m.Submit(3,3,Op::Read,nullptr,512,0,0,id)==Quota);
    m.Pump(1);VERIFY(m.Get(1,ids[0])->state==State::Complete && m.Get(1,ids[1])->state==State::Queued);
    m.Exit(1);VERIFY(m.Statistics().requests==PerOwner);m.Exit(2);VERIFY(!m.Statistics().requests);
    fake.busy=true;VERIFY(m.Unload(driver)==Busy);fake.busy=false;VERIFY(m.Unload(driver)==Success && m.Unload(driver)==NotFound);
    uint32_t newer=0;VERIFY(m.Load(d,newer)==Success && newer!=driver && m.Unload(driver)==NotFound);driver=newer;
    fake.fail=true;VERIFY(m.Submit(1,3,Op::Read,nullptr,512,0,0,id)==Pending);m.Pump();VERIFY(m.Collect(1,id,out,512,bytes)==Removed && m.Submit(1,3,Op::Read,nullptr,512,0,0,id)==Removed);
    VERIFY(m.Unload(driver)==Success);fake.fail=false;VERIFY(m.Load(d,driver)==Success);
    fake.oversize=true;VERIFY(m.Submit(1,3,Op::Read,nullptr,512,0,0,id)==Pending);m.Pump();VERIFY(m.Collect(1,id,out,512,bytes)==DeviceError && !bytes);fake.oversize=false;
    VERIFY(m.Unload(driver)==Success && m.Load(d,driver)==Success);
    fake.unknown=true;VERIFY(m.Submit(1,3,Op::Read,nullptr,512,0,0,id)==Pending);m.Pump();VERIFY(m.Collect(1,id,out,512,bytes)==DeviceError && !bytes);fake.unknown=false;
    VERIFY(m.Unload(driver)==Success && m.Load(d,driver)==Success);
    // Exhausting scheduler objects must roll back submission and ownership.
    uint32_t raw[threads::MaxObjects]{},count=0;for(;count<threads::MaxObjects;++count){raw[count]=s.CreateObject(threads::Kind::ManualEvent);if(!raw[count])break;}
    VERIFY(m.Submit(1,3,Op::Read,nullptr,512,0,0,id)==NoMemory && !m.Statistics().requests);
    for(uint32_t i=0;i<count;++i)VERIFY(s.Close(raw[i])==threads::Success);
    for(uint32_t i=0;i<128;++i){
        data[0]=static_cast<uint8_t>(i);VERIFY(m.Submit(100+i,3,Op::Write,data,512,0,0,id)==Pending);const auto stale=id;m.Pump();VERIFY(m.Collect(100+i,id,nullptr,0,bytes)==Success && fake.disk[0]==data[0]);
        VERIFY(m.Submit(100+i,3,Op::Read,nullptr,512,0,0,id)==Pending && id!=stale && !m.Get(100+i,stale));m.Exit(100+i);VERIFY(!m.Statistics().requests && !m.Statistics().pins);
    }
    VERIFY(s.Terminate(tid,0)==threads::Success && s.Reap(tid));VERIFY(m.Unload(driver)==Success && fake.starts==fake.stops);
    const auto c=m.Statistics();VERIFY(c.consistent && !c.drivers && !c.requests && !c.pins && c.submitted==c.completed && c.cancelled>=128 && c.failed==3);
#undef VERIFY
    return n;
}
}
