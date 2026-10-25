#pragma once
#include "Abi.h"
namespace guardian::desktop {
inline constexpr uint64_t Magic=0x37495547415647ull,InfoGpa=0x1F3000,PacketGpa=0x1F4000,ReportGpa=0x1F8000;
inline constexpr uint64_t PixelsGpa=0x5200000,StateGpa=0x5800000,StateBytes=2u<<20;
inline constexpr uint32_t Version=1,Width=1280,Height=720,PixelCount=Width*Height,MaxWindows=32,QueueDepth=64,MaxThreads=16,MaxDrawings=128;
inline constexpr uint16_t Port=0xF8;
enum class InputType:uint32_t {None,Move,Button,KeyDown,KeyUp,Text,Blur,Close};
struct Input {InputType type{};int32_t x=0,y=0;uint32_t key=0,scan=0,modifiers=0,buttons=0,flags=0;uint64_t serial=0;};
struct Rect {int32_t left=0,top=0,right=0,bottom=0;};
struct Window {uint64_t handle=0,parent=0,owner=0;uint32_t pid=0,tid=0,style=0,exStyle=0,dpi=96,flags=0;Rect bounds{},client{};uint16_t title[64]{};};
struct Frame {uint32_t width=Width,height=Height,stride=Width*4,bytes=PixelCount*4,count=0,reserved=0;uint64_t focus=0;Window windows[MaxWindows]{};};
struct Message {uint64_t window=0;uint32_t id=0,time=0;uint64_t wparam=0;int64_t lparam=0;int32_t x=0,y=0;uint32_t flags=0,reserved=0;uint64_t cookie=0;};
enum class Draw:uint32_t {Fill=1,Text,Line,Pixels};
struct Primitive {Draw kind=Draw::Fill;Rect rect{},clip{};uint32_t color=0,background=0xFFFFFF,mode=1,height=14,width=8,count=0;uint16_t text[128]{};};
enum class Op:uint32_t {Create=1,Destroy,Query,Position,Parent,Style,Title,Focus,Foreground,Invalidate,BeginPaint,EndPaint,Draw,Poll,Post,Reply,ReplyPoll,Timer,KillTimer,KeyState,Translate,Present,Clip,Dark,ThreadPolicy=40};
struct Request {uint64_t magic=Magic;uint32_t version=Version,bytes=sizeof(Request);Op op=Op::Query;uint32_t flags=0,error=0,index=0;uint64_t handle=0,other=0,value=0,extra=0;Window window{};Message message{};Primitive primitive{};Rect rect{};uint8_t keys[256]{};};
struct Info {uint64_t magic=Magic,boot=0;uint32_t version=Version,bytes=sizeof(Info),width=Width,height=Height,interactive=0,reserved=0;};
struct Packet {uint64_t magic=Magic,boot=0,sequence=0;uint32_t version=Version,bytes=sizeof(Packet),op=0,status=0;Input input{};Frame frame{};};
struct Report {uint64_t magic=Magic,boot=0;uint32_t version=Version,bytes=sizeof(Report);uint64_t calls=0,frames=0,inputs=0,denied=0,liveWindows=0,liveMessages=0,hash=0;};
inline bool ValidInput(const Input& e) noexcept {
    if(e.serial||e.modifiers&~15u||e.buttons&~7u||e.flags&~1u||e.scan>255||e.x<0||e.y<0||e.x>=int32_t(Width)||e.y>=int32_t(Height))return false;
    switch(e.type){case InputType::Move:return !e.key&&!e.scan&&!e.flags;case InputType::Button:return !e.key&&!e.scan;
    case InputType::KeyDown:case InputType::KeyUp:return e.key>0&&e.key<256&&!e.buttons;
    case InputType::Text:return e.key&&e.key<=0x10FFFF&&!(e.key>=0xD800&&e.key<=0xDFFF)&&!e.buttons&&!e.scan&&!e.flags;
    case InputType::Blur:case InputType::Close:return !e.key&&!e.buttons&&!e.scan&&!e.flags;default:return false;}
}
inline uint64_t Hash(const uint32_t* p) noexcept {uint64_t h=14695981039346656037ull;for(uint32_t i=0;i<PixelCount;++i){h^=p[i];h*=1099511628211ull;}return h;}
bool ValidFrame(const Frame&) noexcept;
static_assert(sizeof(Request)<1200&&sizeof(Packet)<16384&&sizeof(Input)==40&&sizeof(Message)==56);
}
