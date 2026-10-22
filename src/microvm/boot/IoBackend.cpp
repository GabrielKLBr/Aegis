#include "IoBackend.h"
namespace guardian::io {
Backend::Backend(uint64_t boot) noexcept {
    bus_.magic=Magic;bus_.bootId=boot;bus_.version=Version;bus_.bytes=sizeof(bus_);bus_.count=DeviceCount;bus_.queueDepth=MaxRequests;
    for(uint32_t i=0;i<DeviceCount;++i)bus_.devices[i]={i+1,static_cast<Kind>(i+1),Version,MaxTransfer,i==0?ConsoleBytes:(i==1?MaxRequests*MaxTransfer:BlockBytes),i==0?2u:3u,0};
}
bool Backend::Exchange(Packet& p) noexcept {
    if(!ValidRequest(p,bus_.bootId,sequence_+1))return false;++sequence_;
    uint32_t status=Success,n=0;
    if(!Shape(p.device,p.op,p.offset,p.length))status=Invalid;
    else if(failed_[p.device-1])status=Removed;
    else if(p.device==1){
        if(p.length>ConsoleBytes-consoleUsed_)status=Busy;
        for(uint32_t i=0;!status && i<p.length;++i)if((p.data[i]<32 && p.data[i]!='\n' && p.data[i]!='\t') || p.data[i]>126)status=Invalid;
        if(!status){for(uint32_t i=0;i<p.length;++i)console_[consoleUsed_+i]=p.data[i];consoleUsed_+=p.length;n=p.length;}
    }else if(p.device==2){
        if(p.op==Op::Write){if(count_==MaxRequests)status=Busy;else{const auto slot=(head_+count_)%MaxRequests;for(uint32_t i=0;i<p.length;++i)channel_[slot][i]=p.data[i];lengths_[slot]=p.length;++count_;n=p.length;}}
        else if(!count_)status=Busy;else if(p.length<lengths_[head_])status=Small;
        else{n=lengths_[head_];for(uint32_t i=0;i<n;++i)p.data[i]=channel_[head_][i];lengths_[head_]=0;head_=(head_+1)%MaxRequests;--count_;}channelBytes_+=n;
    }else if(p.op==Op::QuerySpace){
        guardian::storage::BackingSpace space{};
        if(!storage_||!query_||!query_(storage_,space))status=DeviceError;
        else {const auto* data=reinterpret_cast<const uint8_t*>(&space);for(uint32_t i=0;i<MaxTransfer;++i)p.data[i]=i<sizeof(space)?data[i]:0;n=512;}
    }else if(storage_){
        const auto ok=p.op==Op::Read?read_(storage_,p.offset,p.data):(p.op==Op::Write?write_(storage_,p.offset,p.data):flush_(storage_));
        if(!ok){status=status_?status_(storage_):DeviceError;if(status!=NoSpace && status!=Quota)status=DeviceError;}else{n=p.length;blockBytes_+=n;}
    }else if(p.op!=Op::Flush){
        n=p.length;for(uint32_t i=0;i<n;++i){if(p.op==Op::Write)disk_[p.offset+i]=p.data[i];else p.data[i]=disk_[p.offset+i];}blockBytes_+=n;
    }
    p.status=status;p.transferred=n;return true;
}
}

namespace guardian::io {
void Backend::Save(av::microvm::state::Writer& w) const {
    w.Pod(sequence_);w.Pod(consoleUsed_);w.Pod(channelBytes_);w.Pod(blockBytes_);
    for(bool f:failed_)w.Pod(uint32_t(f));w.Raw(disk_,sizeof(disk_));w.Raw(console_,sizeof(console_));w.Raw(channel_,sizeof(channel_));
    w.Raw(lengths_,sizeof(lengths_));w.Pod(head_);w.Pod(count_);
}
void Backend::Load(av::microvm::state::Reader& r){
    sequence_=r.Pod<uint64_t>();consoleUsed_=r.Pod<uint64_t>();channelBytes_=r.Pod<uint64_t>();blockBytes_=r.Pod<uint64_t>();
    for(auto& f:failed_){auto value=r.Pod<uint32_t>();av::microvm::state::Require(value<=1,"Invalid device fault flag");f=value!=0;}
    r.Raw(disk_,sizeof(disk_));r.Raw(console_,sizeof(console_));r.Raw(channel_,sizeof(channel_));r.Raw(lengths_,sizeof(lengths_));head_=r.Pod<uint32_t>();count_=r.Pod<uint32_t>();
    av::microvm::state::Require(consoleUsed_<=ConsoleBytes && head_<MaxRequests && count_<=MaxRequests,"Invalid saved device queue");
    for(auto n:lengths_)av::microvm::state::Require(n<=MaxTransfer,"Invalid saved channel length");
}
}
