#pragma once
#include "NativeControls.h"
namespace guardian::expandedgdi {
namespace g=nativegdi;
inline constexpr uint64_t Magic=0x354944474E4741ull;
inline constexpr uint32_t Version=5,MaxPoints=32,MaxPathPoints=128,MaxRegionBytes=65536;
enum class Op:uint32_t {
    Ellipse=100,RoundRect,Arc,Chord,Pie,Polyline,Polygon,Bezier,PolylineTo,BezierTo,PolyPolygon,
    Hatch,StyledPen,SetState,GetState,SetPoint,GetPoint,World,MapPoints,
    EllipticRegion,RoundRegion,Combine,OffsetRegion,RegionBox,PointRegion,RectRegion,SetRegion,
    ClipEx,Exclude,OffsetClip,ClipBox,Path,GetPath,Raster,SetPixel,ExtText,ExtentEx,Glyphs,Widths,Describe,GetColor,Justify,MetricsAnsi
};
enum class State:uint32_t {Rop2=1,PolyFill,StretchMode,Graphics,MapMode,TextAlign,CharExtra,ArcDirection,DcPen,DcBrush};
enum class PointState:uint32_t {WindowOrg=1,ViewportOrg,WindowExt,ViewportExt,BrushOrg,Current};
inline bool Extended(g::Op op){return uint32_t(op)>=100&&uint32_t(op)<=uint32_t(Op::MetricsAnsi);}
inline bool Coord(int32_t n){return n>=-4096&&n<=4096;}
inline bool Rop(uint32_t rop){switch(rop){case 0x00CC0020:case 0x00EE0086:case 0x008800C6:case 0x00660046:case 0x00440328:case 0x00330008:case 0x001100A6:case 0x00C000CA:case 0x00BB0226:case 0x00F00021:case 0x00FB0A09:case 0x005A0049:case 0x00550009:case 0x00000042:case 0x00FF0062:return true;default:return false;}}
inline bool DestinationRop(uint32_t rop){return rop==0x00F00021||rop==0x005A0049||rop==0x00550009||rop==0x00000042||rop==0x00FF0062;}
inline bool FiniteWord(int32_t word){return (uint32_t(word)&0x7F800000u)!=0x7F800000u;}
inline bool Valid(const g::Command& c){
    if(!Extended(c.op))return g::Valid(c);
    g::Command v;v.op=c.op;
    auto rect=[&](){v.rect=c.rect;return g::Rect(c.rect);};
    auto point=[&](){v.args[0]=c.args[0];v.args[1]=c.args[1];return Coord(c.args[0])&&Coord(c.args[1]);};
    auto points=[&](){v.count=c.count;if(!c.count||c.count>MaxPoints)return false;for(uint32_t i=0;i<c.count*2;++i){if(!Coord(int32_t(c.pixels[i])))return false;v.pixels[i]=c.pixels[i];}return true;};
    switch(static_cast<Op>(c.op)){
    case Op::Ellipse:v.dc=c.dc;if(!rect())return false;break;
    case Op::RoundRect:v.dc=c.dc;if(!rect()||!point()||c.args[0]<0||c.args[1]<0)return false;break;
    case Op::Arc:case Op::Chord:case Op::Pie:v.dc=c.dc;if(!rect())return false;for(unsigned i=0;i<4;++i){v.args[i]=c.args[i];if(!Coord(c.args[i]))return false;}break;
    case Op::Polyline:case Op::Polygon:case Op::Bezier:case Op::PolylineTo:case Op::BezierTo:case Op::PolyPolygon:
        v.dc=c.dc;if(!points())return false;
        if(static_cast<Op>(c.op)==Op::Bezier&&(c.count<4||(c.count-1)%3))return false;
        if(static_cast<Op>(c.op)==Op::BezierTo&&c.count%3)return false;
        if(static_cast<Op>(c.op)==Op::PolyPolygon){v.flags=c.flags;if(!c.flags||c.flags>8)return false;uint32_t sum=0;for(uint32_t i=0;i<c.flags;++i){v.args[i]=c.args[i];if(c.args[i]<2||c.args[i]>int(MaxPoints))return false;sum+=uint32_t(c.args[i]);}if(sum!=c.count)return false;}break;
    case Op::Hatch:v.flags=c.flags;v.color=c.color;if(c.flags>5||c.color>0xFFFFFF)return false;break;
    case Op::StyledPen:v.flags=c.flags;v.color=c.color;v.args[0]=c.args[0];if(c.flags>6||c.args[0]<0||c.args[0]>16||c.color>0xFFFFFF)return false;break;
    case Op::SetState:case Op::GetState:
        v.dc=c.dc;v.flags=c.flags;if(c.flags<1||c.flags>10)return false;
        if(static_cast<Op>(c.op)==Op::SetState){v.args[0]=c.args[0];if(c.flags>=9){if(uint32_t(c.args[0])>0xFFFFFF)return false;}
            else if(c.flags==7){if(c.args[0]<-256||c.args[0]>256)return false;}
            else if(c.flags==6){if(uint32_t(c.args[0])&~0x11Fu)return false;}
            else if(c.args[0]<1||c.args[0]>16)return false;}break;
    case Op::SetPoint:case Op::GetPoint:
        v.dc=c.dc;v.flags=c.flags;if(c.flags<1||c.flags>6)return false;
        if(static_cast<Op>(c.op)==Op::SetPoint){if(!point()||c.flags==6)return false;if((c.flags==3||c.flags==4)&&(!c.args[0]||!c.args[1]))return false;}break;
    case Op::World:v.dc=c.dc;v.flags=c.flags;if(c.flags>2)return false;if(c.flags){v.mode=c.mode;if(c.flags==1&&c.mode||c.flags==2&&(c.mode<1||c.mode>3))return false;for(unsigned i=0;i<6;++i){v.args[i]=c.args[i];if(!FiniteWord(c.args[i]))return false;}}break;
    case Op::MapPoints:v.dc=c.dc;v.flags=c.flags;if(c.flags>1||!points())return false;break;
    case Op::EllipticRegion:if(!rect())return false;break;
    case Op::RoundRegion:if(!rect()||!point()||c.args[0]<0||c.args[1]<0)return false;break;
    case Op::Combine:v.dc=c.dc;v.object=c.object;v.other=c.other;v.flags=c.flags;if(c.flags<1||c.flags>5||c.flags==5&&c.other)return false;break;
    case Op::OffsetRegion:case Op::PointRegion:v.dc=c.dc;if(!point())return false;break;
    case Op::MetricsAnsi:case Op::RegionBox:case Op::ClipBox:case Op::GetPath:case Op::Describe:v.dc=c.dc;break;
    case Op::RectRegion:case Op::SetRegion:v.dc=c.dc;if(!rect())return false;break;
    case Op::ClipEx:v.dc=c.dc;v.object=c.object;v.flags=c.flags;if(c.flags<1||c.flags>5||!c.object&&c.flags!=5)return false;break;
    case Op::Exclude:v.dc=c.dc;if(!rect())return false;break;
    case Op::OffsetClip:v.dc=c.dc;if(!point())return false;break;
    case Op::Path:v.dc=c.dc;v.flags=c.flags;v.mode=c.mode;if(c.flags<1||c.flags>11||c.flags!=11&&c.mode||c.flags==11&&(c.mode<1||c.mode>5))return false;break;
    case Op::Raster:v.dc=c.dc;v.other=c.other;v.flags=c.flags;v.color=c.color;if(c.flags>2||!Rop(c.color)||!rect()||c.rect.left==c.rect.right||c.rect.top==c.rect.bottom)return false;
        if(c.flags<2){v.source=c.source;if(!g::Rect(c.source)||c.source.left==c.source.right||c.source.top==c.source.bottom)return false;}
        else if(c.other||!DestinationRop(c.color))return false;break;
    case Op::SetPixel:v.dc=c.dc;v.color=c.color;if(c.color>0xFFFFFF||!point())return false;break;
    case Op::ExtText:v.dc=c.dc;v.flags=c.flags;v.mode=c.mode;v.count=c.count;if(c.flags&~0x2096u||c.mode>3||c.count>64||c.flags&0x2000u&&!(c.mode&2)||!point())return false;
        if(c.mode&1){if(!rect())return false;}else if(c.flags&6)return false;
        for(uint32_t i=0;i<c.count;++i)v.text[i]=c.text[i];
        if(!(c.flags&0x10u)&&!g::Text(c.text,c.count))return false;
        if(c.mode&2)for(uint32_t i=0;i<c.count*((c.flags&0x2000u)?2:1);++i){if(int32_t(c.pixels[i])<-256||int32_t(c.pixels[i])>256)return false;v.pixels[i]=c.pixels[i];}break;
    case Op::ExtentEx:case Op::Glyphs:v.dc=c.dc;v.count=c.count;for(unsigned i=0;i<128;++i)v.text[i]=c.text[i];if(!g::Text(c.text,c.count))return false;
        if(static_cast<Op>(c.op)==Op::Glyphs){v.flags=c.flags;if(c.flags>1)return false;}else{v.args[0]=c.args[0];if(c.args[0]<0||c.args[0]>65536)return false;}break;
    case Op::Widths:v.dc=c.dc;v.flags=c.flags;v.args[0]=c.args[0];v.args[1]=c.args[1];if(c.flags>1||c.args[0]<0||c.args[1]<c.args[0]||c.args[1]-c.args[0]>=32||c.args[1]>65535)return false;break;
    case Op::GetColor:v.dc=c.dc;v.flags=c.flags;if(c.flags>1)return false;break;
    case Op::Justify:v.dc=c.dc;if(!point()||c.args[0]<-256||c.args[0]>256||c.args[1]<0||c.args[1]>127)return false;break;
    default:return false;
    }
    const auto* a=reinterpret_cast<const uint8_t*>(&c);const auto* b=reinterpret_cast<const uint8_t*>(&v);for(unsigned i=0;i<sizeof(c);++i)if(a[i]!=b[i])return false;return true;
}
static_assert(sizeof(g::Packet)<4096&&sizeof(g::Reply)==sizeof(nativegdi::Reply));
}

