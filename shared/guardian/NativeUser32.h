#pragma once
#include "NativeGui.h"
// Version 2 is intentionally separate from the 14.10A GDI command protocol.
// All references are session IDs. No native handle, procedure or pointer crosses IPC.
namespace guardian::nativeuser {
inline constexpr uint64_t Magic=0x32524553554E4741ull;
inline constexpr uint32_t Version=2,MaxWindows=32,MaxEvents=256,MaxPending=32,MaxTimers=32,MaxCommands=65536;
using Rect=nativegui::Rect;
enum class Op:uint32_t {Hello,Create,Destroy,Query,Position,Enable,Show,Focus,Text,GetText,NativeMessage,Send,Post,Poll,Reply,Result,Timer,KillTimer,Invalidate,Finish,Style,Parent,ThreadExit};
enum class Class:uint32_t {Custom=1,Button,Static,Edit};
struct Command {
    Op op=Op::Hello;uint32_t flags=0;uint64_t window=0,other=0;
    Rect rect{};uint32_t style=0,exStyle=0;Class kind{};uint32_t id=0,message=0,reserved=0;
    uint64_t wp=0,lp=0;uint16_t text[256]{};
};
struct Window {uint64_t id=0,parent=0;uint32_t pid=0,tid=0;Class kind{};uint32_t style=0,exStyle=0,enabled=0,visible=0,dpi=0;Rect bounds{},client{};int32_t originX=0,originY=0;};
// Sent=1, native input (already translated)=2, marshalled WINDOWPOS=4.
struct Event {uint64_t window=0,cookie=0,wp=0,lp=0,serial=0;uint32_t message=0,flags=0,tid=0,time=0;Rect rect{};uint32_t positionFlags=0,reserved=0;};
struct Reply {uint64_t value=0;uint32_t status=0,completed=0;Window window{};Event event{};uint16_t text[256]{};uint32_t live[3]{},restrictions=0,helperPid=0,reserved=0,calls[8]{};};
struct Packet {uint64_t magic=Magic,boot=0,session=0,sequence=0;uint32_t version=Version,bytes=sizeof(Packet),pid=0,tid=0;Command command{};Reply reply{};};
struct Call {uint64_t magic=Magic;uint32_t version=Version,bytes=sizeof(Call);Command command{};Reply reply{};};
using Info=nativegui::Info;
inline bool Text(const uint16_t* t) noexcept {
    bool end=false;
    for(uint32_t i=0;i<256;++i){auto c=t[i];if(!c){end=true;continue;}if(end||c<32||c==127)return false;
        if(c>=0xD800&&c<=0xDBFF){if(i+1>=256||t[i+1]<0xDC00||t[i+1]>0xDFFF)return false;++i;}
        else if(c>=0xDC00&&c<=0xDFFF)return false;}
    return end;
}
inline bool Geometry(const Rect& r) noexcept {return r.left>=-8192&&r.left<=8192&&r.top>=-8192&&r.top<=8192&&int64_t(r.right)-r.left>=1&&int64_t(r.right)-r.left<=2048&&int64_t(r.bottom)-r.top>=1&&int64_t(r.bottom)-r.top<=2048;}
inline bool ScalarMessage(uint32_t m) noexcept {return m==0||m==0x10||(m>=0x8000&&m<0xC000);}
inline bool Valid(const Command& c) noexcept {
    if(c.reserved||!Text(c.text))return false;
    Command v;v.op=c.op;
    switch(c.op){
    case Op::Hello:case Op::ThreadExit:break;
    case Op::Finish:v.flags=c.flags;if(c.flags>1)return false;break;
    case Op::Create:v.other=c.other;v.rect=c.rect;v.style=c.style;v.exStyle=c.exStyle;v.kind=c.kind;v.id=c.id;for(uint32_t i=0;i<256;++i)v.text[i]=c.text[i];if(!Geometry(c.rect)||uint32_t(c.kind)<1||uint32_t(c.kind)>4||c.id>65535||c.exStyle|| (c.style&0x0000F000u))return false;break;
    case Op::Position:v.window=c.window;v.rect=c.rect;v.flags=c.flags;if(!Geometry(c.rect)||c.flags&~0x57Fu)return false;break;
    case Op::Enable:case Op::Show:v.window=c.window;v.flags=c.flags;if(c.flags>1)return false;break;
    case Op::Focus:v.window=c.window;v.flags=c.flags;if(c.flags>1||(c.flags&&c.window))return false;break;
    case Op::Style:v.window=c.window;v.style=c.style;if(c.style&0xF000)return false;break;
    case Op::Parent:v.window=c.window;v.other=c.other;break;
    case Op::Destroy:case Op::Query:case Op::GetText:case Op::Invalidate:v.window=c.window;break;
    case Op::Text:v.window=c.window;for(uint32_t i=0;i<256;++i)v.text[i]=c.text[i];break;
    case Op::NativeMessage:v.window=c.window;v.message=c.message;v.wp=c.wp;v.lp=c.lp;break;
    case Op::Send:case Op::Post:v.window=c.window;v.message=c.message;v.wp=c.wp;v.lp=c.lp;if(!ScalarMessage(c.message))return false;break;
    case Op::Poll:v.window=c.window;v.flags=c.flags;v.wp=c.wp;v.lp=c.lp;if(c.flags>3||c.wp>65535||c.lp>65535||(c.lp&&c.wp>c.lp))return false;break;
    case Op::Reply:v.window=c.window;v.wp=c.wp;break;
    case Op::Result:v.window=c.window;break;
    case Op::Timer:v.window=c.window;v.wp=c.wp;v.lp=c.lp;if(!c.wp||c.wp>65535||c.lp<10||c.lp>60000)return false;break;
    case Op::KillTimer:v.window=c.window;v.wp=c.wp;break;
    default:return false;
    }
    const auto* a=reinterpret_cast<const uint8_t*>(&c);const auto* b=reinterpret_cast<const uint8_t*>(&v);
    for(uint32_t i=0;i<sizeof(c);++i)if(a[i]!=b[i])return false;
    return true;
}
static_assert(sizeof(Packet)<4096&&sizeof(Call)<4096);
}
