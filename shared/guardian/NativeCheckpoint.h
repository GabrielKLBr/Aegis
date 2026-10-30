#pragma once
#include "NativeUser32.h"
// Supervisor-only transport. These operations are deliberately absent from
// nativeuser::Valid and cannot be requested through the VM's broker exchange.
namespace guardian::checkpoint {
inline constexpr uint32_t Begin=0x1310,Read=0x1311,Load=0x1312,Write=0x1313,Commit=0x1314;
struct Window {nativeuser::Window window{};uint32_t control=0,check=0,selection=0;uint16_t text[256]{};};
struct User {uint64_t magic=0x315043554E4741ull,boot=0,session=0,sequence=0,next=1,nextCookie=1,nextEvent=1,focus=0;
    uint32_t version=1,bytes=sizeof(User),windows=0,events=0,calls[8]{};
    Window window[nativeuser::MaxWindows]{};nativeuser::Event event[nativeuser::MaxEvents]{};};
inline bool Valid(const User& s) noexcept {
    if(s.magic!=0x315043554E4741ull||s.version!=1||s.bytes!=sizeof(s)||!s.boot||!s.session||s.sequence>nativeuser::MaxCommands||s.windows>nativeuser::MaxWindows||s.events>nativeuser::MaxEvents||!s.next||s.next>nativeuser::MaxCommands+1||!s.nextCookie||!s.nextEvent)return false;
    for(auto c:s.calls)if(c>4*nativeuser::MaxCommands)return false;
    if(s.calls[1]>s.calls[0]||s.calls[0]-s.calls[1]!=s.windows)return false;
    for(uint32_t i=0;i<s.windows;++i){const auto& v=s.window[i];const auto& w=v.window;
        if(!w.id||w.id!=((uint64_t(1)<<56)|((s.session&0xFFFFFF)<<32)|uint32_t(w.id))||uint32_t(w.id)>=s.next||!w.pid||!w.tid||w.enabled>1||w.visible>1||w.dpi!=96||!nativeuser::Geometry(w.bounds)||!nativeuser::Text(v.text)||v.control>65535||v.check>2||uint32_t(w.kind)<1||uint32_t(w.kind)>4)return false;
        if(w.parent){bool parent=false;for(uint32_t j=0;j<i;++j)if(s.window[j].window.id==w.parent&&s.window[j].window.pid==w.pid&&s.window[j].window.tid==w.tid)parent=true;if(!parent)return false;}
        for(uint32_t j=0;j<i;++j)if(w.id==s.window[j].window.id)return false;
    }
    if(s.focus){bool found=false;for(uint32_t i=0;i<s.windows;++i)if(s.window[i].window.id==s.focus)found=true;if(!found)return false;}
    for(uint32_t i=0;i<s.events;++i){const auto& e=s.event[i];if(e.cookie||e.flags&~7u||e.reserved||!e.serial||e.serial>=s.nextEvent)return false;bool found=false;for(uint32_t j=0;j<s.windows;++j)if(e.window==s.window[j].window.id&&e.tid==s.window[j].window.tid)found=true;if(!found)return false;}
    return true;
}
}
