#pragma once
#include "NativeUser32.h"
namespace guardian::nativegdi {
#ifdef GUARDIAN_NATIVE_CONTROLS
inline constexpr uint64_t Magic=0x344C5254434E4741ull;
inline constexpr uint32_t Version=4,MaxSurfaces=32;
#else
inline constexpr uint64_t Magic=0x334944474E4741ull;
inline constexpr uint32_t Version=3,MaxSurfaces=8;
#endif
inline constexpr uint32_t MaxObjects=128,MaxDcs=32,MaxPixels=512*512,Chunk=128,MaxBytes=24u<<20;
#pragma pack(push,4)
enum class Op:uint32_t {None,Dc,Release,Paint,EndPaint,MemoryDc,DeleteDc,Brush,Pen,Font,Bitmap,Upload,Read,Stock,Select,Delete,Current,Save,Restore,Clip,ClipRegion,Region,Fill,Rectangle,Move,Line,TextColor,BkColor,BkMode,OutputText,FormatText,Extent,Metrics,BitBlt,Stretch,Pixel,Flush,Caps};
enum class Kind:uint32_t {Dc=3,Brush,Pen,Font,Bitmap,Region};
struct Font {int32_t height=0,width=0,escapement=0,orientation=0,weight=0;uint8_t italic=0,underline=0,strike=0,charset=0,out=0,clip=0,quality=0,pitch=0;uint16_t face[32]{};};
struct Metrics {int32_t height=0,ascent=0,descent=0,internal=0,external=0,average=0,maximum=0,weight=0,overhang=0,aspectX=0,aspectY=0;uint16_t first=0,last=0,def=0,brk=0;uint8_t italic=0,underline=0,strike=0,pitch=0,charset=0,pad[3]{};};
struct Command {Op op=Op::None;uint32_t flags=0;uint64_t dc=0,object=0,other=0;nativeuser::Rect rect{},source{};int32_t args[8]{};uint32_t color=0,mode=0,count=0,offset=0;Font font{};uint16_t text[128]{};uint32_t pixels[Chunk]{};};
struct Reply {uint64_t value=0;uint32_t status=0,reserved=0;nativeuser::Rect rect{};int32_t x=0,y=0;Metrics metrics{};uint16_t face[32]{};uint64_t selected[4]{};uint32_t pixels[Chunk]{};uint32_t live[3]{},calls[uint32_t(Op::Caps)+1]{};uint64_t pixelBytes=0,uploaded=0,downloaded=0;};
struct Packet {uint64_t magic=Magic,boot=0,session=0,sequence=0;uint32_t version=Version,bytes=sizeof(Packet),pid=0,tid=0;nativeuser::Command command{};nativeuser::Reply reply{};Command draw{};Reply graphics{};};
struct Call {uint64_t magic=Magic;uint32_t version=Version,bytes=sizeof(Call);nativeuser::Command command{};nativeuser::Reply reply{};Command draw{};Reply graphics{};};
#pragma pack(pop)
inline bool Empty(const void* p,uint32_t n){return nativegui::Empty(p,n);}
inline bool Text(const uint16_t* p,uint32_t count){if(count>127)return false;for(uint32_t i=0;i<128;++i){auto c=p[i];if(i>=count){if(c)return false;continue;}if(!c||(c<32&&c!=9&&c!=10&&c!=13)||c==127)return false;if(c>=0xD800&&c<=0xDBFF){if(i+1>=count||p[i+1]<0xDC00||p[i+1]>0xDFFF)return false;++i;}else if(c>=0xDC00&&c<=0xDFFF)return false;}return true;}
inline bool Rect(const nativeuser::Rect& r){return r.left>=-4096&&r.top>=-4096&&r.right>=r.left&&r.bottom>=r.top&&r.right<=4096&&r.bottom<=4096;}
inline bool Valid(const Command& c){
    Command v;v.op=c.op;
    switch(c.op){
    case Op::None:case Op::Flush:break;
    case Op::Dc:case Op::Paint:case Op::MemoryDc:case Op::Release:case Op::EndPaint:case Op::DeleteDc:case Op::Delete:case Op::Save:case Op::Metrics:v.dc=c.dc;break;
    case Op::Brush:v.color=c.color;if(c.color>0xFFFFFF)return false;break;
    case Op::Pen:v.color=c.color;v.args[0]=c.args[0];if(c.color>0xFFFFFF||c.args[0]<1||c.args[0]>16)return false;break;
    case Op::Font:v.font=c.font;
#ifdef GUARDIAN_NATIVE_CONTROLS
        if(c.font.height<-96||c.font.height>96||c.font.width||c.font.escapement||c.font.orientation||c.font.weight<0||c.font.weight>1000||c.font.italic>1||c.font.underline>1||c.font.strike>1||c.font.charset>1||c.font.out||c.font.clip||c.font.quality>6||c.font.pitch&~0xF3u)return false;break;
#else
        if(c.font.height<-64||c.font.height>-8||c.font.width||c.font.escapement||c.font.orientation||c.font.weight<100||c.font.weight>900||c.font.italic>1||c.font.underline>1||c.font.strike>1||c.font.charset!=1||c.font.out||c.font.clip||c.font.quality!=3||c.font.pitch)return false;break;
#endif
    case Op::Bitmap:v.args[0]=c.args[0];v.args[1]=c.args[1];if(c.args[0]<1||c.args[0]>512||c.args[1]<1||c.args[1]>512)return false;break;
    case Op::Upload:case Op::Read:v.dc=c.dc;v.offset=c.offset;v.count=c.count;if(!c.count||c.count>Chunk||c.offset>MaxPixels-c.count)return false;if(c.op==Op::Upload)for(uint32_t i=0;i<c.count;++i)v.pixels[i]=c.pixels[i];break;
    case Op::Stock:case Op::Restore:case Op::Current:case Op::BkMode:case Op::Caps:v.dc=c.dc;v.args[0]=c.args[0];break;
    case Op::Select:case Op::ClipRegion:v.dc=c.dc;v.object=c.object;break;
    case Op::Region:v.rect=c.rect;if(!Rect(c.rect))return false;break;
    case Op::Fill:v.object=c.object;[[fallthrough]];
    case Op::Rectangle:case Op::Clip:v.dc=c.dc;v.rect=c.rect;if(!Rect(c.rect))return false;break;
    case Op::Move:case Op::Line:case Op::Pixel:v.dc=c.dc;v.args[0]=c.args[0];v.args[1]=c.args[1];if(c.args[0]<-4096||c.args[0]>4096||c.args[1]<-4096||c.args[1]>4096)return false;break;
    case Op::TextColor:case Op::BkColor:v.dc=c.dc;v.color=c.color;if(c.color>0xFFFFFF)return false;break;
    case Op::OutputText:case Op::FormatText:case Op::Extent:v.dc=c.dc;v.count=c.count;for(uint32_t i=0;i<128;++i)v.text[i]=c.text[i];if(!Text(c.text,c.count))return false;if(c.op==Op::OutputText){v.args[0]=c.args[0];v.args[1]=c.args[1];if(c.args[0]<-4096||c.args[0]>4096||c.args[1]<-4096||c.args[1]>4096)return false;}if(c.op==Op::FormatText){v.rect=c.rect;v.flags=c.flags;if(!Rect(c.rect)||c.flags&~0x00008D3Fu)return false;}break;
    case Op::BitBlt:case Op::Stretch:v.dc=c.dc;if(c.op==Op::Stretch)v.object=c.object;else v.other=c.other;v.rect=c.rect;v.source=c.source;if(!Rect(c.rect)||!Rect(c.source)||c.rect.right==c.rect.left||c.rect.bottom==c.rect.top||c.source.right==c.source.left||c.source.bottom==c.source.top)return false;break;
    default:return false;
    }
    const auto* a=reinterpret_cast<const uint8_t*>(&c);const auto* b=reinterpret_cast<const uint8_t*>(&v);for(uint32_t i=0;i<sizeof(c);++i)if(a[i]!=b[i])return false;return true;
}
static_assert(sizeof(Font)==92&&sizeof(Metrics)==60&&sizeof(Packet)<4096&&sizeof(Call)<4096);
}
