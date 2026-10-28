#pragma once
#include "NativeGdi.h"
namespace guardian::nativecontrols {
namespace u=nativeuser;
#ifdef GUARDIAN_GDI_EXPANDED
inline constexpr uint64_t Magic=0x354944474E4741ull;
inline constexpr uint32_t Version=5;
#else
inline constexpr uint64_t Magic=0x344C5254434E4741ull;
inline constexpr uint32_t Version=4;
#endif
inline constexpr uint32_t MaxMenus=8,MaxItems=32,MaxIcons=32;
inline constexpr u::Class List=static_cast<u::Class>(5),Combo=static_cast<u::Class>(6);
enum class Op:uint32_t {Menu=100,Append,Attach,DeleteMenu,ControlText,Metrics,Foreground,Icon,DeleteIcon,Dark,SysColor,Unsupported,SysBrush,SetIcon
#ifdef GUARDIAN_GDI_EXPANDED
,Cursor,Redraw
#endif
};
inline bool Text(const uint16_t* text){uint16_t normal[256]{};for(unsigned i=0;i<256;++i)normal[i]=(text[i]==9||text[i]==10||text[i]==13)?uint16_t(' '):text[i];return u::Text(normal);}
inline bool Extended(u::Op op){return uint32_t(op)>=100&&uint32_t(op)<=
#ifdef GUARDIAN_GDI_EXPANDED
115
#else
113
#endif
;}
inline bool Valid(const u::Command& c){
    if(c.op==u::Op::Create){auto base=c;base.kind=u::Class::Custom;base.wp=base.lp=0;base.exStyle=0;
#ifdef GUARDIAN_GDI_EXPANDED
base.reserved=0;if(c.reserved&~0xBu)return false;
#endif
if(c.exStyle&~0x389u)return false;if(!Text(c.text))return false;for(auto& ch:base.text)if(ch==9||ch==10||ch==13)ch=32;if(!u::Valid(base)||uint32_t(c.kind)<1||uint32_t(c.kind)>6||c.wp>65535)return false;return true;}
    if(!Extended(c.op)){if(c.op==u::Op::Text){auto base=c;if(!Text(c.text))return false;for(auto& ch:base.text)if(ch==9||ch==10||ch==13)ch=32;return u::Valid(base);}return u::Valid(c);}
    u::Command v;v.op=c.op;
    switch(static_cast<Op>(c.op)){
    case Op::Menu:v.flags=c.flags;if(c.flags>1)return false;break;
    case Op::Append:v.window=c.window;v.other=c.other;v.wp=c.wp;v.flags=c.flags;for(unsigned i=0;i<256;++i)v.text[i]=c.text[i];if(c.wp>65535||c.flags&~0x81Bu||!u::Text(c.text))return false;break;
    case Op::Attach:v.window=c.window;v.other=c.other;break;
    case Op::DeleteMenu:case Op::DeleteIcon:case Op::Foreground:v.window=c.window;break;
    case Op::ControlText:v.window=c.window;v.message=c.message;v.wp=c.wp;for(unsigned i=0;i<256;++i)v.text[i]=c.text[i];if(c.wp>32&&c.wp!=uint64_t(-1)||!u::Text(c.text))return false;break;
    case Op::Unsupported:v.wp=c.wp;v.lp=c.lp;v.message=c.message;v.rect=c.rect;if(c.message>16)return false;break;
    case Op::SysBrush:case Op::SysColor:v.wp=c.wp;if(c.wp>31)return false;break;
    case Op::Metrics:v.wp=c.wp;if(c.wp>97)return false;break;
    case Op::Icon:v.wp=c.wp;v.other=c.other;if(c.wp&&(c.wp<32512||c.wp>32518)||bool(c.wp)==bool(c.other))return false;break;
    case Op::SetIcon:v.window=c.window;v.other=c.other;v.wp=c.wp;v.flags=c.flags;if(c.wp>2||c.flags>1||!c.flags&&c.wp>1||c.flags&&c.other)return false;break;
#ifdef GUARDIAN_GDI_EXPANDED
    case Op::Redraw:v.window=c.window;v.flags=c.flags;if(c.flags>4)return false;if(c.flags&1){v.rect=c.rect;if(!nativegdi::Rect(c.rect))return false;}break;
    case Op::Cursor:v.wp=c.wp;v.lp=c.lp;if(c.wp!=0&&c.wp!=32512&&c.wp!=32513&&c.wp!=32514&&c.wp!=32515&&c.wp!=32646&&c.wp!=32648&&c.wp!=32649&&c.wp!=32650||int64_t(c.lp)<-64||int64_t(c.lp)>64)return false;break;
#endif
    case Op::Dark:v.window=c.window;v.flags=c.flags;if(c.flags>1)return false;break;
    default:return false;
    }
    const auto* a=reinterpret_cast<const uint8_t*>(&c);const auto* b=reinterpret_cast<const uint8_t*>(&v);for(unsigned i=0;i<sizeof(c);++i)if(a[i]!=b[i])return false;return true;
}
}
