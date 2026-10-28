#ifdef GUARDIAN_SOFTWARE_GRAPHICS
#include "guardian/SoftwareGraphics.h"
extern "C" const volatile char GuardianSoftwareGraphicsCapability[]="GuardianSoftwareGraphics/6/guest";
#elif defined(GUARDIAN_GDI_EXPANDED)
extern "C" const volatile char GuardianExpandedGdiCapability[]="GuardianExpandedGdi/5/guest";
#elif defined(GUARDIAN_NATIVE_CONTROLS)
extern "C" const volatile char GuardianNativeControlsCapability[]="GuardianNativeControls/4/guest";
#else
extern "C" const volatile char GuardianNativeGdiCapability[]="GuardianNativeGdi/3/guest";
#endif
namespace {
nativegui::Info nativeGdiInfo;nativegdi::Packet nativeGdiPacket;uint64_t nativeGdiSequence=0;uint32_t nativeGdiPid=0,nativeGdiTid=0;bool nativeGdiStarted=false,nativeGdiCompleted=false;
bool NativeGdiStart() noexcept {if(nativeGdiStarted)return true;memcpy(&nativeGdiInfo,reinterpret_cast<const void*>(desktop::InfoGpa+nativegui::InfoOffset),sizeof(nativeGdiInfo));const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);if(nativeGdiInfo.magic!=nativegdi::Magic||nativeGdiInfo.boot!=boot->bootId||!nativeGdiInfo.session||nativeGdiInfo.version!=nativegdi::Version||nativeGdiInfo.bytes!=sizeof(nativeGdiInfo))return false;nativeGdiStarted=true;return true;}
bool NativeGdiExchange() noexcept {auto& p=nativeGdiPacket;p.boot=nativeGdiInfo.boot;p.session=nativeGdiInfo.session;p.sequence=++nativeGdiSequence;p.pid=nativeGdiPid;p.tid=nativeGdiTid;memcpy(reinterpret_cast<void*>(desktop::PacketGpa),&p,sizeof(p));_ReadWriteBarrier();__outbyte(desktop::Port,6);memcpy(&p,reinterpret_cast<const void*>(desktop::PacketGpa),sizeof(p));if(p.magic!=nativegdi::Magic||p.version!=nativegdi::Version||p.bytes!=sizeof(p)||p.boot!=nativeGdiInfo.boot||p.session!=nativeGdiInfo.session||p.sequence!=nativeGdiSequence||p.pid!=nativeGdiPid||p.tid!=nativeGdiTid){allClean=false;return false;}nativeGdiCompleted=p.reply.completed!=0;return true;}
uint32_t NativeGdiDispatch(RuntimeSlot& slot,uint32_t tid,uint64_t address) noexcept {
    nativegdi::Call call;if(!slot.nativeServices->Read(address,&call,sizeof(call))||!slot.nativeServices->Probe(address,sizeof(call),true))return 998;
    if(call.magic!=nativegdi::Magic||call.version!=nativegdi::Version||call.bytes!=sizeof(call)||!nativegdi::Empty(&call.reply,sizeof(call.reply))||!nativegdi::Empty(&call.graphics,sizeof(call.graphics)))return 87;
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
    if(!softwaregraphics::Valid(call.draw)||!nativecontrols::Valid(call.command))return 87;
#endif
    if(call.draw.op==nativegdi::Op::None&&(call.command.op==nativeuser::Op::Hello||call.command.op==nativeuser::Op::Finish||call.command.op==nativeuser::Op::ThreadExit))return 87;
    auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,slot.pid,slot.context.processes->Get(slot.pid)->token.user,"Native GDI service","",call.draw.op==nativegdi::Op::None?uint32_t(call.command.op):256+uint32_t(call.draw.op));if(!telemetry::Before(event))return telemetry::End(event,5);
    if(!NativeGdiStart()||nativeGdiCompleted)return telemetry::End(event,50);nativeGdiPid=slot.pid;nativeGdiTid=tid;nativeGdiPacket={};nativeGdiPacket.command=call.command;nativeGdiPacket.draw=call.draw;if(!NativeGdiExchange())return telemetry::End(event,87);call.reply=nativeGdiPacket.reply;call.graphics=nativeGdiPacket.graphics;call.reply.restrictions=call.reply.helperPid=0;for(auto& n:call.reply.calls)n=0;for(auto& n:call.graphics.calls)n=0;
    if(!slot.nativeServices->Write(address,&call,sizeof(call)))return telemetry::End(event,998);return telemetry::End(event,call.reply.status);
}
void NativeGdiThreadGone(uint32_t pid,uint32_t tid) noexcept {if(!nativeGdiStarted||nativeGdiCompleted)return;nativeGdiPid=pid;nativeGdiTid=tid;nativeGdiPacket={};nativeGdiPacket.command.op=nativeuser::Op::ThreadExit;if(!NativeGdiExchange()||nativeGdiPacket.reply.status)allClean=false;}
bool NativeGdiFinish() noexcept {if(!nativeGdiStarted||nativeGdiCompleted)return true;nativeGdiPacket={};nativeGdiPacket.command.op=nativeuser::Op::Finish;nativeGdiPacket.command.flags=1;return NativeGdiExchange()&&!nativeGdiPacket.reply.status&&nativeGdiCompleted;}
}
