#include "IoManager.h"
#include "guardian/Telemetry.h"
namespace guardian::io {
uint32_t Manager::LoadRaw(const Driver& d,uint32_t& id) noexcept {
    if(d.version!=Version || d.bytes!=sizeof(Driver) || d.machine!=abi::X64 || d.flags || !d.device || d.device>DeviceCount || !d.start || !d.stop || !d.dispatch)return Incompatible;
    auto& slot=drivers_[d.device-1];if(slot.live)return Busy;if(slot.generation>=0xFFFFFE)return Quota;
    if(!d.start(d.context,d.device))return DeviceError;
    ++slot.generation;slot.definition=d;slot.id=(slot.generation<<8)|d.device;slot.live=true;slot.failed=false;id=slot.id;++counts_.loads;return Success;
}
uint32_t Manager::UnloadRaw(uint32_t id) noexcept {
    const auto device=id&255u;if(!device || device>DeviceCount)return NotFound;auto& d=drivers_[device-1];if(!d.live || d.id!=id)return NotFound;
    for(const auto& r:requests_)if(r.state!=State::Empty && r.device==device)return Busy;
    if(busy_ && busy_(context_,device))return Busy;d.definition.stop(d.definition.context,device);d.live=false;++counts_.unloads;return Success;
}
Request* Manager::Find(uint32_t owner,uint32_t id) noexcept {const auto slot=id&255u;if(!slot || slot>MaxRequests)return nullptr;auto& r=requests_[slot-1];return r.id==id && r.owner==owner && r.state!=State::Empty?&r:nullptr;}
const Request* Manager::Get(uint32_t owner,uint32_t id) const noexcept {return const_cast<Manager*>(this)->Find(owner,id);}
uint32_t Manager::Submit(uint32_t owner,uint32_t device,Op op,const uint8_t* in,uint32_t bytes,uint64_t offset,uint32_t pin,uint32_t& id) noexcept {
    if(!owner || !Shape(device,op,offset,bytes) || (op==Op::Write && !in))return Invalid;
    auto& d=drivers_[device-1];if(!d.live || d.failed)return Removed;
    uint32_t used=0;for(const auto& r:requests_)if(r.state!=State::Empty && r.owner==owner)++used;if(used>=PerOwner)return Quota;
    for(uint32_t i=0;i<MaxRequests;++i)if(requests_[i].state==State::Empty && requests_[i].generation<0xFFFFFE){
        const auto completion=scheduler_->CreateObject(threads::Kind::ManualEvent);if(!completion)return NoMemory;
        auto& r=requests_[i];const auto gen=r.generation+1;r={};r.generation=gen;r.id=(gen<<8)|(i+1);r.owner=owner;r.device=device;r.pin=pin;r.completion=completion;r.op=op;r.offset=offset;r.bytes=bytes;r.order=++order_;r.state=State::Queued;
        if(op==Op::Write)for(uint32_t n=0;n<bytes;++n)r.data[n]=in[n];id=r.id;++counts_.submitted;return Pending;
    }
    return Quota;
}
void Manager::Complete(Request& r,uint32_t status,uint32_t bytes) noexcept {
    if(r.state!=State::Queued && r.state!=State::Active){counts_.consistent=false;return;}r.state=State::Complete;r.status=status;r.transferred=bytes;++counts_.completed;
    if(status==Cancelled)++counts_.cancelled;else if(status!=Success)++counts_.failed;
    if(scheduler_->Signal(r.completion)!=threads::Success)counts_.consistent=false;
}
uint32_t Manager::Cancel(uint32_t owner,uint32_t id) noexcept {auto* r=Find(owner,id);if(!r)return NotFound;if(r->state!=State::Queued)return TooLate;Complete(*r,Cancelled,0);return Success;}
void Manager::Drop(Request& r) noexcept {
    if(scheduler_->Close(r.completion)!=threads::Success){counts_.consistent=false;return;}
    if(r.pin && release_)release_(context_,r.pin);const auto gen=r.generation;r={};r.generation=gen;
}
uint32_t Manager::Collect(uint32_t owner,uint32_t id,uint8_t* out,uint32_t capacity,uint32_t& bytes) noexcept {
    auto* r=Find(owner,id);if(!r)return NotFound;if(r->state!=State::Complete)return Pending;
    if((r->op==Op::Read || r->op==Op::QuerySpace) && r->status==Success){if(!out || capacity<r->transferred)return Small;for(uint32_t n=0;n<r->transferred;++n)out[n]=r->data[n];}
    const auto status=r->status;bytes=r->transferred;Drop(*r);return status;
}
uint32_t Manager::Wait(uint32_t owner,uint32_t id,uint32_t tid) noexcept {const auto* r=Find(owner,id);return r?scheduler_->Wait(tid,&r->completion,1,false,threads::Never):NotFound;}
void Manager::Exit(uint32_t owner) noexcept {for(auto& r:requests_)if(r.owner==owner && r.state!=State::Empty){if(r.state==State::Queued)Complete(r,Cancelled,0);if(r.state==State::Complete)Drop(r);}}
void Manager::Pump(uint32_t maximum) noexcept {
    for(uint32_t n=0;n<maximum;++n){Request* next=nullptr;for(auto& r:requests_)if(r.state==State::Queued && (!next || r.order<next->order))next=&r;if(!next)break;
        auto& r=*next;auto& d=drivers_[r.device-1];r.state=State::Active;uint32_t bytes=0;uint32_t status=Removed;
        if(d.live && !d.failed)status=d.definition.dispatch(d.definition.context,r.device,r.op,r.offset,r.data,r.bytes,r.data,bytes);
        if((status!=Success && status!=Invalid && status!=DeviceError && status!=Removed && status!=Small && status!=Busy && status!=NoSpace && status!=Quota) || (status!=Success && bytes) || bytes>r.bytes || (status==Success && (((r.op==Op::Read || r.op==Op::QuerySpace) && !bytes) || (r.op!=Op::Read && bytes!=r.bytes)))){status=DeviceError;bytes=0;}
        if(status==DeviceError || status==Removed)d.failed=true;Complete(r,status,bytes);
    }
}
uint32_t Manager::Load(const Driver& d,uint32_t& id) noexcept {
    auto e=telemetry::Begin(telemetry::Domain::Driver,telemetry::Operation::Load,0,0,"guest I/O driver","",d.device);
    if(!telemetry::Before(e))return telemetry::End(e,DeviceError);const auto status=LoadRaw(d,id);if(!status)e.related=id;return telemetry::End(e,status);
}
uint32_t Manager::Unload(uint32_t id) noexcept {
    auto e=telemetry::Begin(telemetry::Domain::Driver,telemetry::Operation::Unload,0,0,"guest I/O driver","",id);
    telemetry::Before(e);return telemetry::End(e,UnloadRaw(id));
}
Counts Manager::Statistics() const noexcept {auto c=counts_;for(const auto& d:drivers_)if(d.live)++c.drivers;for(const auto& r:requests_)if(r.state!=State::Empty){++c.requests;if(r.pin)++c.pins;}return c;}
}
