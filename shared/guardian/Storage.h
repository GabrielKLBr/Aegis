#pragma once
#include "Processes.h"
#include "Io.h"
#include "FileTimes.h"
namespace guardian::storage {
inline constexpr uint64_t Magic=0x374653415647ull,ReportGpa=0x10F000,RequiredChecks=0xFFF;
inline constexpr uint32_t Version=1,MaxNodes=32,MaxHandles=32,PerOwnerHandles=8,MaxWatches=8,DataBytes=512,PathBytes=128,NameBytes=32,Depth=8;
inline constexpr uint32_t SlotBytes=30720,FirstSlot=4096,SecondSlot=34816;
inline constexpr uint32_t FormatIntentOffset=3072;
inline constexpr uint16_t Port=0xF1;
inline constexpr uint32_t Success=0,Pending=0x103,Invalid=0xC000000D,Denied=0xC0000022,NotFound=0xC0000034,Collision=0xC0000035,
    Sharing=0xC0000043,NoSpace=0xC000007F,Quota=0xC0000044,Corrupt=0xC0000102,IoError=0xC0000185,TypeMismatch=0xC0000024,
    Small=0xC0000023,NotEmpty=0xC0000101,Unsupported=0xC00000BB,InvalidHandle=0xC0000008;
inline constexpr uint32_t Read=processes::ReadMemory,Write=processes::WriteMemory,Delete=processes::Terminate,List=processes::Query,CreateChild=processes::CreateChild,
    ReadAcl=processes::ReadControl,WriteAcl=processes::WriteDac,All=Read|Write|Delete|List|CreateChild|ReadAcl|WriteAcl;
enum class Kind:uint32_t {Empty,Directory,File,Key,Value,Link};
enum class ValueType:uint32_t {None=0,String=1,ExpandString=2,Binary=3,Dword=4,MultiString=7,Qword=11};
enum class Op:uint32_t {Mkdir=1,Open,Read,Write,Close,Rename,Unlink,Link,SetAcl,Flush,RegCreate,RegSet,RegGet,RegEnum,RegDeleteValue,RegDeleteKey,RegWatch,RegPoll,CloseWatch,QueryAcl,WaitWatch,Enumerate,Truncate,FileSize,QuerySpace,FileTimesRead,FileTimesWrite};
struct Node {
    uint32_t id=0,parent=0,target=0;Kind kind=Kind::Empty;uint32_t flags=0;ValueType type=ValueType::None;
    uint32_t size=0,revision=0,generation=0,reserved=0;processes::Security security{};char name[NameBytes]{};uint8_t data[DataBytes]{};
};
struct Header {uint64_t magic=0,sequence=0,checksum=0;uint32_t version=0,bytes=0,nodeBytes=0,nodeCount=0;uint64_t reserved[59]{};};
struct Image {Header header{};Node nodes[MaxNodes]{};};
struct Command {
    uint32_t version=Version,bytes=sizeof(Command);Op op=Op::Open;uint32_t flags=0;
    uint64_t handle=0,offset=0;uint32_t access=0,share=7,disposition=0,type=0,index=0,length=0,status=0,transferred=0;
    uint64_t watch=0,reserved=0;char path[PathBytes]{},other[PathBytes]{},name[NameBytes]{};uint8_t data[DataBytes]{};
};
struct Report {uint64_t magic;uint32_t version,bytes;uint64_t bootId,checks,coreChecks,sequenceBefore,sequenceAfter,recovered,
    userCalls,reads,writes,registryChanges,notifications,denials,liveHandles,liveWatches,reserved[4];};
static_assert(sizeof(Node)==736 && sizeof(Header)==512 && sizeof(Image)==24064 && sizeof(Command)==880 && sizeof(Report)==160);
inline uint64_t Hash(const void* pointer,uint32_t bytes,uint32_t skip=UINT32_MAX,uint32_t skipBytes=0) noexcept {
    const auto* p=static_cast<const uint8_t*>(pointer);uint64_t h=14695981039346656037ull;
    for(uint32_t i=0;i<bytes;++i){const auto value=i>=skip && i-skip<skipBytes?uint8_t(0):p[i];h=(h^value)*1099511628211ull;}return h;
}
inline bool ValidReport(const Report& r,uint64_t id) noexcept {if(r.magic!=Magic || r.version!=Version || r.bytes!=sizeof(r) || r.bootId!=id || (r.checks&~RequiredChecks))return false;for(auto x:r.reserved)if(x)return false;return true;}
}
