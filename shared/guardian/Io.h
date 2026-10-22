#pragma once
#include "Abi.h"
namespace guardian::io {
inline constexpr uint64_t Magic=0x364F49415647ull,PacketGpa=0x10C000,ReportGpa=0x10D000,BusGpa=0x10E000,RequiredChecks=0xFFF;
inline constexpr uint32_t Version=1,MaxRequests=8,PerOwner=4,MaxTransfer=512,DeviceCount=3,BlockBytes=65536,ConsoleBytes=4096;
inline constexpr uint16_t Port=0xF0;
inline constexpr uint32_t Success=0,Pending=0x103,Invalid=0xC000000D,Denied=0xC0000022,NoMemory=0xC0000017,
    Busy=0xC000009E,NotFound=0xC0000034,Cancelled=0xC0000120,TooLate=0xC000010B,DeviceError=0xC0000185,
    Removed=0xC000000E,Small=0xC0000023,Quota=0xC0000044,Incompatible=0xC00000BB,NoSpace=0xC000007F;
enum class Kind:uint32_t {Console=1,Channel,Block};
enum class Op:uint32_t {Read=1,Write,Flush,QuerySpace=16};
inline bool KnownOperation(Op op) noexcept {return op==Op::Read||op==Op::Write||op==Op::Flush||op==Op::QuerySpace;}
struct Device {uint32_t id;Kind kind;uint32_t version,maxTransfer;uint64_t capacity,features,reserved;};
struct Bus {uint64_t magic,bootId;uint32_t version,bytes,count,queueDepth;Device devices[DeviceCount];};
struct Packet {
    uint64_t magic;uint32_t version,bytes;uint64_t bootId,sequence,request;
    uint32_t device;Op op;uint64_t offset;uint32_t length,status,transferred,reserved;
    uint8_t data[MaxTransfer];
};
struct Report {
    uint64_t magic;uint32_t version,bytes;uint64_t bootId,checks,coreChecks,submitted,completed,cancelled,failed,
        loads,unloads,liveDrivers,liveRequests,livePins,consoleBytes,channelBytes,blockBytes,ticks,reserved[4];
};
static_assert(sizeof(Packet)==584 && sizeof(Bus)==152 && sizeof(Report)==176);
inline bool ValidBus(const Bus& b,uint64_t id) noexcept {
    if(b.magic!=Magic || b.bootId!=id || b.version!=Version || b.bytes!=sizeof(b) || b.count!=DeviceCount || b.queueDepth!=MaxRequests)return false;
    for(uint32_t i=0;i<DeviceCount;++i){const auto& d=b.devices[i];if(d.id!=i+1 || static_cast<uint32_t>(d.kind)!=i+1 || d.version!=Version || d.maxTransfer!=MaxTransfer || d.reserved ||
        (d.features!=(i==0?2u:3u) && !(i==2 && d.features==7)) || d.capacity!=(i==0?ConsoleBytes:(i==1?MaxRequests*MaxTransfer:BlockBytes)))return false;}return true;
}
inline bool Shape(uint32_t device,Op op,uint64_t offset,uint32_t n) noexcept {
    if(!device || device>DeviceCount || !KnownOperation(op))return false;
    if(op==Op::QuerySpace)return device==3 && !offset && n==512;
    if(op==Op::Flush)return device==3 && !offset && !n;
    if(!n || n>MaxTransfer)return false;
    if(device==1)return op==Op::Write && !offset;
    if(device==2)return !offset;
    return n==512 && !(offset&511) && offset<BlockBytes && n<=BlockBytes-offset;
}
inline bool ValidRequest(const Packet& p,uint64_t id,uint64_t next) noexcept {
    return next && p.magic==Magic && p.version==Version && p.bytes==sizeof(p) && p.bootId==id && p.sequence==next && p.request &&
        !p.status && !p.transferred && !p.reserved && p.length<=MaxTransfer && p.device && p.device<=DeviceCount && KnownOperation(p.op);
}
inline bool ValidResponse(const Packet& a,const Packet& b) noexcept {
    if(b.magic!=a.magic || b.version!=a.version || b.bytes!=a.bytes || b.bootId!=a.bootId || b.sequence!=a.sequence || b.request!=a.request ||
        b.device!=a.device || b.op!=a.op || b.offset!=a.offset || b.length!=a.length || b.reserved || b.transferred>a.length)return false;
    if(b.status!=Success && b.status!=Invalid && b.status!=DeviceError && b.status!=Removed && b.status!=Small && b.status!=Busy && b.status!=NoSpace && b.status!=Quota)return false;
    if(b.status!=Success)return !b.transferred;
    return (b.op==Op::Read||b.op==Op::QuerySpace)?b.transferred>0:b.transferred==a.length;
}
inline bool ValidReport(const Report& r,uint64_t id) noexcept {
    if(r.magic!=Magic || r.version!=Version || r.bytes!=sizeof(r) || r.bootId!=id || (r.checks&~RequiredChecks))return false;
    for(auto x:r.reserved)if(x)return false;return true;
}
}
