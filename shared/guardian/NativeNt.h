#pragma once
#include <stdint.h>
#include "NtCompatibility.h"
namespace guardian::native {
inline constexpr uint32_t Success=0,NotImplemented=0xC0000002,InvalidClass=0xC0000003,LengthMismatch=0xC0000004,
    AccessViolation=0xC0000005,InvalidHandle=0xC0000008,Invalid=0xC000000D,NoMemory=0xC0000017,Denied=0xC0000022,
    Small=0xC0000023,Unsupported=0xC00000BB,Timeout=0x102,UserApc=0xC0;
inline constexpr uint64_t CurrentProcess=UINT64_MAX,CurrentThread=UINT64_MAX-1,SharedData=0x7FFE0000;
enum class Op:uint32_t {Invalid,Allocate,Free,Protect,QueryMemory,CreateSection,MapSection,UnmapSection,
    Close,Duplicate,QueryObject,QueryProcess,QueryThread,QuerySystem,OpenProcess,OpenThread,CreateThread,ResumeThread,TerminateThread,TerminateProcess,
    CreateEvent,SetEvent,ResetEvent,CreateMutant,ReleaseMutant,CreateSemaphore,ReleaseSemaphore,CreateTimer,SetTimer,CancelTimer,
    WaitSingle,WaitMultiple,Delay,QueueApc,TestAlert,QueryTime,QueryCounter,FlushInstructionCache,
    CreateFile,OpenFile,ReadFile,WriteFile,QueryFile,CreateKey,OpenKey,SetValue,QueryValue,DeleteValue,DeleteKey,Count};
struct Packet {uint32_t version=1,bytes=sizeof(Packet);Op op=Op::Invalid;uint32_t reserved=0;uint64_t args[11]{};};
struct Unicode {uint16_t length=0,maximum=0;uint32_t padding=0;uint64_t buffer=0;};
struct Ansi {uint16_t length=0,maximum=0;uint32_t padding=0;uint64_t buffer=0;};
struct Attributes {uint32_t length=sizeof(Attributes),padding=0;uint64_t root=0,name=0;uint32_t attributes=0,reserved=0;uint64_t security=0,qos=0;};
struct IoStatus {uint64_t status=0,information=0;};
struct ClientId {uint64_t process=0,thread=0;};
struct ProcessBasic {uint32_t exitStatus=259,reserved=0;uint64_t peb=0,affinity=1;int32_t priority=8;uint32_t padding=0;uint64_t pid=0,parent=0;};
struct ThreadBasic {uint32_t exitStatus=259,reserved=0;uint64_t teb=0;ClientId client{};uint64_t affinity=1;int32_t priority=8,basePriority=8;};
struct VersionInfo {uint32_t size=sizeof(VersionInfo),major=0,minor=0,build=0,platform=2;uint16_t servicePack[128]{};uint16_t spMajor=0,spMinor=0,suite=0;uint8_t product=1,reserved=0;};
inline constexpr uint64_t MapMagic=0x31504D53544E47ull;
inline constexpr uint32_t MaxMap=64;
struct Mapping {uint32_t number=0;Op operation=Op::Invalid;};
struct SyscallMap {uint64_t magic=MapMagic;uint32_t version=1,bytes=sizeof(SyscallMap),profile=0,count=0,original=0,reserved=0;uint64_t dllHash=0;Mapping entries[MaxMap]{};};
inline bool ValidMap(const SyscallMap& m,uint32_t profile) noexcept {
    if(m.magic!=MapMagic||m.version!=1||m.bytes!=sizeof(m)||m.profile!=profile||m.count>MaxMap||m.original>1||m.reserved||!m.dllHash||
        (m.original?m.count<30:m.count!=0))return false;
    for(uint32_t i=0;i<m.count;++i){if(m.entries[i].number>=0x2000||m.entries[i].operation<=Op::Invalid||m.entries[i].operation>=Op::Count)return false;
        for(uint32_t j=0;j<i;++j)if(m.entries[i].number==m.entries[j].number)return false;}
    for(uint32_t i=m.count;i<MaxMap;++i)if(m.entries[i].number||m.entries[i].operation!=Op::Invalid)return false;return true;
}
static_assert(sizeof(Packet)==104&&sizeof(Unicode)==16&&sizeof(Attributes)==48&&sizeof(IoStatus)==16&&sizeof(ProcessBasic)==48&&sizeof(ThreadBasic)==48&&sizeof(VersionInfo)==284&&sizeof(SyscallMap)==552);
}
