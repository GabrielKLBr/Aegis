#include "guardian/NativeUser32.h"
#include "NativeGuiRestrictions.h"
#include "NativeGuiPipe.h"
#ifdef GUARDIAN_NATIVE_GDI
#include "NativeGdiEngine.h"
namespace g=guardian::nativegdi;
using WirePacket=g::Packet;
constexpr auto WireMagic=g::Magic;constexpr uint32_t WireVersion=g::Version;
#ifdef GUARDIAN_NATIVE_CONTROLS
#include "guardian/NativeControls.h"
namespace nc=guardian::nativecontrols;
#endif
#else
using WirePacket=guardian::nativeuser::Packet;
constexpr auto WireMagic=guardian::nativeuser::Magic;constexpr uint32_t WireVersion=2;
#endif
#include <Windows.h>
#ifdef GUARDIAN_NATIVE_CONTROLS
#include <dwmapi.h>
#endif
#include <windowsx.h>
#include <array>
#include <deque>
#include <thread>
#include <atomic>
#include <string>
#include <algorithm>
#include <cstring>
namespace u=guardian::nativeuser;
namespace {
#ifdef GUARDIAN_AUDIO
constexpr wchar_t ClassName[]=L"Aegis.Audio.14.10G",Prefix[]=L"[MiniNT audio 14.10G] ";
constexpr const wchar_t* ExpandedClasses[]{ClassName,L"Aegis.Audio.1",L"Aegis.Audio.2",L"Aegis.Audio.3",L"Aegis.Audio.4",L"Aegis.Audio.5",L"Aegis.Audio.6",L"Aegis.Audio.7"};
#elif defined(GUARDIAN_SOFTWARE_GRAPHICS)
constexpr wchar_t ClassName[]=L"Aegis.SoftwareGraphics.14.10F",Prefix[]=L"[MiniNT software 14.10F] ";
constexpr const wchar_t* ExpandedClasses[]{ClassName,L"Aegis.SoftwareGraphics.14.10F.1",L"Aegis.SoftwareGraphics.14.10F.2",L"Aegis.SoftwareGraphics.14.10F.3",L"Aegis.SoftwareGraphics.14.10F.4",L"Aegis.SoftwareGraphics.14.10F.5",L"Aegis.SoftwareGraphics.14.10F.6",L"Aegis.SoftwareGraphics.14.10F.7"};
#elif defined(GUARDIAN_GDI_EXPANDED)
constexpr wchar_t ClassName[]=L"Aegis.ExpandedGdi.14.10E",Prefix[]=L"[MiniNT native 14.10E] ";
constexpr const wchar_t* ExpandedClasses[]{ClassName,L"Aegis.ExpandedGdi.14.10E.1",L"Aegis.ExpandedGdi.14.10E.2",L"Aegis.ExpandedGdi.14.10E.3",L"Aegis.ExpandedGdi.14.10E.4",L"Aegis.ExpandedGdi.14.10E.5",L"Aegis.ExpandedGdi.14.10E.6",L"Aegis.ExpandedGdi.14.10E.7"};
#elif defined(GUARDIAN_NATIVE_CONTROLS)
constexpr wchar_t ClassName[]=L"Aegis.NativeControls.14.10D",Prefix[]=L"[MiniNT native 14.10D] ";
#elif defined(GUARDIAN_NATIVE_GDI)
constexpr wchar_t ClassName[]=L"Aegis.NativeGdi.14.10C",Prefix[]=L"[MiniNT native 14.10C] ";
#else
constexpr wchar_t ClassName[]=L"Aegis.NativeUser32.14.10B",Prefix[]=L"[MiniNT native 14.10B] ";
#endif
struct Host;
struct Object {Host* host=nullptr;HWND hwnd=nullptr;uint64_t id=0,parent=0;uint32_t pid=0,tid=0,control=0;u::Class kind{};bool ready=false;WNDPROC previous=nullptr;uint64_t icon=0,smallIcon=0,font=0;
#ifdef GUARDIAN_GDI_EXPANDED
bool paintOutstanding=false,redrawOnSize=false;
#endif
};
struct Pending {uint64_t id=0,result=0,window=0;uint32_t sender=0,receiver=0,pid=0,status=ERROR_IO_PENDING;ULONGLONG until=0;};
struct Timer {UINT_PTR native=0;uint64_t window=0,id=0;};
struct Host {
    std::array<Object,u::MaxWindows> objects{};std::array<Pending,u::MaxPending> pending{};std::array<Timer,u::MaxTimers> timers{};
    std::deque<u::Event> events;uint64_t boot=0,session=0,sequence=0,next=1,nextCookie=1,nextEvent=1;bool complete=false,overflow=false;uint32_t calls[8]{};
    HWND channel=nullptr;HANDLE done=nullptr;UINT_PTR nextTimer=1;WirePacket packet{};
#ifdef GUARDIAN_NATIVE_GDI
    av::microvm::native_gui::GdiEngine graphics;
#endif
    Object* Find(uint64_t id){for(auto& o:objects)if(id&&o.id==id)return &o;return nullptr;}
    Object* Find(HWND h){for(auto& o:objects)if(h&&o.hwnd==h)return &o;return nullptr;}
    static int Logical(int value,UINT dpi){return MulDiv(value,96,int(dpi));}
    static int Physical(int value,UINT dpi){return MulDiv(value,int(dpi),96);}
    void Queue(Object& o,UINT msg,WPARAM wp=0,LPARAM lp=0,uint32_t flags=2,const WINDOWPOS* pos=nullptr){
        if(!o.ready)return;
#ifdef GUARDIAN_GDI_EXPANDED
        // Native dragging can generate hundreds of geometry notifications while
        // the guest is busy. Retain the newest state for each notification type.
        // Explicitly posted messages and correlated send requests keep order.
        if(!flags&&(msg==WM_WINDOWPOSCHANGED||msg==WM_MOVE||msg==WM_SIZE))
            events.erase(std::remove_if(events.begin(),events.end(),[&](const auto& e){return e.window==o.id&&e.message==msg&&!(e.flags&3)&&!e.cookie;}),events.end());
#endif
        if((msg==WM_PAINT||msg==WM_MOUSEMOVE||msg==WM_TIMER)&&std::any_of(events.begin(),events.end(),[&](const auto& e){return e.window==o.id&&e.message==msg&&e.wp==wp;}))return;
#ifdef GUARDIAN_GDI_EXPANDED
        if(msg==WM_PAINT&&o.paintOutstanding)return;
#endif
        if(events.size()>=u::MaxEvents){overflow=true;return;}
        u::Event e;e.window=o.id;e.message=msg;e.wp=wp;e.lp=uint64_t(lp);e.flags=flags;e.tid=o.tid;e.time=GetTickCount();e.serial=nextEvent++;
        if(pos){const auto dpi=GetDpiForWindow(o.hwnd);e.flags|=4;e.lp=0;e.rect={Logical(pos->x,dpi),Logical(pos->y,dpi),Logical(pos->x+pos->cx,dpi),Logical(pos->y+pos->cy,dpi)};e.positionFlags=pos->flags;}
#ifdef GUARDIAN_GDI_EXPANDED
        if(msg==WM_PAINT)o.paintOutstanding=true;
#endif
        events.push_back(e);
    }
    static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
#ifdef GUARDIAN_NATIVE_CONTROLS
            // Guest geometry is bounded explicitly; native default minimums must not enlarge it.

