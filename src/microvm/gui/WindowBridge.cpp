#include "WindowBridge.h"
#include "../state/Files.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif
namespace av::microvm::window_session {
namespace d=guardian::desktop;namespace p=guardian::presentation;
namespace {
const d::Window* Find(const d::Frame& frame,uint64_t handle){for(uint32_t i=0;i<frame.count;++i)if(frame.windows[i].handle==handle)return &frame.windows[i];return nullptr;}
bool UnicodeTitle(const uint16_t* text){for(uint32_t i=0;i<64&&text[i];++i){if(text[i]>=0xDC00&&text[i]<=0xDFFF)return false;if(text[i]>=0xD800&&text[i]<=0xDBFF){if(++i==64||text[i]<0xDC00||text[i]>0xDFFF)return false;}}return true;}
}
uint64_t NewSession(){uint64_t value=0;
#ifdef _WIN32
    state::Require(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&value),sizeof(value),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Window session entropy failed");
#else
    state::Require(getrandom(&value,sizeof(value),0)==sizeof(value),"Window session entropy failed");
#endif
    state::Require(value!=0,"Window session identifier rejected");return value;
}
bool ValidMetadata(const d::Frame& frame) noexcept {
    if(!d::ValidFrame(frame))return false;uint64_t total=0;
    for(uint32_t i=0;i<frame.count;++i){const auto& w=frame.windows[i];if(!UnicodeTitle(w.title))return false;if(w.owner){const auto* owner=Find(frame,w.owner);if(!owner||owner->parent)return false;}
        if(!w.parent&&(w.flags&1)){const int64_t width=int64_t(w.client.right)-w.client.left,height=int64_t(w.client.bottom)-w.client.top;if(width<=0||height<=0||width>p::MaxWidth||height>p::MaxHeight)return false;total+=uint64_t(width)*uint64_t(height)*4;if(total>p::MaxFrameBytes)return false;}
    }return true;
}
Bridge::Bridge(uint64_t boot,uint64_t session,Callbacks callbacks):boot_(boot),session_(session),callbacks_(std::move(callbacks)){state::Require(boot&&session,"Window bridge session rejected");}
p::Info Bridge::Enumeration() const {p::Info info;info.boot=boot_;info.session=session_;return info;}
bool Bridge::Exchange(p::Packet& packet,const uint32_t* pixels,uint64_t now){
    if(completed_||packet.magic!=p::Magic||packet.boot!=boot_||packet.session!=session_||packet.version!=p::Version||packet.bytes!=sizeof(packet)||packet.status||packet.sequence!=sequence_+1||now<now_||++commands_>p::MaxCommands)return false;
    ++sequence_;now_=now;
    if(packet.operation==p::Operation::Begin){
        if(staging_||frames_==p::MaxFrames||packet.frameNumber!=frames_+1||!ValidMetadata(packet.frame))return false;
        const auto second=now/1000000000;if(second!=rateSecond_){rateSecond_=second;rateFrames_=rateBytes_=0;}if(++rateFrames_>512)return false;
        for(uint32_t i=0;i<packet.frame.count;++i){const auto& w=packet.frame.windows[i];const auto* old=Find(committed_,w.handle);if(old&&(old->pid!=w.pid||old->tid!=w.tid))return false;}
        for(uint32_t i=0;i<packet.frame.count;++i){const auto& w=packet.frame.windows[i];bool known=false;for(const auto& old:identities_)if(old.handle==w.handle){known=true;if(old.pid!=w.pid||old.tid!=w.tid||!Find(committed_,w.handle))return false;}if(!known){if(identities_.size()==4096)return false;identities_.push_back(w);}}
        pending_={};pending_.session=session_;pending_.number=packet.frameNumber;pending_.metadata=packet.frame;std::copy_n(packet.stacking,d::MaxWindows,pending_.stacking.begin());stagingBytes_=0;staging_=true;return true;
    }
    if(packet.operation==p::Operation::Surface){
        if(!staging_||packet.frameNumber!=pending_.number||!pixels)return false;const auto* w=Find(pending_.metadata,packet.window);
        if(!w||w->parent||!(w->flags&1)||packet.width!=uint32_t(w->client.right-w->client.left)||packet.height!=uint32_t(w->client.bottom-w->client.top)||packet.stride!=packet.width*4||packet.pixelBytes!=packet.width*packet.height*4||packet.pixelBytes>p::MaxFrameBytes-stagingBytes_)return false;
        for(const auto& image:pending_.images)if(image.window==packet.window)return false;
        if(packet.pixelBytes>(128u<<20)-rateBytes_)return false;rateBytes_+=packet.pixelBytes;Image image;image.window=packet.window;image.width=packet.width;image.height=packet.height;image.pixels.assign(pixels,pixels+packet.pixelBytes/4);stagingBytes_+=packet.pixelBytes;pending_.images.push_back(std::move(image));return true;
    }
    if(packet.operation==p::Operation::Commit){
        if(!staging_||packet.frameNumber!=pending_.number)return false;size_t roots=0;for(uint32_t i=0;i<pending_.metadata.count;++i)if(!pending_.metadata.windows[i].parent&&(pending_.metadata.windows[i].flags&1))++roots;if(pending_.images.size()!=roots)return false;
        if(callbacks_.display)callbacks_.display(pending_);committed_=pending_.metadata;pending_={};staging_=false;++frames_;return true;
    }
    if(packet.operation==p::Operation::Poll){
        if(staging_||packet.frameNumber!=frames_)return false;packet.event={};packet.status=1;if(!callbacks_.poll)return true;
        auto event=callbacks_.poll();if(event.kind==p::Kind::None)return true;
        if(!p::ValidEvent(event)||event.session!=session_)return false;const auto* w=Find(committed_,event.window);
        if(!w||w->parent||event.kind!=p::Kind::Blur&&(!(w->flags&1)||(w->style&0x08000000))){++rejected_;return true;}
        if((event.kind==p::Kind::Move||event.kind==p::Kind::Button)&&(event.x>=w->client.right-w->client.left||event.y>=w->client.bottom-w->client.top)){++rejected_;return true;}
        if(inputs_==p::MaxInputs)return false;packet.event=event;packet.status=0;delivered_.push_back({now,event.window,uint32_t(event.kind)});++inputs_;return true;
    }
    if(packet.operation==p::Operation::Finish){if(staging_||!frames_||committed_.count||packet.frameNumber!=frames_||packet.inputs!=inputs_||packet.rejected>inputs_)return false;guestRejected_=packet.rejected;completed_=true;return true;}
    return false;
}
void Bridge::Export(const std::filesystem::path& output) const {if(output.empty())return;std::ostringstream text;text<<"{\"schema\":1,\"phase\":\"12F\",\"session\":"<<session_<<",\"frames\":"<<frames_<<",\"inputs\":"<<inputs_<<",\"staleInputsRejected\":"<<rejected_<<",\"guestInputsRejected\":"<<guestRejected_<<",\"commands\":"<<commands_<<",\"completed\":"<<(completed_?"true":"false")<<",\"liveGuestWindows\":"<<committed_.count<<",\"staging\":"<<(staging_?"true":"false")<<",\"hostCallbacks\":false,\"hostWindowHandlesInGuest\":false}\n";auto base=text.str();if(base.size()>=2)base.resize(base.size()-2);std::ostringstream timeline;timeline<<base<<",\"timeline\":[";for(size_t i=0;i<delivered_.size();++i){if(i)timeline<<',';const auto& e=delivered_[i];timeline<<"{\"ns\":"<<e.ns<<",\"window\":"<<e.window<<",\"kind\":"<<e.kind<<'}';}timeline<<"]}\n";state::AtomicText(output/"window-bridge.json",timeline.str());}
}
