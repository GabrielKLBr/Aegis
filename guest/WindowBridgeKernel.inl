extern "C" const volatile char GuardianWindowBridgeCapability[]="GuardianWindowBridge/1/guest";
namespace {
presentation::Packet windowPacket;uint64_t windowSession=0,windowSequence=0,windowFrames=0,windowInputs=0,windowRejected=0;
bool WindowPacket(presentation::Operation operation) noexcept {
    windowPacket.magic=presentation::Magic;windowPacket.boot=desktopReport.boot;windowPacket.session=windowSession;windowPacket.sequence=++windowSequence;windowPacket.version=presentation::Version;windowPacket.bytes=sizeof(windowPacket);windowPacket.operation=operation;windowPacket.status=0;windowPacket.frameNumber=windowFrames;windowPacket.event={};
    memcpy(reinterpret_cast<void*>(desktop::PacketGpa),&windowPacket,sizeof(windowPacket));_ReadWriteBarrier();__outbyte(desktop::Port,3);memcpy(&windowPacket,reinterpret_cast<const void*>(desktop::PacketGpa),sizeof(windowPacket));
    if(windowPacket.magic!=presentation::Magic||windowPacket.boot!=desktopReport.boot||windowPacket.session!=windowSession||windowPacket.sequence!=windowSequence||windowPacket.operation!=operation||windowPacket.version!=presentation::Version||windowPacket.bytes!=sizeof(windowPacket)||windowPacket.status>1){allClean=false;return false;}return true;
}
bool WindowStart(const desktop::Info& info) noexcept {
    if(!info.reserved)return true;presentation::Info bridge;memcpy(&bridge,reinterpret_cast<const void*>(desktop::InfoGpa+presentation::InfoOffset),sizeof(bridge));if(info.reserved!=1||bridge.magic!=presentation::Magic||bridge.boot!=info.boot||!bridge.session||bridge.version!=presentation::Version||bridge.bytes!=sizeof(bridge))return false;windowSession=bridge.session;return true;
}
void WindowPresent() noexcept {
    if(!windowSession)return;const auto frame=desktopCore.Metadata();windowPacket={};windowPacket.frame=frame;for(uint32_t i=0;i<frame.count;++i)windowPacket.stacking[i]=desktopCore.WindowOrder(frame.windows[i].handle);++windowFrames;if(!WindowPacket(presentation::Operation::Begin))return;
    for(uint32_t i=0;i<frame.count;++i){const auto& w=frame.windows[i];if(w.parent||!(w.flags&1))continue;uint32_t width=0,height=0;if(!desktopCore.RenderWindow(w.handle,reinterpret_cast<uint32_t*>(desktop::PixelsGpa),desktop::PixelCount,width,height)){allClean=false;break;}windowPacket={};windowPacket.window=w.handle;windowPacket.width=width;windowPacket.height=height;windowPacket.stride=width*4;windowPacket.pixelBytes=width*height*4;if(!WindowPacket(presentation::Operation::Surface))break;}
    windowPacket={};WindowPacket(presentation::Operation::Commit);
    // Preserve the existing composite hash/report and optional viewer fallback.
    desktopCore.Render(reinterpret_cast<uint32_t*>(desktop::PixelsGpa));
}
void WindowPoll() noexcept {
    windowPacket={};if(!WindowPacket(presentation::Operation::Poll)||windowPacket.status)return;++windowInputs;const auto status=desktopCore.InjectWindow(windowPacket.event,windowSession,desktopCurrentNow);if(status==5||status==1400){++windowRejected;return;}if(status)allClean=false;
}
void WindowFinish() noexcept {if(!windowSession)return;windowPacket={};windowPacket.inputs=windowInputs;windowPacket.rejected=windowRejected;WindowPacket(presentation::Operation::Finish);}
}