            if(m==WM_WINDOWPOSCHANGING)return 0;
            if(m==WM_GETMINMAXINFO&&l){auto* info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={1,1};return 0;}
#endif
        auto* o=reinterpret_cast<Object*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){o=static_cast<Object*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(o));o->hwnd=h;}
        if(o&&o->host){auto& host=*o->host;
#ifdef GUARDIAN_GDI_EXPANDED
            if(m==WM_SETCURSOR&&LOWORD(l)==HTCLIENT){auto* cursor=o->host->CursorFor(o->pid,o->tid,false);SetCursor(cursor&&(cursor->visible<0||!cursor->resource)?nullptr:LoadCursorW(nullptr,MAKEINTRESOURCEW(WORD(cursor?cursor->resource:32512))));return TRUE;}
#endif

            if(m==WM_COMMAND){auto* child=host.Find(reinterpret_cast<HWND>(l));if(child&&child->parent==o->id)host.Queue(*o,m,MAKEWPARAM(child->control,HIWORD(w)),LPARAM(child->id),0);
#ifdef GUARDIAN_NATIVE_CONTROLS
                else if(!l&&host.MenuCommand(o->id,LOWORD(w)))host.Queue(*o,m,LOWORD(w),0,0);
#endif
                return 0;}
            if(m==WM_WINDOWPOSCHANGED){host.Queue(*o,m,0,0,0,reinterpret_cast<WINDOWPOS*>(l));return DefWindowProcW(h,m,w,l);}
            if(m==WM_SIZE||m==WM_MOVE){const auto dpi=GetDpiForWindow(h);l=MAKELPARAM(WORD(Logical(GET_X_LPARAM(l),dpi)),WORD(Logical(GET_Y_LPARAM(l),dpi)));host.Queue(*o,m,w,l,0);return 0;}
            #ifdef GUARDIAN_NATIVE_GDI
            if(m==WM_PRINTCLIENT&&w){host.graphics.Print(o->id,h,reinterpret_cast<HDC>(w));return 0;}
#endif
            if(m==WM_PAINT){PAINTSTRUCT ps{};const auto dc=BeginPaint(h,&ps);
#ifdef GUARDIAN_NATIVE_GDI
                host.graphics.Paint(o->id,h,dc,ps.rcPaint);
#else
                FillRect(dc,&ps.rcPaint,GetSysColorBrush(COLOR_WINDOW));
#endif
                EndPaint(h,&ps);host.Queue(*o,m);return 0;}
            if(m==WM_CLOSE){host.Queue(*o,m);return 0;}
            if(m==WM_DPICHANGED){auto* r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
            if(m==WM_SETFOCUS||m==WM_KILLFOCUS){auto* target=host.Find(reinterpret_cast<HWND>(w));host.Queue(*o,m,target?target->id:0,0);return 0;}
            if(m>=WM_MOUSEFIRST&&m<=WM_MOUSELAST){const auto dpi=GetDpiForWindow(h);l=MAKELPARAM(WORD(Logical(GET_X_LPARAM(l),dpi)),WORD(Logical(GET_Y_LPARAM(l),dpi)));host.Queue(*o,m,w,l);return 0;}
            if(m>=WM_KEYFIRST&&m<=WM_KEYLAST){host.Queue(*o,m,WORD(w),LPARAM(uint32_t(l)));return 0;}
        }
        return DefWindowProcW(h,m,w,l);
    }
    static LRESULT CALLBACK Channel(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<Host*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(self));}
        if(self&&m==WM_APP+1){if(!self->Exchange(self->packet))self->packet.reply.status=ERROR_INVALID_DATA;SetEvent(self->done);return 0;}
        if(self&&m==WM_TIMER){for(auto& timer:self->timers)if(timer.native==w){auto* o=self->Find(timer.window);if(o)self->Queue(*o,WM_TIMER,WPARAM(timer.id),0,0);}return 0;}
        return DefWindowProcW(h,m,w,l);
    }
    void Remove(Object& o){const auto id=o.id;o.ready=false;
#ifdef GUARDIAN_NATIVE_CONTROLS
        DetachMenus(id);
#endif
#ifdef GUARDIAN_NATIVE_GDI
        graphics.DropWindow(id);
#endif
        for(auto& child:objects)if(child.id&&child.parent==id)Remove(child);
        for(auto& t:timers)if(t.window==id){KillTimer(channel,t.native);t={};}
        events.erase(std::remove_if(events.begin(),events.end(),[&](const auto& e){return e.window==id;}),events.end());
        for(auto& p:pending)if(p.window==id&&p.status==ERROR_IO_PENDING)p.status=ERROR_INVALID_WINDOW_HANDLE;
        if(o.hwnd)DestroyWindow(o.hwnd);
#ifdef GUARDIAN_NATIVE_CONTROLS
        if(o.font)graphics.UnpinFont(o.font);
#endif
        ++calls[1];o={};
    }
    ~Host(){for(auto& o:objects)if(o.id)Remove(o);
#ifdef GUARDIAN_NATIVE_CONTROLS
        ClearExtra(0,0);
#endif
        if(channel)DestroyWindow(channel);if(done)CloseHandle(done);}
    void Describe(Object& o,u::Window& info){info.id=o.id;info.parent=o.parent;info.pid=o.pid;info.tid=o.tid;info.kind=o.kind;info.style=uint32_t(GetWindowLongPtrW(o.hwnd,GWL_STYLE));info.exStyle=uint32_t(GetWindowLongPtrW(o.hwnd,GWL_EXSTYLE));info.enabled=IsWindowEnabled(o.hwnd);info.visible=IsWindowVisible(o.hwnd);info.dpi=96;
        RECT r{},c{};GetWindowRect(o.hwnd,&r);GetClientRect(o.hwnd,&c);POINT origin{};ClientToScreen(o.hwnd,&origin);const auto dpi=GetDpiForWindow(o.hwnd);info.bounds={Logical(r.left,dpi),Logical(r.top,dpi),Logical(r.right,dpi),Logical(r.bottom,dpi)};info.client={0,0,Logical(c.right,dpi),Logical(c.bottom,dpi)};info.originX=Logical(origin.x,dpi);info.originY=Logical(origin.y,dpi);
    }
