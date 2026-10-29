#include "PeServices.h"
#ifdef GUARDIAN_FRAMEWORKS
#include "guardian/ConsoleUtf8.h"
#endif
#include "ProcessManager.h"
namespace guardian::runtime {
namespace {
bool Text(char* out,const char* in,uint32_t n) noexcept {for(uint32_t i=0;i<n;++i){const auto c=static_cast<uint8_t>(in[i]);if(c>126 || (c && c<32))return false;out[i]=in[i];if(!c)return true;}return false;}
void Bytes(void* out,const void* in,uint32_t n) noexcept {auto* d=static_cast<uint8_t*>(out);const auto* s=static_cast<const uint8_t*>(in);for(uint32_t i=0;i<n;++i)d[i]=s[i];}
}
Services::Handle* Services::Find(uint32_t p,uint64_t id,uint32_t kind) noexcept {for(auto& h:handles_)if(h.id==id && h.id && h.owner==p && h.kind==kind)return &h;return nullptr;}
Services::Handle* Services::Free(uint32_t pid) noexcept {uint32_t count=0;Handle* free=nullptr;for(auto& h:handles_){if(h.id && h.owner==pid)++count;if(!h.id)free=&h;}return count<8 && next_<=UINT64_MAX-4?free:nullptr;}
uint32_t Services::Call(uint32_t p,const processes::Token& t,storage::Command& c) noexcept {return storage_?storage_(context_,p,t,c):storage::Unsupported;}
uint32_t Services::FileOpen(uint32_t pid,const processes::Token& token,Command& c) noexcept {
    using namespace storage;
    if(c.flags || !c.access || (c.access&~0xC0010000u) || (c.share&~7u) || !c.disposition || c.disposition>5)return Invalid;
    const uint32_t rights=((c.access&0x80000000)?storage::Read:0u)|((c.access&0x40000000)?storage::Write:0u)|((c.access&0x10000)?storage::Delete:0u);
    if((c.disposition==2 || c.disposition==5) && !(rights&storage::Write))return Denied;
    auto* slot=Free(pid);if(!slot)return Quota;storage::Command io{};io.op=storage::Op::Open;io.access=rights;io.share=c.share;
    if(!Text(io.path,c.path,128))return Invalid;
    io.disposition=c.disposition==1?1u:0u;auto status=Call(pid,token,io);bool existed=!status;
    if(status==NotFound && (c.disposition==2 || c.disposition==4)){io.status=0;io.disposition=1;status=Call(pid,token,io);existed=false;}
    if(status)return status;const auto raw=io.handle;
    if(c.disposition==2 || c.disposition==5){io={};io.op=storage::Op::Truncate;io.handle=raw;status=Call(pid,token,io);
        if(status){io={};io.op=storage::Op::Close;io.handle=raw;Call(pid,token,io);return status;}}
    *slot={};slot->id=next_;next_+=4;slot->owner=pid;slot->kind=1;slot->raw=raw;slot->access=rights;c.handle=slot->id;c.flags=existed?1u:0u;return Success;
}
uint32_t Services::KeyPath(uint32_t pid,const Command& c,char* out) noexcept {
    const char* root=nullptr;auto* key=Find(pid,c.handle,2);
    if(c.handle==CurrentUser)root="HKCU";else if(c.handle==LocalMachine)root="HKLM";else if(key){if(c.flags && c.path[0] && !(key->access&4))return storage::Denied;root=key->path;}else return storage::InvalidHandle;
    uint32_t at=0;for(;root[at] && at<127;++at)out[at]=root[at];if(root[at])return storage::Invalid;
    if(c.path[0]){if(at==127)return storage::Invalid;out[at++]='\\';for(uint32_t i=0;i<128;++i){const auto ch=static_cast<uint8_t>(c.path[i]);if(!ch){out[at]=0;return storage::Success;}if(ch<32 || ch>126 || at==127)return storage::Invalid;out[at++]=static_cast<char>(ch);}return storage::Invalid;}
    out[at]=0;return storage::Success;
}
uint32_t Services::KeyAccess(uint32_t pid,const processes::Token& token,const char* path,uint32_t access) noexcept {
    storage::Command q{};q.op=storage::Op::QueryAcl;q.length=sizeof(processes::Security);Text(q.path,path,128);const auto status=Call(pid,token,q);if(status)return status;
    processes::Security sd{};Bytes(&sd,q.data,sizeof(sd));
    const auto rights=((access&1)?storage::Read:0u)|((access&2)?storage::Write:0u)|((access&4)?storage::CreateChild:0u)|((access&24)?storage::List:0u)|((access&0x20000)?storage::ReadAcl:0u);
    return processes::Manager::Access(token,sd,processes::Kind::Process,rights)?storage::Success:storage::Denied;
}
uint32_t Services::KeyOpen(uint32_t pid,const processes::Token& token,Command& c) noexcept {
    if(c.flags>1 || !c.access || (c.access&~0x2001Fu))return storage::Invalid;
    auto* slot=Free(pid);if(!slot)return storage::Quota;char path[128]{};auto status=KeyPath(pid,c,path);if(status)return status;
    if(hardware_&&hardware::RegistryKey(*hardware_,path)){
        if((c.access&~0x20019u)||!(c.access&1))return storage::Denied;
        *slot={};slot->id=next_;next_+=4;slot->owner=pid;slot->kind=2;slot->access=c.access;Text(slot->path,path,128);c.handle=slot->id;c.disposition=2;return storage::Success;
    }
    status=KeyAccess(pid,token,path,c.access);const bool existed=!status;
    if(status==storage::NotFound && c.flags){storage::Command io{};io.op=storage::Op::RegCreate;Text(io.path,path,128);status=Call(pid,token,io);if(!status)status=KeyAccess(pid,token,path,c.access);}
    if(status)return status;*slot={};slot->id=next_;next_+=4;slot->owner=pid;slot->kind=2;slot->access=c.access;Text(slot->path,path,128);c.handle=slot->id;c.disposition=existed?2u:1u;return storage::Success;
}
uint32_t Services::Execute(uint32_t pid,const processes::Token& token,Command& c) noexcept {
    using namespace storage;
    if(!pid || !processes::Manager::ValidToken(token) || c.version!=runtime::Version || c.bytes!=sizeof(c) || c.reserved || c.status || c.transferred)return c.status=Invalid;
    if(c.op==runtime::Op::FileRename||c.op==runtime::Op::FileDelete){
        if(c.flags||c.handle||c.argument||c.access||c.share||c.disposition||c.length||c.type)return c.status=Invalid;
        storage::Command io{};io.op=c.op==runtime::Op::FileRename?storage::Op::Rename:storage::Op::Unlink;
        if(!Text(io.path,c.path,128)||(c.op==runtime::Op::FileRename&&!Text(io.other,reinterpret_cast<const char*>(c.data),128)))return c.status=Invalid;
        return c.status=Call(pid,token,io);
    }
    if(c.op==runtime::Op::Open)return c.status=FileOpen(pid,token,c);
    if(c.op==runtime::Op::RegOpen)return c.status=KeyOpen(pid,token,c);
    if(c.op==runtime::Op::FirmwareRead){
        if(!hardware_)return c.status=Unsupported;
        if(c.handle!=hardware::Rsmb||c.type||c.flags>1||c.length>512)return c.status=Unsupported;
        const auto firmware=hardware::Smbios(*hardware_);if(!firmware.good)return c.status=Invalid;c.access=firmware.size;
        if(c.flags)return c.status=Success;
        if(c.argument>firmware.size)return c.status=Invalid;
        const uint32_t remaining=firmware.size-static_cast<uint32_t>(c.argument);c.transferred=c.length<remaining?c.length:remaining;
        Bytes(c.data,firmware.bytes+c.argument,c.transferred);return c.status=Success;
    }
    if(c.op==runtime::Op::DiskSpace){
        if(c.flags||c.handle||c.argument||c.length)return c.status=Invalid;
        storage::Command io{};io.op=storage::Op::QuerySpace;if(!Text(io.path,c.path,128))return c.status=Invalid;
        const auto status=Call(pid,token,io);if(!status){Bytes(c.data,io.data,sizeof(storage::Space));c.transferred=sizeof(storage::Space);}return c.status=status;
    }
    if(c.op==runtime::Op::ConsoleWrite){if(c.handle!=Console || c.flags || c.length>512)return c.status=Invalid;
#ifdef GUARDIAN_FRAMEWORKS
        if(!guardian::ConsoleUtf8(c.data,c.length))return c.status=Invalid;
#elif defined(GUARDIAN_CRT_NATIVE)
        for(uint32_t i=0;i<c.length;++i)if((c.data[i]<32&&c.data[i]!=10&&c.data[i]!=9&&c.data[i]!=13)||c.data[i]>126)return c.status=Invalid;
#else
        for(uint32_t i=0;i<c.length;++i)if((c.data[i]<32 && c.data[i]!=10 && c.data[i]!=9) || c.data[i]>126)return c.status=Invalid;
#endif
        return c.status=c.length?(console_?console_(context_,pid,c.data,c.length,c.transferred):Unsupported):Success;}
    const bool registry=c.op==runtime::Op::RegSet || c.op==runtime::Op::RegGet || c.op==runtime::Op::RegClose;
    auto* h=Find(pid,c.handle,registry?2u:1u);if(!h)return c.status=InvalidHandle;
    if(registry&&hardware_&&hardware::RegistryKey(*hardware_,h->path)){
        if(c.op==runtime::Op::RegClose){*h={};return c.status=Success;}
        if(c.op==runtime::Op::RegSet)return c.status=Denied;
        char name[32]{};if(c.flags>1||c.length>512||!Text(name,c.name,32))return c.status=Invalid;
        const auto* value=hardware::RegistryQuery(*hardware_,h->path,name);if(!value)return c.status=NotFound;
        const uint32_t bytes=hardware::Length(value)+1,capacity=c.length;c.type=1;c.length=bytes;
        if(c.flags)return c.status=Success;if(capacity<bytes)return c.status=Small;
        Bytes(c.data,value,bytes);c.transferred=bytes;return c.status=Success;
    }
    storage::Command io{};io.handle=h->raw;io.offset=h->position;uint32_t status=Success;
    switch(c.op){
    case runtime::Op::Read:case runtime::Op::Write:
        if(c.flags || c.length>512)return c.status=Invalid;
        io.op=c.op==runtime::Op::Read?storage::Op::Read:storage::Op::Write;io.length=c.length;
        if(c.op==runtime::Op::Write)Bytes(io.data,c.data,c.length);status=Call(pid,token,io);
        if(!status){h->position+=io.transferred;c.transferred=io.transferred;if(c.op==runtime::Op::Read)Bytes(c.data,io.data,io.transferred);}break;
    case runtime::Op::Seek:case runtime::Op::Size:{
        if(c.flags || (c.op==runtime::Op::Seek && c.share>2))return c.status=Invalid;
        io.op=storage::Op::FileSize;status=Call(pid,token,io);if(status)break;
        if(c.op==runtime::Op::Size){c.argument=io.transferred;break;}
        const auto base=c.share==0?0ull:(c.share==1?h->position:io.transferred);uint64_t at=0;
        if(c.argument>>63){const uint64_t distance=0-c.argument;if(distance>base)return c.status=NegativeSeek;at=base-distance;}
        else {if(c.argument>512 || base>512-c.argument)return c.status=NoSpace;at=base+c.argument;}
        h->position=at;c.argument=at;break;}
    case runtime::Op::FileTimesRead:case runtime::Op::FileTimesWrite:
        if(c.argument||c.access||c.share||c.disposition||c.type||c.flags>7||
            (c.op==runtime::Op::FileTimesRead?(c.flags||c.length):c.length!=sizeof(storage::FileTimes)))return c.status=Invalid;
        io.op=c.op==runtime::Op::FileTimesRead?storage::Op::FileTimesRead:storage::Op::FileTimesWrite;
        io.offset=0;io.flags=c.flags;io.length=sizeof(storage::FileTimes);
        if(c.op==runtime::Op::FileTimesWrite)Bytes(io.data,c.data,sizeof(storage::FileTimes));
        status=Call(pid,token,io);if(!status && c.op==runtime::Op::FileTimesRead){Bytes(c.data,io.data,sizeof(storage::FileTimes));c.transferred=sizeof(storage::FileTimes);}break;
    case runtime::Op::Flush:if(!(h->access&storage::Write))status=Denied;else {io.op=storage::Op::Flush;status=Call(pid,token,io);}break;
    case runtime::Op::Close:io.op=storage::Op::Close;status=Call(pid,token,io);if(!status)*h={};break;
    case runtime::Op::RegClose:*h={};break;
    case runtime::Op::RegSet:case runtime::Op::RegGet:
        if(c.flags>1 || c.length>512 || !Text(io.name,c.name,32))return c.status=Invalid;
        if(c.op==runtime::Op::RegSet && c.type!=0 && c.type!=3 && c.type!=4 && c.type!=11)return c.status=Unsupported;
        if(!(h->access&(c.op==runtime::Op::RegSet?2u:1u)))return c.status=Denied;
        Text(io.path,h->path,128);io.op=c.op==runtime::Op::RegSet?storage::Op::RegSet:storage::Op::RegGet;io.length=c.op==runtime::Op::RegSet?c.length:512u;io.type=c.type;
        if(c.op==runtime::Op::RegSet)Bytes(io.data,c.data,c.length);status=Call(pid,token,io);
        if(!status && c.op==runtime::Op::RegGet){if(io.type!=0 && io.type!=3 && io.type!=4 && io.type!=11)return c.status=Unsupported;c.type=io.type;const auto capacity=c.length;c.length=io.transferred;
            if(!c.flags && capacity<io.transferred)status=Small;else if(!c.flags){Bytes(c.data,io.data,io.transferred);c.transferred=io.transferred;}}break;
    default:status=Unsupported;break;
    }return c.status=status;
}
bool Services::Exit(uint32_t pid,const processes::Token& token) noexcept {
    bool ok=true;for(auto& h:handles_)if(h.id && h.owner==pid){if(h.kind==1){storage::Command io{};io.op=storage::Op::Close;io.handle=h.raw;if(Call(pid,token,io)){ok=false;continue;}}h={};}return ok;
}
uint32_t Services::Count(uint32_t pid) const noexcept {uint32_t n=0;for(const auto& h:handles_)if(h.id && (!pid || h.owner==pid))++n;return n;}
}
