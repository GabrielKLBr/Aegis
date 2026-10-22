#include "TelemetryKernel.h"
#include "guardian/Cpu.h"
#include <intrin.h>
namespace {
uint64_t activeBoot=0,sequence=0;
void Copy(void* to,const void* from,uint32_t bytes) noexcept {auto* d=static_cast<volatile uint8_t*>(to);const auto* s=static_cast<const volatile uint8_t*>(from);for(uint32_t i=0;i<bytes;++i)d[i]=s[i];}
void TransportExchange(guardian::telemetry::Event& e) noexcept {
    using namespace guardian;using namespace telemetry;
    e.boot=activeBoot;e.sequence=++sequence;if(e.stage==Stage::Intent)e.operationId=e.sequence;
    e.monotonicNs=reinterpret_cast<const volatile cpu::Clock*>(cpu::ClockGpa)->monotonicNs;
    Copy(reinterpret_cast<void*>(PacketGpa),&e,sizeof(e));_ReadWriteBarrier();__outbyte(Port,1);_ReadWriteBarrier();
    Event response{};Copy(&response,reinterpret_cast<const void*>(PacketGpa),sizeof(response));
    if(response.magic!=Magic||response.boot!=activeBoot||response.sequence!=sequence||response.version!=Version||response.bytes!=sizeof(response)||response.stage!=e.stage||uint32_t(response.decision)>2||uint32_t(response.reason)>6){e.decision=Decision::Deny;e.reason=Reason::InvalidInput;return;}
    e.decision=response.decision;e.reason=response.reason;
}
}
void TelemetryInitialize(uint64_t boot) noexcept {
    using namespace guardian::telemetry;
    const auto& info=*reinterpret_cast<const Info*>(InfoGpa);
    if(info.magic!=Magic||info.boot!=boot||info.version!=Version||info.bytes!=sizeof(info)||info.enabled!=1)return;
    if(!activeBoot){activeBoot=boot;sequence=0;exchange=TransportExchange;}
}