#ifdef GUARDIAN_NATIVE_CONTROLS
#include "NativeControlsHost.inl"
#endif
    template<class Packet> uint32_t Execute(Packet& p){const auto& c=p.command;auto& r=p.reply;auto* o=Find(c.window);
        if(c.op==u::Op::Create){
            if(c.window)return ERROR_INVALID_PARAMETER;Object* parent=c.other?Find(c.other):nullptr;
            if(c.other&&(!parent||parent->pid!=p.pid||parent->tid!=p.tid))return ERROR_INVALID_HANDLE;
            if((c.style&WS_CHILD)&&!parent)return ERROR_INVALID_PARAMETER;
            if(c.kind!=u::Class::Custom&&(!(c.style&WS_CHILD)||!parent))return ERROR_NOT_SUPPORTED;
            if((c.kind==u::Class::Button&&(c.style&15)>BS_AUTOCHECKBOX)||(c.kind==u::Class::Static&&(c.style&31)>
#ifdef GUARDIAN_NATIVE_CONTROLS
            SS_ICON
#else
            SS_RIGHT
#endif
            )||(c.kind==u::Class::Edit&&(c.style&(ES_MULTILINE|ES_PASSWORD|ES_OEMCONVERT|ES_WANTRETURN))))return ERROR_NOT_SUPPORTED;
            Object* slot=nullptr;for(auto& entry:objects)if(!entry.id){slot=&entry;break;}if(!slot)return ERROR_NOT_ENOUGH_QUOTA;
            const auto id=(uint64_t(1)<<56)|((session&0xFFFFFF)<<32)|next++;*slot={this,nullptr,id,c.other,p.pid,p.tid,c.id,c.kind,false};
            std::wstring text(reinterpret_cast<const wchar_t*>(c.text));if(!(c.style&WS_CHILD))text=Prefix+text;
            const wchar_t* cls=c.kind==u::Class::Custom?ClassName:c.kind==u::Class::Button?L"BUTTON":c.kind==u::Class::Static?L"STATIC":L"EDIT";
#ifdef GUARDIAN_GDI_EXPANDED
            if(c.kind==u::Class::Custom){cls=ExpandedClasses[(c.reserved&3)|((c.reserved&8)>>1)];slot->redrawOnSize=(c.reserved&3)!=0;}
#endif
#ifdef GUARDIAN_NATIVE_CONTROLS
            if(c.kind==nc::List){if(c.style&0xFFFFu&~(LBS_NOTIFY|LBS_SORT|LBS_NOINTEGRALHEIGHT|LBS_HASSTRINGS))return ERROR_NOT_SUPPORTED;cls=L"LISTBOX";}
            if(c.kind==nc::Combo){if((c.style&3)!=CBS_DROPDOWNLIST||c.style&0xFFFFu&~(CBS_DROPDOWNLIST|CBS_SORT|CBS_HASSTRINGS))return ERROR_NOT_SUPPORTED;cls=L"COMBOBOX";}
            if(c.lp&&!OwnedIcon(c.lp,p.pid,p.tid))return ERROR_INVALID_HANDLE;
#endif
            const auto dpi=parent?GetDpiForWindow(parent->hwnd):GetDpiForSystem();
            slot->hwnd=CreateWindowExW(c.exStyle,cls,text.c_str(),c.style&~WS_VISIBLE,Physical(c.rect.left,dpi),Physical(c.rect.top,dpi),Physical(c.rect.right-c.rect.left,dpi),Physical(c.rect.bottom-c.rect.top,dpi),parent?parent->hwnd:nullptr,(c.style&WS_CHILD)?reinterpret_cast<HMENU>(uintptr_t(c.id)):nullptr,GetModuleHandleW(nullptr),c.kind==u::Class::Custom?slot:nullptr);
            if(!slot->hwnd){*slot={};return ERROR_NOT_ENOUGH_MEMORY;}
#ifdef GUARDIAN_NATIVE_GDI
            if(c.kind==u::Class::Custom){const auto error=graphics.AddWindow(id,slot->hwnd);if(error){DestroyWindow(slot->hwnd);*slot={};return error;}}
#endif
            ++calls[0];slot->ready=true;
#ifdef GUARDIAN_NATIVE_CONTROLS
            if(c.kind!=u::Class::Custom){SetWindowLongPtrW(slot->hwnd,GWLP_USERDATA,LONG_PTR(slot));slot->previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(slot->hwnd,GWLP_WNDPROC,LONG_PTR(ControlProc)));SendMessageW(slot->hwnd,WM_SETFONT,WPARAM(GetStockObject(DEFAULT_GUI_FONT)),TRUE);}
            if(c.lp){auto* icon=OwnedIcon(c.lp,p.pid,p.tid);slot->icon=slot->smallIcon=icon->id;SendMessageW(slot->hwnd,WM_SETICON,ICON_BIG,LPARAM(icon->handle));SendMessageW(slot->hwnd,WM_SETICON,ICON_SMALL,LPARAM(icon->handle));}
#endif
            if(c.kind==u::Class::Edit)SendMessageW(slot->hwnd,EM_SETLIMITTEXT,255,0);
            if(c.style&WS_VISIBLE)ShowWindow(slot->hwnd,SW_SHOWNOACTIVATE);r.value=id;Describe(*slot,r.window);return 0;
        }
        if(c.op==u::Op::Poll){
            for(auto it=events.begin();it!=events.end();++it){auto* owner=Find(it->window);if(!owner||owner->pid!=p.pid||it->tid!=p.tid||(c.window&&c.window!=it->window))continue;if((c.flags&2)&&!(it->flags&1)&&it->message!=WM_COMMAND&&it->message!=WM_WINDOWPOSCHANGED&&it->message!=WM_SIZE&&it->message!=WM_MOVE)continue;if(c.lp&&(it->message<c.wp||it->message>c.lp))continue;r.event=*it;r.value=1;if(c.flags&1){
#ifdef GUARDIAN_GDI_EXPANDED
                // A resize can arrive while a guest paint callback is running.
                // Its earlier native invalidation may be consumed by that old
                // BeginPaint. Repaint after the guest receives the new size.
                if(it->message==WM_SIZE&&it->wp!=SIZE_MINIMIZED&&owner->redrawOnSize)InvalidateRect(owner->hwnd,nullptr,TRUE);
#endif
                events.erase(it);}break;}return 0;
        }
        if(c.op==u::Op::Result||c.op==u::Op::Reply){
            for(auto& entry:pending)if(entry.id==c.window&&entry.pid==p.pid){
                if(c.op==u::Op::Reply){if(entry.receiver!=p.tid||entry.status!=ERROR_IO_PENDING||GetTickCount64()>entry.until)return ERROR_INVALID_HANDLE;entry.result=c.wp;entry.status=0;return 0;}
                if(entry.sender!=p.tid)return ERROR_ACCESS_DENIED;
                if(entry.status==ERROR_IO_PENDING&&GetTickCount64()>entry.until){entry.status=ERROR_TIMEOUT;events.erase(std::remove_if(events.begin(),events.end(),[&](const auto& e){return e.cookie==entry.id;}),events.end());}
                r.value=entry.result;const auto status=entry.status;if(status!=ERROR_IO_PENDING)entry={};return status;
            }return ERROR_INVALID_HANDLE;
        }
        #ifdef GUARDIAN_NATIVE_GDI
        #ifndef GUARDIAN_NATIVE_CONTROLS
        if(c.op==u::Op::ThreadExit)graphics.Exit(p.pid,p.tid);
