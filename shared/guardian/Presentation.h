#pragma once
#include "Desktop.h"
namespace guardian::presentation {
inline constexpr uint64_t Magic=0x31464E49575647ull,InfoOffset=128;
inline constexpr uint32_t Version=1,MaxWidth=1280,MaxHeight=720,MaxFrameBytes=16u<<20,MaxFrames=4096,MaxCommands=131072,MaxInputs=4096;
enum class Operation:uint32_t {Begin=1,Surface,Commit,Poll,Finish};
enum class Kind:uint32_t {None,Move,Button,KeyDown,KeyUp,Text,Focus,Blur,Close,Geometry};
struct Event {
    uint64_t session=0,window=0;Kind kind=Kind::None;uint32_t reserved=0;
    int32_t x=0,y=0;uint32_t key=0,scan=0,modifiers=0,buttons=0,flags=0,padding=0;
    desktop::Rect bounds{};
};
struct Info {uint64_t magic=Magic,boot=0,session=0;uint32_t version=Version,bytes=sizeof(Info);};
struct Packet {
    uint64_t magic=Magic,boot=0,session=0,sequence=0,frameNumber=0;
    uint32_t version=Version,bytes=sizeof(Packet);Operation operation=Operation::Poll;uint32_t status=0;
    uint64_t window=0,inputs=0,rejected=0;uint32_t width=0,height=0,stride=0,pixelBytes=0;
    desktop::Frame frame{};uint64_t stacking[desktop::MaxWindows]{};Event event{};
};
inline bool EmptyRect(const desktop::Rect& r) noexcept {return !r.left&&!r.top&&!r.right&&!r.bottom;}
inline bool ValidEvent(const Event& e) noexcept {
    if(!e.session||!e.window||e.reserved||e.padding||e.modifiers&~15u||e.buttons&~7u||e.flags&~1u||e.scan>255)return false;
    const bool scalar=e.key&&e.key<=0x10FFFF&&!(e.key>=0xD800&&e.key<=0xDFFF);
    if(e.kind==Kind::Geometry)return !e.x&&!e.y&&!e.key&&!e.scan&&!e.modifiers&&!e.buttons&&!e.flags&&e.bounds.left>=-32768&&e.bounds.top>=-32768&&e.bounds.right<=32768&&e.bounds.bottom<=32768&&int64_t(e.bounds.right)-e.bounds.left>0&&int64_t(e.bounds.bottom)-e.bounds.top>0&&int64_t(e.bounds.right)-e.bounds.left<=4096&&int64_t(e.bounds.bottom)-e.bounds.top<=4096;
    if(!EmptyRect(e.bounds))return false;
    switch(e.kind){
    case Kind::Move:case Kind::Button:return e.x>=0&&e.y>=0&&e.x<int32_t(MaxWidth)&&e.y<int32_t(MaxHeight)&&!e.key&&!e.scan&&!e.flags;
    case Kind::KeyDown:case Kind::KeyUp:return !e.x&&!e.y&&e.key>0&&e.key<256&&!e.buttons;
    case Kind::Text:return !e.x&&!e.y&&scalar&&!e.buttons&&!e.scan&&!e.flags;
    case Kind::Focus:case Kind::Blur:case Kind::Close:return !e.x&&!e.y&&!e.key&&!e.scan&&!e.modifiers&&!e.buttons&&!e.flags;
    default:return false;
    }
}
inline int32_t Physical(int32_t logical,uint32_t dpi) noexcept {return int32_t((int64_t(logical)*dpi+(logical>=0?48:-48))/96);}
inline int32_t Logical(int32_t physical,uint32_t dpi) noexcept {return dpi?int32_t((int64_t(physical)*96+(physical>=0?int64_t(dpi)/2:-int64_t(dpi)/2))/dpi):0;}
static_assert(sizeof(Packet)<16384&&sizeof(Event)==72);
}
