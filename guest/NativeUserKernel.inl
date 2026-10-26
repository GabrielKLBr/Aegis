extern "C" const volatile char GuardianNativeUserCapability[]="GuardianNativeUser32/2/guest";
namespace {
nativeuser::Info nativeUserInfo;uint64_t nativeUserSequence=0;uint32_t nativeUserPid=0,nativeUserTid=0;bool nativeUserStarted=false,nativeUserCompleted=false;
bool NativeUserStart() noexcept {if(nativeUserStarted)return true;memcpy(&nativeUserInfo,reinterpret_cast<const void*>(desktop::InfoGpa+nativegui::InfoOffset),sizeof(nativeUserInfo));const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);if(nativeUserInfo.magic!=nativeuser::Magic||nativeUserInfo.boot!=boot->bootId||!nativeUserInfo.session||nativeUserInfo.version!=2||nativeUserInfo.bytes!=sizeof(nativeUserInfo))return false;nativeUserStarted=true;return true;}
bool NativeUserExchange(nativeuser::Packet& p) noexcept {
    p.boot=nativeUserInfo.boot;p.session=nativeUserInfo.session;p.sequence=++nativeUserSequence;p.pid=nativeUserPid;p.tid=nativeUserTid;
    const auto sequence=p.sequence;memcpy(reinterpret_cast<void*>(desktop::PacketGpa),&p,sizeof(p));_ReadWriteBarrier();__outbyte(desktop::Port,5);memcpy(&p,reinterpret_cast<const void*>(desktop::PacketGpa),sizeof(p));
    if(p.magic!=nativeuser::Magic||p.boot!=nativeUserInfo.boot||p.session!=nativeUserInfo.session||p.sequence!=sequence||p.pid!=nativeUserPid||p.tid!=nativeUserTid||p.version!=2||p.bytes!=sizeof(p)){allClean=false;return false;}nativeUserCompleted=p.reply.completed!=0;return true;
}
uint32_t NativeUserDispatch(RuntimeSlot& slot,uint32_t tid,uint64_t address) noexcept {
    nativeuser::Call call;if(!slot.nativeServices->Read(address,&call,sizeof(call))||!slot.nativeServices->Probe(address,sizeof(call),true))return 998;
    if(call.magic!=nativeuser::Magic||call.version!=2||call.bytes!=sizeof(call)||!nativegui::Empty(&call.reply,sizeof(call.reply)))return 87;
    // Lifetime control is emitted only by the kernel, never by an application.
    if(call.command.op==nativeuser::Op::Hello||call.command.op==nativeuser::Op::Finish||call.command.op==nativeuser::Op::ThreadExit)return 87;
    auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,slot.pid,slot.context.processes->Get(slot.pid)->token.user,"Native USER32 service","",uint32_t(call.command.op));if(!telemetry::Before(event))return telemetry::End(event,5);
    if(!NativeUserStart()||nativeUserCompleted)return telemetry::End(event,50);nativeUserPid=slot.pid;nativeUserTid=tid;nativeuser::Packet packet;packet.command=call.command;
    if(!NativeUserExchange(packet))return telemetry::End(event,87);call.reply=packet.reply;
    // Supervisor diagnostics, native PID and native counters are never guest API results.
    call.reply.restrictions=call.reply.helperPid=0;for(auto& c:call.reply.calls)c=0;
    if(!slot.nativeServices->Write(address,&call,sizeof(call)))return telemetry::End(event,998);return telemetry::End(event,call.reply.status);
}
bool NativeUserFinish() noexcept {if(!nativeUserStarted||nativeUserCompleted)return true;nativeuser::Packet packet;packet.command.op=nativeuser::Op::Finish;packet.command.flags=1;return NativeUserExchange(packet)&&!packet.reply.status&&nativeUserCompleted;}
void NativeUserThreadGone(uint32_t pid,uint32_t tid) noexcept {if(!nativeUserStarted||nativeUserCompleted)return;nativeUserPid=pid;nativeUserTid=tid;nativeuser::Packet packet;packet.command.op=nativeuser::Op::ThreadExit;if(!NativeUserExchange(packet)||packet.reply.status)allClean=false;}
}
