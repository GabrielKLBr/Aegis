#pragma once
#include <stdint.h>
namespace guardian::telemetry {
inline constexpr uint64_t Magic=0x31524341545647ull,InfoGpa=0x13F000,PacketGpa=0x13E000;
inline constexpr uint32_t Version=1,MaxEvents=8192,DefaultEvents=4096,AnalysisChecks=7;
inline constexpr uint16_t Port=0xF7;
enum class Domain:uint32_t {Process=1,Thread,Module,File,Registry,Service,Driver,Network,Memory,Control};
enum class Operation:uint32_t {Create=1,Open,Read,Write,Rename,Delete,Load,Unload,Start,Stop,Send,Receive,Protect,Exit,Flush,Query};
enum class Stage:uint32_t {Intent=1,Result};
enum class Decision:uint32_t {Allow,Deny,Simulate};
enum class Reason:uint32_t {Observed,Policy,UnsupportedSimulation,Capacity,AccessDenied,OperationFailed,InvalidInput};
inline constexpr uint32_t Mutation=1,ExistedBefore=2,ExistsAfter=4,InvalidText=8,AllowedFlags=15;
struct Info {uint64_t magic=Magic,boot=0;uint32_t version=Version,bytes=sizeof(Info),enabled=1,scenario=0;};
struct Event {
    uint64_t magic=Magic,boot=0,sequence=0,monotonicNs=0,operationId=0,object=0,oldHash=0,newHash=0,related=0;
    uint32_t version=Version,bytes=sizeof(Event),pid=0,tid=0,user=0,actorPid=0,actorTid=0;
    Domain domain=Domain::Control;Operation operation=Operation::Query;Stage stage=Stage::Intent;
    uint32_t status=0;Decision decision=Decision::Allow;Reason reason=Reason::Observed;
    uint32_t flags=0,beforeBytes=0,afterBytes=0,reserved[2]{};
    char resource[128]{},secondary[128]{};
};
struct Policy {uint32_t version=Version,denyMask=0,simulateMask=0,eventLimit=DefaultEvents;};
inline constexpr uint32_t DomainMask=(1u<<10)-1;
inline constexpr uint32_t Bit(Domain d) noexcept {return uint32_t(d)>=1&&uint32_t(d)<=10?1u<<(uint32_t(d)-1):0;}
inline bool ValidPolicy(const Policy& p) noexcept {return p.version==Version&&!(p.denyMask&~DomainMask)&&!(p.simulateMask&~DomainMask)&&!(p.denyMask&p.simulateMask)&&p.eventLimit>=16&&p.eventLimit<=MaxEvents;}
inline bool Text(char* out,const char* in,uint32_t limit=128) noexcept {
    if(!in||!limit)return false;
    for(uint32_t i=0;i<limit;++i){const auto ch=static_cast<uint8_t>(in[i]);if(!ch){out[i]=0;return true;}if(ch<32||ch>126){for(uint32_t k=0;k<limit;++k)out[k]=0;return false;}out[i]=static_cast<char>(ch);}for(uint32_t k=0;k<limit;++k)out[k]=0;return false;
}
inline uint64_t Hash(const uint8_t* p,uint32_t bytes) noexcept {uint64_t h=14695981039346656037ull;for(uint32_t i=0;i<bytes;++i)h=(h^p[i])*1099511628211ull;return h;}
inline bool Mutating(Domain d,Operation op) noexcept {
    if(op==Operation::Exit||op==Operation::Stop||op==Operation::Unload||op==Operation::Flush)return false;
    if(d==Domain::File||d==Domain::Registry)return op==Operation::Create||op==Operation::Write||op==Operation::Rename||op==Operation::Delete||op==Operation::Protect;
    return op==Operation::Create||op==Operation::Load||op==Operation::Start||op==Operation::Send||op==Operation::Protect;
}
inline bool Simulatable(const Event& e) noexcept {return (e.domain==Domain::File||e.domain==Domain::Registry)&&Mutating(e.domain,e.operation);}
using Exchange=void (*)(Event&) noexcept;
inline Exchange exchange=nullptr;
inline uint32_t currentThread=0,currentProcess=0;
inline Event Begin(Domain domain,Operation op,uint32_t pid,uint32_t user,const char* path="",const char* other="",uint64_t object=0) noexcept {
    Event e{};e.domain=domain;e.operation=op;e.pid=pid;e.tid=currentProcess==pid?currentThread:0;e.user=user;e.actorPid=currentProcess;e.actorTid=currentThread;e.object=object;
    if(Mutating(domain,op))e.flags|=Mutation;
    if(!Text(e.resource,path)||!Text(e.secondary,other))e.flags|=InvalidText;
    return e;
}
inline bool Before(Event& e) noexcept {if(exchange)exchange(e);return e.decision!=Decision::Deny;}
inline uint32_t End(Event& e,uint32_t status) noexcept {e.stage=Stage::Result;e.status=status;if(exchange)exchange(e);return status;}
static_assert(sizeof(Info)==32&&sizeof(Event)==400&&sizeof(Policy)==16);
}