#endif
        #ifndef GUARDIAN_NATIVE_CONTROLS
        if(c.op==u::Op::Finish&&c.flags)graphics.Clear();
#endif
#endif
        #ifdef GUARDIAN_NATIVE_CONTROLS
        if(nc::Extended(c.op))return Extra(p);
#endif
        if(c.op==u::Op::ThreadExit){for(auto& v:objects)if(v.id&&v.pid==p.pid&&v.tid==p.tid)Remove(v);for(auto& v:pending)if(v.pid==p.pid&&v.sender==p.tid)v={};
#ifdef GUARDIAN_NATIVE_CONTROLS
            ClearExtra(p.pid,p.tid);graphics.Exit(p.pid,p.tid);
#endif
            return 0;}
        if(c.op==u::Op::Finish){if(c.flags){for(auto& v:objects)if(v.id)Remove(v);pending={};
#ifdef GUARDIAN_NATIVE_CONTROLS
            ClearExtra(0,0);graphics.Clear();
#endif
            }if(std::any_of(objects.begin(),objects.end(),[](const auto& v){return v.id!=0;})||std::any_of(pending.begin(),pending.end(),[](const auto& v){return v.id!=0;}))return ERROR_BUSY;complete=true;r.completed=1;events.clear();return 0;}
        if(c.op==u::Op::Focus&&!c.window){auto* focused=Find(GetFocus());if(!c.flags)SetFocus(nullptr);r.value=focused&&focused->pid==p.pid&&focused->tid==p.tid?focused->id:0;return 0;}
        if(!o||o->pid!=p.pid)return ERROR_INVALID_HANDLE;
        if(c.op!=u::Op::Query&&c.op!=u::Op::Send&&c.op!=u::Op::Post&&o->tid!=p.tid)return ERROR_ACCESS_DENIED;
        switch(c.op){
        case u::Op::Query:Describe(*o,r.window);return 0;
        case u::Op::Style:{const auto old=uint32_t(GetWindowLongPtrW(o->hwnd,GWL_STYLE));if((old^c.style)&(WS_CHILD|WS_POPUP))return ERROR_NOT_SUPPORTED;if((o->kind==u::Class::Button&&(c.style&15)>BS_AUTOCHECKBOX)||(o->kind==u::Class::Static&&(c.style&31)>SS_RIGHT)||(o->kind==u::Class::Edit&&(c.style&(ES_MULTILINE|ES_PASSWORD|ES_OEMCONVERT|ES_WANTRETURN))))return ERROR_NOT_SUPPORTED;SetWindowLongPtrW(o->hwnd,GWL_STYLE,c.style);SetWindowPos(o->hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);r.value=old;++calls[2];return 0;}
        case u::Op::Parent:{auto* parent=Find(c.other);if(!parent||parent->pid!=p.pid||parent->tid!=p.tid||!(GetWindowLongPtrW(o->hwnd,GWL_STYLE)&WS_CHILD))return ERROR_INVALID_HANDLE;for(auto* ancestor=parent;ancestor;ancestor=Find(ancestor->parent))if(ancestor==o)return ERROR_INVALID_PARAMETER;SetLastError(0);const auto previous=SetParent(o->hwnd,parent->hwnd);if(!previous&&GetLastError())return GetLastError();r.value=o->parent;o->parent=parent->id;++calls[2];return 0;}
        case u::Op::Destroy:Remove(*o);return 0;
        case u::Op::Position:{const auto dpi=GetDpiForWindow(o->hwnd);if(!SetWindowPos(o->hwnd,nullptr,Physical(c.rect.left,dpi),Physical(c.rect.top,dpi),Physical(c.rect.right-c.rect.left,dpi),Physical(c.rect.bottom-c.rect.top,dpi),c.flags|SWP_NOZORDER|SWP_NOACTIVATE))return ERROR_INVALID_PARAMETER;++calls[2];return 0;}
        case u::Op::Enable:r.value=EnableWindow(o->hwnd,c.flags);++calls[2];return 0;
        case u::Op::Show:r.value=ShowWindow(o->hwnd,c.flags?SW_SHOWNOACTIVATE:SW_HIDE);++calls[2];return 0;
        case u::Op::Focus:{const auto previous=SetFocus(o->hwnd);auto* old=Find(previous);r.value=old?old->id:0;++calls[2];return 0;}
        case u::Op::Text:{std::wstring text(reinterpret_cast<const wchar_t*>(c.text));if(!(GetWindowLongPtrW(o->hwnd,GWL_STYLE)&WS_CHILD))text=Prefix+text;if(!SetWindowTextW(o->hwnd,text.c_str()))return ERROR_GEN_FAILURE;++calls[3];return 0;}
        case u::Op::GetText:{wchar_t text[280]{};GetWindowTextW(o->hwnd,text,280);const wchar_t* source=text;if(!(GetWindowLongPtrW(o->hwnd,GWL_STYLE)&WS_CHILD)&&!wcsncmp(text,Prefix,std::size(Prefix)-1))source+=std::size(Prefix)-1;const auto length=wcslen(source);if(length>255)return ERROR_INSUFFICIENT_BUFFER;std::copy_n(source,length,reinterpret_cast<wchar_t*>(r.text));r.value=length;++calls[3];return 0;}
        case u::Op::NativeMessage:{
#ifdef GUARDIAN_NATIVE_CONTROLS
            if(c.message==WM_SETFONT||c.message==WM_GETFONT||c.message==STM_SETICON||c.message==STM_GETICON||c.message==STM_SETIMAGE||c.message==STM_GETIMAGE)return ControlResource(*o,c,r,p.pid,p.tid);
            if(o->kind==nc::List||o->kind==nc::Combo)return ControlScalar(*o,c,r);
#endif
            const bool button=o->kind==u::Class::Button&&(c.message==BM_CLICK||c.message==BM_GETCHECK||c.message==BM_SETCHECK);
            const bool edit=o->kind==u::Class::Edit&&(c.message==EM_SETSEL||c.message==EM_GETSEL);
            if(!button&&!edit)return ERROR_NOT_SUPPORTED;
            if((c.message==BM_CLICK||c.message==BM_GETCHECK||c.message==EM_GETSEL)&&(c.wp||c.lp))return ERROR_INVALID_PARAMETER;
            if(c.message==BM_SETCHECK&&(c.wp>2||c.lp))return ERROR_INVALID_PARAMETER;
            if(c.message==EM_SETSEL&&((c.wp>255&&c.wp!=uint64_t(-1))||(c.lp>255&&c.lp!=uint64_t(-1))))return ERROR_INVALID_PARAMETER;
            r.value=uint64_t(SendMessageW(o->hwnd,c.message,WPARAM(c.wp),LPARAM(c.lp)));++calls[4];return 0;
        }
        case u::Op::Send:case u::Op::Post:{
            if(o->kind!=u::Class::Custom)return ERROR_NOT_SUPPORTED;
            if(events.size()>=u::MaxEvents)return ERROR_NOT_ENOUGH_QUOTA;
            u::Event e;e.window=o->id;e.tid=o->tid;e.message=c.message;e.wp=c.wp;e.lp=c.lp;e.time=GetTickCount();e.serial=nextEvent++;
            if(c.op==u::Op::Send){Pending* entry=nullptr;for(auto& v:pending)if(!v.id){entry=&v;break;}if(!entry)return ERROR_NOT_ENOUGH_QUOTA;e.cookie=(uint64_t(2)<<56)|((session&0xFFFFFF)<<32)|nextCookie++;e.flags=1;*entry={e.cookie,0,o->id,p.tid,o->tid,p.pid,ERROR_IO_PENDING,GetTickCount64()+2000};r.value=e.cookie;}
            events.push_back(e);++calls[4];return 0;
        }
        case u::Op::Timer:{
#ifdef GUARDIAN_NATIVE_CONTROLS
            if(ExtraCount()+std::count_if(timers.begin(),timers.end(),[](const auto& t){return t.native!=0;})>=u::MaxTimers)return ERROR_NOT_ENOUGH_QUOTA;
#endif
            Timer* slot=nullptr;for(auto& t:timers)if(t.window==o->id&&t.id==c.wp){slot=&t;break;}if(!slot)for(auto& t:timers)if(!t.native){slot=&t;break;}if(!slot)return ERROR_NOT_ENOUGH_QUOTA;if(slot->native)KillTimer(channel,slot->native);const auto nativeId=nextTimer++;if(!SetTimer(channel,nativeId,UINT(c.lp),nullptr)){*slot={};return ERROR_NOT_ENOUGH_MEMORY;}slot->native=nativeId;slot->window=o->id;slot->id=c.wp;r.value=c.wp;++calls[5];return 0;}
        case u::Op::KillTimer:for(auto& t:timers)if(t.window==o->id&&t.id==c.wp){KillTimer(channel,t.native);t={};events.erase(std::remove_if(events.begin(),events.end(),[&](const auto& e){return e.window==o->id&&e.message==WM_TIMER&&e.wp==c.wp;}),events.end());++calls[5];return 0;}return ERROR_INVALID_HANDLE;
        case u::Op::Invalidate:InvalidateRect(o->hwnd,nullptr,TRUE);UpdateWindow(o->hwnd);++calls[6];return 0;
        default:return ERROR_NOT_SUPPORTED;
        }
    }
    bool Exchange(WirePacket& p){
        if(complete||p.magic!=WireMagic||p.version!=WireVersion||p.bytes!=sizeof(p)||p.boot!=boot||p.session!=session||p.sequence!=sequence+1||p.sequence>u::MaxCommands||!p.pid||!p.tid||!guardian::nativegui::Empty(&p.reply,sizeof(p.reply)))return false;
        ++sequence;
#ifdef GUARDIAN_NATIVE_GDI
        if(!g::Empty(&p.graphics,sizeof(p.graphics)))return false;
        if(p.draw.op!=g::Op::None){u::Command empty;if(std::memcmp(&p.command,&empty,sizeof(empty)))return false;
#ifdef GUARDIAN_GDI_EXPANDED
            const auto paintWindow=p.draw.op==g::Op::EndPaint?graphics.PaintWindow(p.draw.dc,p.pid,p.tid):0;
#endif
            p.graphics.status=graphics.Execute(p.draw,p.graphics,p.pid,p.tid,[&](uint64_t id,uint32_t pid,uint32_t tid){auto* o=Find(id);return o&&o->pid==pid&&o->tid==tid&&o->kind==u::Class::Custom?o->hwnd:nullptr;});p.reply.status=p.graphics.status;
#ifdef GUARDIAN_GDI_EXPANDED
            if(paintWindow&&!p.graphics.status)if(auto* window=Find(paintWindow)){window->paintOutstanding=false;if(graphics.PendingPaint(paintWindow))Queue(*window,WM_PAINT);}
#endif
        }else p.reply.status=!g::Valid(p.draw)||!
#ifdef GUARDIAN_NATIVE_CONTROLS
        nc::Valid(p.command)
#else
        u::Valid(p.command)
#endif
        ?ERROR_INVALID_PARAMETER:overflow?ERROR_NOT_ENOUGH_QUOTA:Execute(p);
        graphics.Report(p.graphics);
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
        if(p.command.op==u::Op::Finish)graphics.SoftwareFinal(p.graphics);
#ifdef GUARDIAN_AUDIO
        if(p.command.op==u::Op::Finish)graphics.AudioFinal(p.graphics);
#endif
#endif
#else
        p.reply.status=!u::Valid(p.command)?ERROR_INVALID_PARAMETER:overflow?ERROR_NOT_ENOUGH_QUOTA:Execute(p);
#endif
        for(auto& o:objects)if(o.id)++p.reply.live[0];for(auto& t:timers)if(t.native)++p.reply.live[1];p.reply.live[2]=uint32_t(events.size());
#ifdef GUARDIAN_NATIVE_CONTROLS
        p.reply.live[1]+=ExtraCount();
#endif
        p.reply.restrictions=31;p.reply.helperPid=GetCurrentProcessId();std::copy(std::begin(calls),std::end(calls),std::begin(p.reply.calls));return true;
    }
};
int Run(){
    const auto input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);if(GetFileType(input)!=FILE_TYPE_PIPE||GetFileType(output)!=FILE_TYPE_PIPE)return 3;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(av::microvm::native_gui::Restrictions()!=31)return 4;
    WNDCLASSW cls{};cls.lpfnWndProc=Host::Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=ClassName;cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));if(!RegisterClassW(&cls))return 5;
