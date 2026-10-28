#pragma once
#include "SoftwareGraphics.h"
namespace guardian::audio {
namespace g=nativegdi;
inline constexpr uint64_t Magic=0x374455414E4741ull;
inline constexpr uint32_t Version=7,MaxStreams=4,MaxBuffers=32,MaxBuffer=256u<<10,MaxBytes=1u<<20;
enum class Op:uint32_t {Enumerate=300,Open,Allocate,Upload,Submit,Poll,Position,Pause,Start,Reset,Free,Close,Stats};
inline bool Extended(g::Op op){return uint32_t(op)>=300&&uint32_t(op)<=uint32_t(Op::Stats);}
inline bool Format(uint32_t rate,uint32_t channels,uint32_t bits){return (rate==22050||rate==44100||rate==48000)&&(channels==1||channels==2)&&bits==16;}
inline bool Valid(const g::Command& c){
    if(!Extended(c.op))return softwaregraphics::Valid(c);
    g::Command v;v.op=c.op;
    switch(static_cast<Op>(c.op)){
    case Op::Enumerate:case Op::Stats:break;
    case Op::Open:for(unsigned i=0;i<4;++i)v.args[i]=c.args[i];v.flags=c.flags;if(!Format(c.args[0],c.args[1],c.args[2])||c.args[3]<20||c.args[3]>500||c.flags>2)return false;break;
    case Op::Allocate:v.dc=c.dc;v.count=c.count;if(!c.count||c.count>MaxBuffer)return false;break;
    case Op::Upload:v.dc=c.dc;v.offset=c.offset;v.count=c.count;if(!c.count||c.count>sizeof(c.pixels)||c.offset>MaxBuffer-c.count)return false;for(unsigned i=0;i<g::Chunk;++i)v.pixels[i]=c.pixels[i];if(!g::Empty(reinterpret_cast<const uint8_t*>(c.pixels)+c.count,sizeof(c.pixels)-c.count))return false;break;
    case Op::Submit:v.dc=c.dc;v.object=c.object;break;
    case Op::Poll:case Op::Position:case Op::Pause:case Op::Start:case Op::Reset:case Op::Free:case Op::Close:v.dc=c.dc;break;
    default:return false;
    }
    const auto* a=reinterpret_cast<const uint8_t*>(&c);const auto* b=reinterpret_cast<const uint8_t*>(&v);for(unsigned i=0;i<sizeof(c);++i)if(a[i]!=b[i])return false;return true;
}
}
