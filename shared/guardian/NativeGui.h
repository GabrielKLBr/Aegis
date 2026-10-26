#pragma once
#include <stdint.h>
namespace guardian::nativegui {
inline constexpr uint64_t Magic=0x314955474E4741ull,InfoOffset=256;
inline constexpr uint32_t Version=1,MaxCommands=1024,MaxObjects=64,MaxWindows=4;
enum class Op:uint32_t {Hello,Window,Brush,Dc,Fill,Pixel,ReleaseDc,DeleteBrush,DestroyWindow,Finish};
enum class Kind:uint32_t {Window=1,Brush,Dc};
struct Rect {int32_t left=0,top=0,right=0,bottom=0;};
struct Command {Op op=Op::Hello;uint32_t flags=0;uint64_t target=0,object=0;Rect rect{};uint32_t color=0,reserved=0;uint16_t title[64]{};};
struct Reply {uint64_t value=0;uint32_t status=0,completed=0,live[3]{},restrictions=0,helperPid=0,reserved=0,calls[8]{};};
struct Packet {uint64_t magic=Magic,boot=0,session=0,sequence=0;uint32_t version=Version,bytes=sizeof(Packet),pid=0,tid=0;Command command{};Reply reply{};};
struct Call {uint64_t magic=Magic;uint32_t version=Version,bytes=sizeof(Call);Command command{};uint64_t value=0;uint32_t status=0,reserved=0;};
struct Info {uint64_t magic=Magic,boot=0,session=0;uint32_t version=Version,bytes=sizeof(Info);};
inline bool Empty(const void* data,uint32_t bytes) noexcept {const auto* p=static_cast<const uint8_t*>(data);for(uint32_t i=0;i<bytes;++i)if(p[i])return false;return true;}
inline bool RectEmpty(const Rect& r) noexcept {return !r.left&&!r.top&&!r.right&&!r.bottom;}
inline bool Title(const uint16_t* text) noexcept {bool end=false;for(uint32_t i=0;i<64;++i){if(!text[i])end=true;else if(end||text[i]<32||text[i]>126)return false;}return end;}
inline bool Valid(const Command& c) noexcept {
    if(c.flags||c.reserved||c.color>0xFFFFFF||!Title(c.title))return false;
    const bool titleEmpty=Empty(c.title,sizeof(c.title));
    switch(c.op){
    case Op::Window:return !c.target&&!c.object&&!c.color&&c.rect.left>=-4096&&c.rect.left<=4096&&c.rect.top>=-4096&&c.rect.top<=4096&&int64_t(c.rect.right)-c.rect.left>=16&&int64_t(c.rect.right)-c.rect.left<=1280&&int64_t(c.rect.bottom)-c.rect.top>=16&&int64_t(c.rect.bottom)-c.rect.top<=720;
    case Op::Brush:return !c.target&&!c.object&&RectEmpty(c.rect)&&titleEmpty;
    case Op::Dc:case Op::ReleaseDc:case Op::DeleteBrush:case Op::DestroyWindow:return c.target&&!c.object&&!c.color&&RectEmpty(c.rect)&&titleEmpty;
    case Op::Fill:return c.target&&c.object&&!c.color&&titleEmpty&&c.rect.left>=0&&c.rect.top>=0&&c.rect.right>c.rect.left&&c.rect.bottom>c.rect.top&&c.rect.right<=1280&&c.rect.bottom<=720;
    case Op::Pixel:return c.target&&!c.object&&!c.color&&titleEmpty&&c.rect.left>=0&&c.rect.left<1280&&c.rect.top>=0&&c.rect.top<720&&c.rect.right==c.rect.left+1&&c.rect.bottom==c.rect.top+1;
    case Op::Hello:case Op::Finish:return !c.target&&!c.object&&!c.color&&RectEmpty(c.rect)&&titleEmpty;
    default:return false;
    }
}
static_assert(sizeof(Command)==176&&sizeof(Reply)==72&&sizeof(Packet)==296&&sizeof(Call)==208&&sizeof(Info)==32);
}