#ifdef GUARDIAN_GDI_EXPANDED
    for(unsigned i=1;i<8;++i){cls.lpszClassName=ExpandedClasses[i];cls.style=(i&3)|((i&4)<<1);if(!RegisterClassW(&cls))return 5;}cls.style=0;
#endif
    cls.lpfnWndProc=Host::Channel;cls.lpszClassName=L"Aegis.NativeUser32.Channel";if(!RegisterClassW(&cls))return 5;
    Host host;host.done=CreateEventW(nullptr,FALSE,FALSE,nullptr);host.channel=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,&host);if(!host.done||!host.channel)return 5;
    auto& p=host.packet;DWORD written=0;if(!av::microvm::native_gui::ReadPacket(input,p)||p.magic!=WireMagic||p.version!=WireVersion||p.bytes!=sizeof(p)||!p.boot||!p.session||p.sequence||p.pid||p.tid||p.command.op!=u::Op::Hello||!u::Valid(p.command)||!guardian::nativegui::Empty(&p.reply,sizeof(p.reply)))return 6;
    host.boot=p.boot;host.session=p.session;
#ifdef GUARDIAN_NATIVE_GDI
    host.graphics.session=p.session;
#endif
    p.reply.restrictions=31;p.reply.helperPid=GetCurrentProcessId();if(!WriteFile(output,&p,sizeof(p),&written,nullptr)||written!=sizeof(p))return 7;
    const auto uiThread=GetCurrentThreadId();std::atomic<int> result{0};
    // The transport never executes a GUI command. Posted tasks still run inside native modal loops.
    std::thread transport([&]{try{for(;;){DWORD put=0;if(!av::microvm::native_gui::ReadPacket(input,p)){result=host.complete?0:8;break;}if(!PostMessageW(host.channel,WM_APP+1,0,0)||WaitForSingleObject(host.done,
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
10000
#else
2000
#endif
)!=WAIT_OBJECT_0){result=9;break;}if(!WriteFile(output,&p,sizeof(p),&put,nullptr)||put!=sizeof(p)){result=10;break;}}}catch(...){result=12;}PostThreadMessageW(uiThread,WM_QUIT,0,0);});
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}transport.join();return result;
}
}
int main(int argc,char** argv){if(argc!=2||std::string(argv[1])!="--native-gui-worker")return 2;try{return Run();}catch(...){return 12;}}
