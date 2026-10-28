#pragma once
#include "ExpandedGdi.h"
namespace guardian::softwaregraphics {
namespace g=nativegdi;
inline constexpr uint64_t Magic=0x365746534E4741ull;
inline constexpr uint32_t Version=6,MaxObjects=64,MaxEncoded=1u<<20,MaxBytes=16u<<20,MaxDimension=512;
enum class Op:uint32_t {Stats=200,Free,Target,Begin,End,Clear,Brush,Rectangle,Line,Ellipse,Transform,Clip,PopClip,Format,FormatState,Text,Bitmap,DrawBitmap,
    Stream,Write,Decode,Convert,Read,SourceInfo,Device,Texture,View,ClearView,Copy,Present,Flush,Query,Signal,Poll,Bind,Viewport,ClearState,Status,Rebind,Unsupported};
enum class Kind:uint32_t {Target=1,Brush,Format,Bitmap,Stream,Image,Device,Texture,View,Query};
inline bool Extended(g::Op op){return uint32_t(op)>=200&&uint32_t(op)<=uint32_t(Op::Unsupported);}
inline uint32_t FloatBound(int32_t value){if(!value)return 0;const auto magnitude=value<0?uint32_t(-int64_t(value)):uint32_t(value);uint32_t exponent=0;while(exponent<31&&(magnitude>>(exponent+1)))++exponent;const auto fraction=(exponent<=23?magnitude<<(23-exponent):magnitude>>(exponent-23))&0x7FFFFFu;return (value<0?0x80000000u:0u)|((exponent+127)<<23)|fraction;}
inline uint32_t Ordered(uint32_t word){if(!(word&0x7FFFFFFFu))word=0;return word&0x80000000u?~word:word^0x80000000u;}
// Validate IEEE-754 words with integer operations in both the guest kernel and
// helper. Guest-controlled floating-point exception modes cannot affect this.
inline bool Float(int32_t word,int32_t low,int32_t high){const auto bits=uint32_t(word);return (bits&0x7F800000u)!=0x7F800000u&&Ordered(bits)>=Ordered(FloatBound(low))&&Ordered(bits)<=Ordered(FloatBound(high));}
inline bool Valid(const g::Command& c){
    if(!Extended(c.op))return expandedgdi::Valid(c);
    g::Command v;v.op=c.op;
    auto scalar=[&](unsigned i,int32_t low,int32_t high){v.args[i]=c.args[i];return Float(c.args[i],low,high);};
    auto color=[&](){for(unsigned i=0;i<4;++i)if(!scalar(i,0,1))return false;return true;};
    auto box=[&](){return scalar(0,-4096,4096)&&scalar(1,-4096,4096)&&scalar(2,-4096,4096)&&scalar(3,-4096,4096);};
    switch(static_cast<Op>(c.op)){
    case Op::Stats:break;
    case Op::Free:case Op::Begin:case Op::End:case Op::PopClip:case Op::SourceInfo:case Op::Flush:case Op::Query:case Op::Signal:case Op::Poll:case Op::ClearState:case Op::Status:v.dc=c.dc;break;
    case Op::Bind:v.dc=c.dc;v.object=c.object;break;
    case Op::Rebind:v.dc=c.dc;v.object=c.object;v.rect=c.rect;if(c.rect.left||c.rect.top||c.rect.right<1||c.rect.bottom<1||c.rect.right>1280||c.rect.bottom>720)return false;break;
    case Op::Viewport:v.dc=c.dc;for(unsigned i=0;i<4;++i)if(!scalar(i,0,512))return false;for(unsigned i=4;i<6;++i)if(!scalar(i,0,1))return false;break;
    case Op::Target:v.dc=c.dc;v.flags=c.flags;if(c.flags>1)return false;if(c.flags){v.rect=c.rect;if(c.rect.left||c.rect.top||c.rect.right<1||c.rect.bottom<1||c.rect.right>1280||c.rect.bottom>720)return false;}break;
    case Op::Clear:case Op::Brush:case Op::ClearView:v.dc=c.dc;if(!color())return false;break;
    case Op::Rectangle:case Op::Line:case Op::Ellipse:v.dc=c.dc;v.object=c.object;v.flags=c.flags;if(c.flags>1||!box()||!scalar(4,0,32))return false;break;
    case Op::Transform:v.dc=c.dc;for(unsigned i=0;i<6;++i)if(!scalar(i,-4096,4096))return false;break;
    case Op::Clip:v.dc=c.dc;v.flags=c.flags;if(c.flags>1||!box())return false;break;
    case Op::Format:v.flags=c.flags;v.color=c.color;v.mode=c.mode;
        if(c.flags<1||c.flags>999||c.color>2||c.mode<1||c.mode>9||!scalar(0,1,96))return false;
        for(unsigned i=0;i<32;++i)v.font.face[i]=c.font.face[i];if(c.font.face[31])return false;
        v.count=c.count;for(unsigned i=0;i<128;++i)v.text[i]=c.text[i];if(c.count>31||!g::Text(c.text,c.count))return false;break;
    case Op::FormatState:v.dc=c.dc;v.flags=c.flags;v.mode=c.mode;if(c.flags>2||c.mode>3)return false;break;
    case Op::Text:v.dc=c.dc;v.object=c.object;v.other=c.other;v.count=c.count;v.flags=c.flags;for(unsigned i=0;i<128;++i)v.text[i]=c.text[i];if(c.flags&~3u||!box()||!g::Text(c.text,c.count))return false;break;
    case Op::Bitmap:case Op::Copy:v.dc=c.dc;v.object=c.object;break;
    case Op::DrawBitmap:v.dc=c.dc;v.object=c.object;v.flags=c.flags;if(c.flags>1||!box()||!scalar(4,0,1))return false;break;
    case Op::Stream:v.count=c.count;if(!c.count||c.count>MaxEncoded)return false;break;
    case Op::Write:v.dc=c.dc;v.offset=c.offset;v.count=c.count;if(!c.count||c.count>512||c.offset>MaxEncoded-c.count)return false;for(unsigned i=0;i<(c.count+3)/4;++i)v.pixels[i]=c.pixels[i];if(c.count%4&&(c.pixels[c.count/4]>>(8*(c.count%4))))return false;break;
    case Op::Decode:v.dc=c.dc;break;
    case Op::Convert:v.dc=c.dc;v.flags=c.flags;if(c.flags<1||c.flags>2)return false;break;
    case Op::Read:v.dc=c.dc;v.offset=c.offset;v.count=c.count;if(!c.count||c.count>512||c.offset>MaxEncoded-c.count)return false;break;
    case Op::Device:break;
    case Op::Texture:v.dc=c.dc;v.flags=c.flags;v.mode=c.mode;v.args[0]=c.args[0];v.args[1]=c.args[1];v.color=c.color;
        if(c.args[0]<1||c.args[1]<1||c.args[0]>int(MaxDimension)||c.args[1]>int(MaxDimension)||(c.color!=28&&c.color!=87)||c.flags>1||c.mode>1||c.flags&&c.mode)return false;break;
    case Op::View:v.dc=c.dc;break;
    case Op::Present:v.dc=c.dc;v.object=c.object;break;
    case Op::Unsupported:v.flags=c.flags;v.mode=c.mode;if(c.flags>32||c.mode>256)return false;break;
    default:return false;
    }
    const auto* a=reinterpret_cast<const uint8_t*>(&c);const auto* b=reinterpret_cast<const uint8_t*>(&v);for(unsigned i=0;i<sizeof(c);++i)if(a[i]!=b[i])return false;return true;
}
static_assert(sizeof(g::Packet)<4096);
}
