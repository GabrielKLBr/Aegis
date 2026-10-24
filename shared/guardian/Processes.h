#pragma once
#include "Abi.h"
#include "PeProfile.h"
namespace guardian::processes {
inline constexpr uint64_t Magic=0x35524350415647ull,ReportGpa=0x10B000,RequiredChecks=0x7FFF;
inline constexpr uint32_t Version=1,MaxProcesses=3,MaxHandles=32,MaxObjects=64,MaxAces=8,MaxGroups=4,MaxProcessThreads=4,MaxPrivatePages=peprofile::PrivatePages;
inline constexpr uint16_t Port=0xEF;
inline constexpr uint32_t Success=0,InvalidHandle=0xC0000008,Invalid=0xC000000D,NoMemory=0xC0000017,Denied=0xC0000022,
    TypeMismatch=0xC0000024,Collision=0xC0000035,NotFound=0xC0000034,Terminated=0xC000010A,Quota=0xC0000044,NotClosable=0xC0000235;
enum Rights:uint32_t { Wait=1,Query=2,Modify=4,Duplicate=8,ReadMemory=16,WriteMemory=32,Terminate=64,CreateChild=128,ReadControl=256,WriteDac=512 };
inline constexpr uint32_t GenericRead=0x80000000,GenericWrite=0x40000000,GenericExecute=0x20000000,GenericAll=0x10000000;
inline constexpr uint32_t Inherit=1,ProtectClose=2,CreateGlobal=1;
struct Group {uint32_t id,flags;}; // Enabled=1, deny-only=2.
struct Token {uint32_t user=0,primaryGroup=0,session=0,integrity=1,privileges=0,groupCount=0;Group groups[MaxGroups]{};};
enum class Principal:uint32_t {Everyone,User,Group};
struct Ace {Principal principal;uint32_t id,rights,deny;};
struct Security {uint32_t owner=0,group=0,integrity=1,daclPresent=1,noWriteUp=1,aceCount=0;Ace aces[MaxAces]{};};
enum class Kind:uint32_t {Empty,Event,Mutex,Semaphore,Timer,Critical,Srw,Process,Thread,Token,Device};
struct Information {uint32_t kind,access,attributes,handleReferences,pointerReferences,owner,integrity,reserved;};
struct Report {
    uint64_t magic;uint32_t version,bytes;
    uint64_t bootId,checks,coreChecks,ticks,switches,completed,faulted,denied,inheritance,remoteCopies,
        freeBefore,freeAfter,liveProcesses,liveObjects,liveHandles,liveReferences,reserved[4];
};
static_assert(sizeof(Token)==56 && sizeof(Security)==152 && sizeof(Report)==176 && sizeof(Information)==32);
inline bool ValidReport(const Report& r,uint64_t id) noexcept {
    if(r.magic!=Magic || r.version!=Version || r.bytes!=sizeof(r) || r.bootId!=id || (r.checks&~RequiredChecks))return false;
    for(auto n:r.reserved)if(n)return false;return true;
}
}
