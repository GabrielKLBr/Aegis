extern "C" const volatile char GuardianNativeGuiCapability[]="GuardianNativeGui/1/guest";
namespace {
nativegui::Info nativeGuiInfo;uint64_t nativeGuiSequence=0;bool nativeGuiStarted=false,nativeGuiCompleted=false;
bool NativeGuiStart() noexcept {if(nativeGuiStarted)return true;memcpy(&nativeGuiInfo,reinterpret_cast<const void*>(desktop::InfoGpa+nativegui::InfoOffset),sizeof(nativeGuiInfo));const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);if(nativeGuiInfo.magic!=nativegui::Magic||nativeGuiInfo.boot!=boot->bootId||!nativeGuiInfo.session||nativeGuiInfo.version!=1||nativeGuiInfo.bytes!=sizeof(nativeGuiInfo))return false;nativeGuiStarted=true;return true;}
uint32_t NativeGuiDispatch(RuntimeSlot& slot,uint32_t tid,uint64_t address) noexcept {
    nativegui::Call call;if(!slot.nativeServices->Read(address,&call,sizeof(call))||!slot.nativeServices->Probe(address,sizeof(call),true))return 998;
    if(call.magic!=nativegui::Magic||call.version!=1||call.bytes!=sizeof(call)||call.reserved||call.value||call.status)return 87;
    auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,slot.pid,slot.context.processes->Get(slot.pid)->token.user,"Native GUI service","",uint32_t(call.command.op));if(!telemetry::Before(event))return telemetry::End(event,5);
    if(!NativeGuiStart()||nativeGuiCompleted)return telemetry::End(event,50);nativegui::Packet packet;packet.boot=nativeGuiInfo.boot;packet.session=nativeGuiInfo.session;packet.sequence=++nativeGuiSequence;packet.pid=slot.pid;packet.tid=tid;packet.command=call.command;const auto original=packet;
    memcpy(reinterpret_cast<void*>(desktop::PacketGpa),&packet,sizeof(packet));_ReadWriteBarrier();__outbyte(desktop::Port,4);memcpy(&packet,reinterpret_cast<const void*>(desktop::PacketGpa),sizeof(packet));
    if(packet.magic!=original.magic||packet.boot!=original.boot||packet.session!=original.session||packet.sequence!=original.sequence||packet.pid!=original.pid||packet.tid!=original.tid||packet.version!=1||packet.bytes!=sizeof(packet)){allClean=false;return telemetry::End(event,87);}
    call.value=packet.reply.value;call.status=packet.reply.status;nativeGuiCompleted=packet.reply.completed!=0;if(!slot.nativeServices->Write(address,&call,sizeof(call)))return telemetry::End(event,998);return telemetry::End(event,call.status);
}
bool NativeGuiFinish() noexcept{return !nativeGuiStarted||nativeGuiCompleted;}
}
