extern "C" const volatile char GuardianDesktopCapability[]="GuardianDesktop/1/guest";
namespace {
desktop::Core desktopCore;desktop::Report desktopReport;desktop::Packet desktopPacket;uint64_t desktopSequence=0,desktopLastFrame=0,desktopCurrentNow=0;bool desktopStarted=false;
void DesktopExchange(uint32_t op) noexcept {
    desktopPacket.magic=desktop::Magic;desktopPacket.boot=desktopReport.boot;desktopPacket.sequence=++desktopSequence;desktopPacket.version=1;desktopPacket.bytes=sizeof(desktopPacket);desktopPacket.op=op;desktopPacket.status=0;desktopPacket.input={};
    if(op==2){desktopPacket.frame=desktopCore.Metadata();desktopCore.Render(reinterpret_cast<uint32_t*>(desktop::PixelsGpa));}
    memcpy(reinterpret_cast<void*>(desktop::PacketGpa),&desktopPacket,sizeof(desktopPacket));_ReadWriteBarrier();__outbyte(desktop::Port,1);memcpy(&desktopPacket,reinterpret_cast<const void*>(desktop::PacketGpa),sizeof(desktopPacket));
    if(desktopPacket.magic!=desktop::Magic||desktopPacket.boot!=desktopReport.boot||desktopPacket.sequence!=desktopSequence||desktopPacket.op!=op||desktopPacket.version!=1||desktopPacket.bytes!=sizeof(desktopPacket)||desktopPacket.status>1){allClean=false;return;}
    if(op==2){++desktopReport.frames;desktopCore.Presented();}
    else if(!desktopPacket.status){if(!desktop::ValidInput(desktopPacket.input)||desktopCore.Inject(desktopPacket.input,desktopCurrentNow)){allClean=false;return;}++desktopReport.inputs;}
}
bool DesktopStart(RuntimeSlot& slot,threads::Scheduler& scheduler) noexcept {
    if(desktopStarted)return true;desktop::Info info;memcpy(&info,reinterpret_cast<const void*>(desktop::InfoGpa),sizeof(info));const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);
    if(info.magic!=desktop::Magic||info.boot!=boot->bootId||info.version!=1||info.bytes!=sizeof(info)||info.width!=desktop::Width||info.height!=desktop::Height||info.interactive>1||info.reserved)return false;
    desktopCore.Initialize(*slot.context.processes,scheduler,*reinterpret_cast<desktop::State*>(desktop::StateGpa));desktopReport={};desktopReport.boot=info.boot;desktopStarted=true;return true;
}
uint32_t DesktopDispatch(RuntimeSlot& slot,uint32_t tid,uint64_t now,uint64_t address) noexcept {
    desktop::Request request;if(!slot.nativeServices->Read(address,&request,sizeof(request))||!slot.nativeServices->Probe(address,sizeof(request),true))return 998;
    ++desktopReport.calls;auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,slot.pid,slot.context.processes->Get(slot.pid)->token.user,"Desktop service","",uint32_t(request.op));if(!telemetry::Before(event))return telemetry::End(event,5);
    desktopCurrentNow=now;if(request.op==desktop::Op::Poll){DesktopExchange(1);}
    auto status=desktopCore.Execute(slot.pid,tid,now,request);if(status==5||status==1400)++desktopReport.denied;
    const bool flush=request.op==desktop::Op::Present&&(request.flags&2);
    if(desktopCore.Changed()&&(!desktopCore.Painting()||flush)&&(!desktopReport.frames||now-desktopLastFrame>=33333333||flush)){DesktopExchange(2);desktopLastFrame=now;}
    if(!slot.nativeServices->Write(address,&request,sizeof(request)))status=998;return telemetry::End(event,status);
}
bool DesktopFinish() noexcept {
    if(!desktopStarted)return true;if(!desktopCore.Clean())return false;DesktopExchange(2);desktopReport.hash=desktop::Hash(reinterpret_cast<const uint32_t*>(desktop::PixelsGpa));
    memcpy(reinterpret_cast<void*>(desktop::ReportGpa),&desktopReport,sizeof(desktopReport));_ReadWriteBarrier();__outbyte(desktop::Port,2);return allClean;
}
}
