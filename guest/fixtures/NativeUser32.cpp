#define _USER32_
#ifdef GUARDIAN_NATIVE_GDI
#define _GDI32_
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#ifdef GUARDIAN_COMPAT32
static __int64 CompatExchange64(volatile __int64* pointer,__int64 value){
    auto previous=_InterlockedCompareExchange64(pointer,0,0);
    for(;;){const auto observed=_InterlockedCompareExchange64(pointer,value,previous);if(observed==previous)return previous;previous=observed;}
}
#define _InterlockedExchange64 CompatExchange64
#endif
#include <limits.h>
#include "guardian/NativeUser32.h"
#ifdef GUARDIAN_NATIVE_CONTROLS
#include "guardian/NativeControls.h"
namespace nc=guardian::nativecontrols;
extern "C" HBRUSH WINAPI UiGetSysColorBrush(int);
#endif
namespace u=guardian::nativeuser;
extern "C" const int NativeUserAnchor=1410;
const void* volatile NativeUserRelocation=&NativeUserAnchor;
#ifdef GUARDIAN_NATIVE_GDI
#include "guardian/NativeGdi.h"
#ifdef GUARDIAN_GDI_EXPANDED
#include "guardian/ExpandedGdi.h"
namespace e=guardian::expandedgdi;
#endif
namespace g=guardian::nativegdi;
extern "C" __declspec(dllimport) DWORD GuardianNativeGdiCall(g::Call*) noexcept;
static DWORD UserService(u::Call* c) noexcept {g::Call request;request.command=c->command;const auto status=GuardianNativeGdiCall(&request);c->reply=request.reply;return status;}
#else
extern "C" __declspec(dllimport) DWORD GuardianNativeUserCall(u::Call*) noexcept;
#ifdef GUARDIAN_COMPAT32
#include "Compat32User.inl"
static DWORD UserService(u::Call* c) noexcept {return CompatUserCall(c);}
#else
static DWORD UserService(u::Call* c) noexcept {return GuardianNativeUserCall(c);}
#endif
#endif
extern "C" LRESULT WINAPI UiDefWindowProcW(HWND,UINT,WPARAM,LPARAM);
extern "C" BOOL WINAPI UiDestroyWindow(HWND);
namespace {
void Copy(void* d,const void* s,size_t n){for(size_t i=0;i<n;++i)static_cast<BYTE*>(d)[i]=static_cast<const BYTE*>(s)[i];}
void Zero(void* d,size_t n){for(size_t i=0;i<n;++i)static_cast<BYTE*>(d)[i]=0;}
volatile int64_t lockOwner=0;uint32_t depth=0;
void Lock(){const auto id=int64_t(GetCurrentThreadId());while(_InterlockedCompareExchange64(&lockOwner,id,0)!=0&&lockOwner!=id)SwitchToThread();++depth;}
void Unlock(){if(!--depth)_InterlockedExchange64(&lockOwner,0);}
struct Guard{Guard(){Lock();}~Guard(){Unlock();}};
struct Suspend {uint32_t saved=depth;Suspend(){depth=0;_InterlockedExchange64(&lockOwner,0);}~Suspend(){Lock();depth=saved;}};
template<class T>T Fail(DWORD error,T result){SetLastError(error);return result;}
bool Wide(const wchar_t* s,uint16_t* d){if(!s)return true;for(uint32_t i=0;i<256;++i){d[i]=uint16_t(s[i]);if(!s[i])return
#ifdef GUARDIAN_NATIVE_CONTROLS
nc::Text(d);
#else
u::Text(d);
#endif
}return false;}
bool Ansi(const char* s,uint16_t* d){return !s||MultiByteToWideChar(1252,0,s,-1,reinterpret_cast<wchar_t*>(d),256)>0;}
bool Equal(const uint16_t* a,const uint16_t* b){for(uint32_t i=0;i<256;++i){auto x=a[i],y=b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;if(!x)return true;}return false;}
bool Name(const uint16_t* a,const wchar_t* b){if(uintptr_t(a)<=65535)return false;uint16_t text[256]{};return Wide(b,text)&&Equal(a,text);}
struct Class {ATOM atom=0;WNDPROC proc=nullptr;uint16_t name[256]{};bool wide=true;HBRUSH background=nullptr;HICON icon=nullptr;
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
HINSTANCE instance=nullptr;uint32_t extra=0;
#endif
#ifdef GUARDIAN_GDI_EXPANDED
DWORD style=0;
#endif
};
struct Window {HWND h=nullptr,parent=nullptr;Class* cls=nullptr;WNDPROC proc=nullptr;LONG_PTR user=0,id=0;uint32_t tid=0;u::Class kind{};bool destroying=false,wide=true;
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
BYTE extra[32]{};
#endif
};
struct Timer {HWND h=nullptr;UINT_PTR id=0;TIMERPROC proc=nullptr;};
struct Stamp {bool used=false;DWORD tid=0;uint64_t serial=0;MSG msg{};WINDOWPOS pos{};uint32_t flags=0;};
struct Quit {DWORD tid=0;int code=0;bool pending=false;};
Class classes[16]{};Window windows[u::MaxWindows]{};Timer timers[u::MaxTimers]{};Stamp stamps[256]{};Quit quits[16]{};ATOM nextAtom=1;
Window* Local(HWND h){for(auto& w:windows)if(w.h==h&&h)return &w;return nullptr;}
bool Call(u::Call& c){const auto status=UserService(&c);if(status||c.reply.status){SetLastError(status?status:c.reply.status);return false;}return true;}
#ifdef GUARDIAN_NATIVE_CONTROLS
void NativeRejected(uint32_t api,uint64_t a,uint64_t b,u::Rect rect={}){u::Call c;c.command.op=static_cast<u::Op>(nc::Op::Unsupported);c.command.message=api;c.command.wp=a;c.command.lp=b;c.command.rect=rect;UserService(&c);}
#endif
bool Query(HWND h,u::Window& w){u::Call c;c.command.op=u::Op::Query;c.command.window=uint64_t(h);if(!Call(c))return false;w=c.reply.window;return true;}
LRESULT Invoke(HWND h,UINT m,WPARAM wp,LPARAM lp){auto* local=Local(h);const auto proc=local?local->proc:nullptr;if(proc){Suspend unlock;return proc(h,m,wp,lp);}return UiDefWindowProcW(h,m,wp,lp);}
void Event(const u::Event& e){WINDOWPOS pos{};LPARAM lp=LPARAM(e.lp);if(e.flags&4){pos.hwnd=reinterpret_cast<HWND>(e.window);pos.x=e.rect.left;pos.y=e.rect.top;pos.cx=e.rect.right-e.rect.left;pos.cy=e.rect.bottom-e.rect.top;pos.flags=e.positionFlags;lp=LPARAM(&pos);}const auto value=Invoke(reinterpret_cast<HWND>(e.window),e.message,WPARAM(e.wp),lp);if(e.cookie){u::Call reply;reply.command.op=u::Op::Reply;reply.command.window=e.cookie;reply.command.wp=uint64_t(value);Call(reply);}}
void Drain(){for(uint32_t i=0;i<u::MaxEvents;++i){u::Call c;c.command.op=u::Op::Poll;c.command.flags=3;if(!Call(c)||!c.reply.value)break;Event(c.reply.event);}}
HWND Create(DWORD ex,const uint16_t* name,const uint16_t* title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,void* param,bool wide){
    Guard guard;Class* cls=nullptr;u::Class kind=u::Class::Custom;
    if(Name(name,L"BUTTON"))kind=u::Class::Button;else if(Name(name,L"STATIC"))kind=u::Class::Static;else if(Name(name,L"EDIT"))kind=u::Class::Edit;
#ifdef GUARDIAN_NATIVE_CONTROLS
    else if(Name(name,L"LISTBOX"))kind=nc::List;else if(Name(name,L"COMBOBOX"))kind=nc::Combo;
#endif
    else{for(auto& c:classes)if(c.atom&&((uintptr_t(name)<=65535&&c.atom==ATOM(uintptr_t(name)))||(uintptr_t(name)>65535&&Equal(name,c.name)))){cls=&c;break;}if(!cls){
#ifdef GUARDIAN_NATIVE_CONTROLS
NativeRejected(3,style,0,{x,y,width,height});
#endif
return Fail(ERROR_CANNOT_FIND_WND_CLASS,HWND{});}}
#ifdef GUARDIAN_NATIVE_CONTROLS
    if(Name(name,L"LISTBOX"))kind=nc::List;else if(Name(name,L"COMBOBOX"))kind=nc::Combo;
#endif
    if(cls)wide=cls->wide;
#ifdef GUARDIAN_GDI_EXPANDED
    if(!(style&(WS_CHILD|WS_POPUP))){if(x==CW_USEDEFAULT)x=120;if(y==CW_USEDEFAULT)y=100;if(width==CW_USEDEFAULT)width=640;if(height==CW_USEDEFAULT)height=480;}
#endif
    if(width<=0||height<=0||width>2048||height>2048||x<-8192||x>8192||y<-8192||y>8192){
#ifdef GUARDIAN_NATIVE_CONTROLS
NativeRejected(4,style,0,{x,y,width,height});
#endif
return Fail(ERROR_INVALID_PARAMETER,HWND{});}
    Window* local=nullptr;for(auto& w:windows)if(!w.h){local=&w;break;}
    // The kernel can remove a departed thread's native windows after its last
    // user instruction. Reclaim these expired local records before reporting quota.
    if(!local)for(auto& w:windows){u::Window info;if(!Query(w.h,info)&&GetLastError()==ERROR_INVALID_HANDLE){for(auto& t:timers)if(t.h==w.h)t={};for(auto& stamp:stamps)if(stamp.used&&stamp.msg.hwnd==w.h)stamp={};w={};local=&w;break;}}
    if(!local)return Fail(ERROR_NOT_ENOUGH_QUOTA,HWND{});
    u::Call c;c.command.op=u::Op::Create;c.command.other=uint64_t(parent);c.command.rect={x,y,x+width,y+height};c.command.style=style;c.command.exStyle=ex;c.command.kind=kind;c.command.id=(style&WS_CHILD)?uint32_t(uintptr_t(menu)):0;
#ifdef GUARDIAN_NATIVE_CONTROLS
    c.command.lp=cls?uint64_t(cls->icon):0;
#ifdef GUARDIAN_GDI_EXPANDED
c.command.reserved=cls?cls->style:0;
#endif
#endif
    #ifndef GUARDIAN_NATIVE_CONTROLS
    if(!(style&WS_CHILD)&&menu)return Fail(ERROR_NOT_SUPPORTED,HWND{});
#endif
    Copy(c.command.text,title,sizeof(c.command.text));if(!Call(c))return nullptr;
    const auto h=reinterpret_cast<HWND>(c.reply.value);*local={h,parent,cls,cls?cls->proc:nullptr,0,LONG_PTR(menu),GetCurrentThreadId(),kind,false,wide};
    CREATESTRUCTW cs{};cs.lpCreateParams=param;cs.hInstance=instance;cs.hMenu=menu;cs.hwndParent=parent;cs.cx=width;cs.cy=height;cs.x=x;cs.y=y;cs.style=LONG(style);cs.lpszName=reinterpret_cast<LPCWSTR>(title);cs.lpszClass=reinterpret_cast<LPCWSTR>(name);cs.dwExStyle=ex;
    char ansiTitle[256]{},ansiClass[256]{};CREATESTRUCTA ca{};
    if(!wide){Copy(&ca,&cs,sizeof(ca));WideCharToMultiByte(1252,0,reinterpret_cast<LPCWSTR>(title),-1,ansiTitle,256,nullptr,nullptr);if(uintptr_t(name)>65535)WideCharToMultiByte(1252,0,reinterpret_cast<LPCWSTR>(name),-1,ansiClass,256,nullptr,nullptr);ca.lpszName=ansiTitle;ca.lpszClass=uintptr_t(name)<=65535?reinterpret_cast<LPCSTR>(name):ansiClass;}
    const auto data=wide?LPARAM(&cs):LPARAM(&ca);
    #ifdef GUARDIAN_NATIVE_CONTROLS
    if(!(style&WS_CHILD)&&menu){u::Call attach;attach.command.op=static_cast<u::Op>(nc::Op::Attach);attach.command.window=uint64_t(h);attach.command.other=uint64_t(menu);if(!Call(attach)){UiDestroyWindow(h);return nullptr;}}
#endif
    if(kind==u::Class::Custom&&(!Invoke(h,WM_NCCREATE,0,data)||Invoke(h,WM_CREATE,0,data)==-1)){UiDestroyWindow(h);return nullptr;}Drain();return h;
}
LRESULT TextMessage(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_SETTEXT){u::Call c;c.command.op=u::Op::Text;c.command.window=uint64_t(h);if(!Wide(reinterpret_cast<LPCWSTR>(l),c.command.text))return Fail(ERROR_INVALID_PARAMETER,LRESULT{});const bool ok=Call(c);Drain();return ok;}
    if(m==WM_GETTEXT||m==WM_GETTEXTLENGTH){u::Call c;c.command.op=u::Op::GetText;c.command.window=uint64_t(h);if(!Call(c))return 0;if(m==WM_GETTEXTLENGTH)return LRESULT(c.reply.value);if(!w||!l)return 0;const auto count=(c.reply.value<w-1)?size_t(c.reply.value):size_t(w-1);Copy(reinterpret_cast<void*>(l),c.reply.text,count*2);reinterpret_cast<wchar_t*>(l)[count]=0;return LRESULT(count);}
    return 0;
}
LRESULT Send(HWND h,UINT m,WPARAM w,LPARAM l){
    u::Window info;if(!Query(h,info))return 0;
    if(info.kind==u::Class::Custom&&info.tid==GetCurrentThreadId())return Invoke(h,m,w,l);
    if(m==WM_SETTEXT||m==WM_GETTEXT||m==WM_GETTEXTLENGTH){if(info.tid!=GetCurrentThreadId())return Fail(ERROR_NOT_SUPPORTED,LRESULT{});return TextMessage(h,m,w,l);}
#ifdef GUARDIAN_NATIVE_CONTROLS
    if(info.kind==nc::List||info.kind==nc::Combo){const bool list=info.kind==nc::List;const bool text=m==UINT(list?LB_ADDSTRING:CB_ADDSTRING)||m==UINT(list?LB_INSERTSTRING:CB_INSERTSTRING)||m==UINT(list?LB_GETTEXT:CB_GETLBTEXT);if(text){u::Call c;c.command.op=static_cast<u::Op>(nc::Op::ControlText);c.command.window=uint64_t(h);c.command.message=m;c.command.wp=w;const bool get=m==UINT(list?LB_GETTEXT:CB_GETLBTEXT);if(!get&&!Wide(reinterpret_cast<LPCWSTR>(l),c.command.text))return Fail(ERROR_INVALID_PARAMETER,LRESULT{});if(!Call(c))return LB_ERR;if(get){if(!l)return Fail(ERROR_INVALID_PARAMETER,LRESULT{});Copy(reinterpret_cast<void*>(l),c.reply.text,(size_t(c.reply.value)+1)*2);}Drain();return LRESULT(c.reply.value);}}
#endif
    if(info.kind!=u::Class::Custom){u::Call c;c.command.op=u::Op::NativeMessage;c.command.window=uint64_t(h);c.command.message=m;c.command.wp=m==EM_GETSEL?0:w;c.command.lp=m==EM_GETSEL?0:uint64_t(l);if(!Call(c))return 0;if(m==EM_GETSEL){if(w)*reinterpret_cast<DWORD*>(w)=LOWORD(c.reply.value);if(l)*reinterpret_cast<DWORD*>(l)=HIWORD(c.reply.value);}Drain();return LRESULT(c.reply.value);}
    if(info.tid==GetCurrentThreadId())return Invoke(h,m,w,l);
    if(!u::ScalarMessage(m))return Fail(ERROR_NOT_SUPPORTED,LRESULT{});
    u::Call c;c.command.op=u::Op::Send;c.command.window=uint64_t(h);c.command.message=m;c.command.wp=w;c.command.lp=uint64_t(l);if(!Call(c))return 0;const auto cookie=c.reply.value;
    for(;;){u::Call result;result.command.op=u::Op::Result;result.command.window=cookie;const auto status=UserService(&result);if(status!=ERROR_IO_PENDING){if(status)return Fail(status,LRESULT{});return LRESULT(result.reply.value);}Drain();{Suspend unlock;Sleep(1);}}
}
}
extern "C" BOOL WINAPI NativeUserEntry(HINSTANCE,DWORD,void*){return TRUE;}
extern "C" ATOM WINAPI UiRegisterClassExW(const WNDCLASSEXW* c){Guard guard;if(!c||c->cbSize!=sizeof(*c)||!c->lpfnWndProc||!c->lpszClassName||c->cbClsExtra||
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
c->cbWndExtra<0||c->cbWndExtra>32||
#else
c->cbWndExtra||
#endif
c->lpszMenuName
#ifndef GUARDIAN_NATIVE_CONTROLS
||c->style||c->hIcon||c->hIconSm||c->hCursor||c->hbrBackground
#else
||c->style&~(CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS)
#ifdef GUARDIAN_GDI_EXPANDED
||(c->hCursor&&uint64_t(c->hCursor)!=((11ull<<56)|32512))
#else
||c->hCursor
#endif
#endif
){
#ifdef GUARDIAN_NATIVE_CONTROLS
NativeRejected(2,c?c->style:0,c?uint64_t(c->cbClsExtra)|(uint64_t(c->cbWndExtra)<<16)|(uint64_t(bool(c->lpszMenuName))<<32)|(uint64_t(bool(c->hCursor))<<33):uint64_t(-1));
#endif
return Fail(ERROR_NOT_SUPPORTED,ATOM{});}uint16_t name[256]{};if(!Wide(c->lpszClassName,name))return Fail(ERROR_INVALID_PARAMETER,ATOM{});for(auto& entry:classes)if(entry.atom&&Equal(name,entry.name))return Fail(ERROR_CLASS_ALREADY_EXISTS,ATOM{});if(!nextAtom)return Fail(ERROR_NOT_ENOUGH_QUOTA,ATOM{});for(auto& entry:classes)if(!entry.atom){entry.atom=nextAtom++;entry.proc=c->lpfnWndProc;
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
entry.instance=c->hInstance?c->hInstance:GetModuleHandleW(nullptr);entry.extra=uint32_t(c->cbWndExtra);
#endif
#ifdef GUARDIAN_NATIVE_CONTROLS
entry.background=c->hbrBackground;entry.icon=c->hIcon;
#ifdef GUARDIAN_GDI_EXPANDED
entry.style=c->style;
#endif
#endif
Copy(entry.name,name,sizeof(name));return entry.atom;}return Fail(ERROR_NOT_ENOUGH_QUOTA,ATOM{});}
extern "C" ATOM WINAPI UiRegisterClassExA(const WNDCLASSEXA* c){if(!c)return 0;uint16_t name[256]{};if(!Ansi(c->lpszClassName,name))return 0;WNDCLASSEXW wide{};Copy(&wide,c,sizeof(wide));wide.lpszClassName=reinterpret_cast<LPCWSTR>(name);const auto atom=UiRegisterClassExW(&wide);if(atom){Guard guard;for(auto& entry:classes)if(entry.atom==atom)entry.wide=false;}return atom;}
extern "C" ATOM WINAPI UiRegisterClassW(const WNDCLASSW* c){if(!c)return 0;WNDCLASSEXW x{};x.cbSize=sizeof(x);x.style=c->style;x.lpfnWndProc=c->lpfnWndProc;x.cbClsExtra=c->cbClsExtra;x.cbWndExtra=c->cbWndExtra;x.hInstance=c->hInstance;x.hIcon=c->hIcon;x.hCursor=c->hCursor;x.hbrBackground=c->hbrBackground;x.lpszMenuName=c->lpszMenuName;x.lpszClassName=c->lpszClassName;return UiRegisterClassExW(&x);}
extern "C" ATOM WINAPI UiRegisterClassA(const WNDCLASSA* c){if(!c)return 0;WNDCLASSEXA x{};x.cbSize=sizeof(x);x.style=c->style;x.lpfnWndProc=c->lpfnWndProc;x.cbClsExtra=c->cbClsExtra;x.cbWndExtra=c->cbWndExtra;x.hInstance=c->hInstance;x.hIcon=c->hIcon;x.hCursor=c->hCursor;x.hbrBackground=c->hbrBackground;x.lpszMenuName=c->lpszMenuName;x.lpszClassName=c->lpszClassName;return UiRegisterClassExA(&x);}
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
extern "C" BOOL WINAPI UiUnregisterClassW(LPCWSTR name,HINSTANCE instance){Guard guard;uint16_t text[256]{};if(uintptr_t(name)>65535&&!Wide(name,text))return Fail(ERROR_INVALID_PARAMETER,FALSE);if(!instance)instance=GetModuleHandleW(nullptr);for(auto& entry:classes)if(entry.atom&&entry.instance==instance&&((uintptr_t(name)<=65535&&entry.atom==ATOM(uintptr_t(name)))||(uintptr_t(name)>65535&&Equal(text,entry.name)))){for(const auto& window:windows)if(window.h&&window.cls==&entry)return Fail(ERROR_CLASS_HAS_WINDOWS,FALSE);if(entry.background)return Fail(ERROR_NOT_SUPPORTED,FALSE);entry={};return TRUE;}return Fail(ERROR_CLASS_DOES_NOT_EXIST,FALSE);}
extern "C" BOOL WINAPI UiUnregisterClassA(LPCSTR name,HINSTANCE instance){if(uintptr_t(name)<=65535)return UiUnregisterClassW(reinterpret_cast<LPCWSTR>(name),instance);uint16_t text[256]{};if(!Ansi(name,text))return Fail(ERROR_INVALID_PARAMETER,FALSE);return UiUnregisterClassW(reinterpret_cast<LPCWSTR>(text),instance);}
#endif
extern "C" HWND WINAPI UiCreateWindowExW(DWORD e,LPCWSTR n,LPCWSTR t,DWORD s,int x,int y,int w,int h,HWND p,HMENU m,HINSTANCE i,void* v){uint16_t name[256]{},title[256]{};if((uintptr_t(n)>65535&&!Wide(n,name))||!Wide(t,title))return Fail(ERROR_INVALID_PARAMETER,HWND{});return Create(e,uintptr_t(n)<=65535?reinterpret_cast<const uint16_t*>(n):name,title,s,x,y,w,h,p,m,i,v,true);}
extern "C" HWND WINAPI UiCreateWindowExA(DWORD e,LPCSTR n,LPCSTR t,DWORD s,int x,int y,int w,int h,HWND p,HMENU m,HINSTANCE i,void* v){uint16_t name[256]{},title[256]{};if((uintptr_t(n)>65535&&!Ansi(n,name))||!Ansi(t,title))return Fail(ERROR_INVALID_PARAMETER,HWND{});return Create(e,uintptr_t(n)<=65535?reinterpret_cast<const uint16_t*>(n):name,title,s,x,y,w,h,p,m,i,v,false);}
extern "C" BOOL WINAPI UiDestroyWindow(HWND h){Guard guard;auto* local=Local(h);if(!local||local->tid!=GetCurrentThreadId())return Fail(ERROR_INVALID_WINDOW_HANDLE,FALSE);if(local->destroying)return TRUE;local->destroying=true;for(auto& child:windows)if(child.h&&child.parent==h)UiDestroyWindow(child.h);Invoke(h,WM_DESTROY,0,0);Invoke(h,WM_NCDESTROY,0,0);u::Call c;c.command.op=u::Op::Destroy;c.command.window=uint64_t(h);if(!Call(c)){local->destroying=false;return FALSE;}for(auto& t:timers)if(t.h==h)t={};*local={};return TRUE;}
extern "C" LRESULT WINAPI UiDefWindowProcW(HWND h,UINT m,WPARAM w,LPARAM l){Guard guard;if(m==WM_NCCREATE)return TRUE;
#ifdef GUARDIAN_NATIVE_CONTROLS
if(m==WM_SETICON||m==WM_GETICON){u::Call c;c.command.op=static_cast<u::Op>(nc::Op::SetIcon);c.command.window=uint64_t(h);c.command.other=m==WM_SETICON?uint64_t(l):0;c.command.wp=w;c.command.flags=m==WM_GETICON;return Call(c)?LRESULT(c.reply.value):0;}
#endif
if(m==WM_CLOSE){UiDestroyWindow(h);return 0;}if(m==WM_SETTEXT||m==WM_GETTEXT||m==WM_GETTEXTLENGTH)return TextMessage(h,m,w,l);if(m==WM_CREATE||m==WM_DESTROY||m==WM_NCDESTROY||m==WM_PAINT||m==WM_WINDOWPOSCHANGED||m==WM_SIZE||m==WM_MOVE||m==WM_SETFOCUS||m==WM_KILLFOCUS)return 0;return Fail(ERROR_NOT_SUPPORTED,LRESULT{});}
extern "C" LRESULT WINAPI UiDefWindowProcA(HWND h,UINT m,WPARAM w,LPARAM l){Guard guard;if(m==WM_SETTEXT){uint16_t text[256]{};if(!Ansi(reinterpret_cast<LPCSTR>(l),text))return Fail(ERROR_INVALID_PARAMETER,LRESULT{});return TextMessage(h,m,w,LPARAM(text));}if(m==WM_GETTEXT||m==WM_GETTEXTLENGTH){wchar_t text[256]{};TextMessage(h,WM_GETTEXT,256,LPARAM(text));if(m==WM_GETTEXTLENGTH)return WideCharToMultiByte(1252,0,text,-1,nullptr,0,nullptr,nullptr)-1;if(!w||!l||w>INT_MAX)return 0;const auto size=WideCharToMultiByte(1252,0,text,-1,reinterpret_cast<char*>(l),int(w),nullptr,nullptr);return size?size-1:0;}return UiDefWindowProcW(h,m,w,l);}
extern "C" LRESULT WINAPI UiSendMessageW(HWND h,UINT m,WPARAM w,LPARAM l){Guard guard;auto* local=Local(h);if(local&&local->kind==u::Class::Custom&&!local->wide){char text[256]{};if(m==WM_SETTEXT){if(!WideCharToMultiByte(1252,0,reinterpret_cast<LPCWSTR>(l),-1,text,256,nullptr,nullptr))return 0;return Send(h,m,w,LPARAM(text));}if(m==WM_GETTEXT||m==WM_GETTEXTLENGTH){Send(h,WM_GETTEXT,256,LPARAM(text));if(m==WM_GETTEXTLENGTH)return MultiByteToWideChar(1252,0,text,-1,nullptr,0)-1;if(!w||!l||w>INT_MAX)return 0;const auto count=MultiByteToWideChar(1252,0,text,-1,reinterpret_cast<LPWSTR>(l),int(w));return count?count-1:0;}}return Send(h,m,w,l);}
extern "C" LRESULT WINAPI UiSendMessageA(HWND h,UINT m,WPARAM w,LPARAM l){Guard guard;auto* local=Local(h);if(local&&local->kind==u::Class::Custom&&!local->wide)return Send(h,m,w,l);
#ifdef GUARDIAN_NATIVE_CONTROLS
if(local&&(local->kind==nc::List||local->kind==nc::Combo)){const bool list=local->kind==nc::List;if(m==UINT(list?LB_ADDSTRING:CB_ADDSTRING)||m==UINT(list?LB_INSERTSTRING:CB_INSERTSTRING)){uint16_t text[256]{};if(!Ansi(reinterpret_cast<LPCSTR>(l),text))return LB_ERR;return Send(h,m,w,LPARAM(text));}if(m==UINT(list?LB_GETTEXT:CB_GETLBTEXT)){wchar_t text[256]{};const auto length=Send(h,m,w,LPARAM(text));if(length<0)return length;return WideCharToMultiByte(1252,0,text,-1,reinterpret_cast<LPSTR>(l),256,nullptr,nullptr)-1;}}
#endif
if(m==WM_SETTEXT){uint16_t text[256]{};if(!Ansi(reinterpret_cast<LPCSTR>(l),text))return 0;return Send(h,m,w,LPARAM(text));}if(m==WM_GETTEXT||m==WM_GETTEXTLENGTH){wchar_t text[256]{};const auto length=Send(h,WM_GETTEXT,256,LPARAM(text));if(m==WM_GETTEXTLENGTH)return length;if(!w||!l)return 0;const auto result=WideCharToMultiByte(1252,0,text,-1,reinterpret_cast<char*>(l),int(w),nullptr,nullptr);return result?result-1:0;}return Send(h,m,w,l);}
extern "C" BOOL WINAPI UiPostMessageW(HWND h,UINT m,WPARAM w,LPARAM l){Guard guard;u::Call c;c.command.op=u::Op::Post;c.command.window=uint64_t(h);c.command.message=m;c.command.wp=w;c.command.lp=uint64_t(l);return Call(c);}
extern "C" BOOL WINAPI UiPostMessageA(HWND h,UINT m,WPARAM w,LPARAM l){return UiPostMessageW(h,m,w,l);}
extern "C" void WINAPI UiPostQuitMessage(int code){Guard guard;const auto tid=GetCurrentThreadId();for(auto& q:quits)if(!q.tid||q.tid==tid){q={tid,code,true};return;}SetLastError(ERROR_NOT_ENOUGH_QUOTA);}
extern "C" BOOL WINAPI UiPeekMessageW(MSG* msg,HWND h,UINT low,UINT high,UINT remove){Guard guard;if(!msg||remove&~PM_REMOVE)return Fail(ERROR_INVALID_PARAMETER,FALSE);if(high&&low>high)return Fail(ERROR_INVALID_PARAMETER,FALSE);u::Window filter;if(h&&!Query(h,filter))return Fail(ERROR_INVALID_WINDOW_HANDLE,FALSE);Drain();const auto tid=GetCurrentThreadId();for(auto& q:quits)if(q.tid==tid&&q.pending){Zero(msg,sizeof(*msg));msg->message=WM_QUIT;msg->wParam=WPARAM(q.code);if(remove)q.pending=false;return TRUE;}
    for(uint32_t i=0;i<u::MaxEvents;++i){u::Call c;c.command.op=u::Op::Poll;c.command.flags=remove?1:0;c.command.window=uint64_t(h);c.command.wp=low;c.command.lp=high;if(!Call(c))return FALSE;if(!c.reply.value){SetLastError(0);return FALSE;}const auto& e=c.reply.event;if(e.flags&1){if(!remove){u::Call pop;pop.command=c.command;pop.command.flags=1;if(!Call(pop))return FALSE;}Event(e);continue;}Zero(msg,sizeof(*msg));msg->hwnd=reinterpret_cast<HWND>(e.window);msg->message=e.message;msg->wParam=WPARAM(e.wp);msg->lParam=LPARAM(e.lp);msg->time=e.time;
        Stamp* stamp=nullptr;for(auto& entry:stamps)if(entry.used&&entry.tid==tid&&entry.serial==e.serial){stamp=&entry;break;}if(!stamp)for(auto& entry:stamps)if(!entry.used){stamp=&entry;break;}if(!stamp)return Fail(ERROR_NOT_ENOUGH_QUOTA,FALSE);*stamp={true,tid,e.serial,*msg,{},e.flags};if(e.flags&4){stamp->pos.hwnd=msg->hwnd;stamp->pos.x=e.rect.left;stamp->pos.y=e.rect.top;stamp->pos.cx=e.rect.right-e.rect.left;stamp->pos.cy=e.rect.bottom-e.rect.top;stamp->pos.flags=e.positionFlags;msg->lParam=LPARAM(&stamp->pos);stamp->msg=*msg;}return TRUE;}return FALSE;}
extern "C" BOOL WINAPI UiPeekMessageA(MSG* m,HWND h,UINT l,UINT r,UINT f){return UiPeekMessageW(m,h,l,r,f);}
extern "C" BOOL WINAPI UiGetMessageW(MSG* m,HWND h,UINT l,UINT r){for(;;){SetLastError(0);if(UiPeekMessageW(m,h,l,r,PM_REMOVE))return m->message!=WM_QUIT;if(GetLastError())return -1;Sleep(1);}}
extern "C" BOOL WINAPI UiGetMessageA(MSG* m,HWND h,UINT l,UINT r){return UiGetMessageW(m,h,l,r);}
extern "C" BOOL WINAPI UiTranslateMessage(const MSG*){return FALSE;/* Native input was translated by the helper. Posted key synthesis is not supported. */}
extern "C" LRESULT WINAPI UiDispatchMessageW(const MSG* m){Guard guard;if(!m||m->message==WM_QUIT)return 0;MSG copy=*m;WINDOWPOS position{};for(auto& s:stamps)if(s.used&&s.tid==GetCurrentThreadId()&&s.msg.hwnd==m->hwnd&&s.msg.message==m->message&&s.msg.time==m->time&&s.msg.wParam==m->wParam&&s.msg.lParam==m->lParam){if(s.flags&4){position=s.pos;copy.lParam=LPARAM(&position);}s.used=false;break;}if(m->message==WM_TIMER){for(auto& t:timers)if(t.h==m->hwnd&&t.id==m->wParam&&t.proc){const auto proc=t.proc;Suspend unlock;proc(m->hwnd,WM_TIMER,m->wParam,m->time);return 0;}}return Invoke(copy.hwnd,copy.message,copy.wParam,copy.lParam);}
extern "C" LRESULT WINAPI UiDispatchMessageA(const MSG* m){return UiDispatchMessageW(m);}
extern "C" BOOL WINAPI UiSetWindowTextW(HWND h,LPCWSTR text){return BOOL(UiSendMessageW(h,WM_SETTEXT,0,LPARAM(text)));}
extern "C" BOOL WINAPI UiSetWindowTextA(HWND h,LPCSTR text){return BOOL(UiSendMessageA(h,WM_SETTEXT,0,LPARAM(text)));}
extern "C" int WINAPI UiGetWindowTextW(HWND h,LPWSTR t,int n){return n>0?int(UiSendMessageW(h,WM_GETTEXT,WPARAM(n),LPARAM(t))):0;}
extern "C" int WINAPI UiGetWindowTextA(HWND h,LPSTR t,int n){return n>0?int(UiSendMessageA(h,WM_GETTEXT,WPARAM(n),LPARAM(t))):0;}
extern "C" int WINAPI UiGetWindowTextLengthW(HWND h){return int(UiSendMessageW(h,WM_GETTEXTLENGTH,0,0));}
extern "C" int WINAPI UiGetWindowTextLengthA(HWND h){return int(UiSendMessageA(h,WM_GETTEXTLENGTH,0,0));}
extern "C" BOOL WINAPI UiSetWindowPos(HWND h,HWND after,int x,int y,int w,int height,UINT flags){Guard guard;if(after&&after!=HWND_TOP)return Fail(ERROR_NOT_SUPPORTED,FALSE);u::Window info;if(!Query(h,info))return FALSE;if(flags&SWP_NOMOVE){x=info.bounds.left;y=info.bounds.top;if(info.parent){u::Window parent;if(!Query(reinterpret_cast<HWND>(info.parent),parent))return FALSE;x-=parent.originX;y-=parent.originY;}}if(flags&SWP_NOSIZE){w=info.bounds.right-info.bounds.left;height=info.bounds.bottom-info.bounds.top;}u::Call c;c.command.op=u::Op::Position;c.command.window=uint64_t(h);c.command.rect={x,y,x+w,y+height};c.command.flags=flags;if(!Call(c))return FALSE;Drain();return TRUE;}
extern "C" BOOL WINAPI UiMoveWindow(HWND h,int x,int y,int w,int height,BOOL repaint){return UiSetWindowPos(h,nullptr,x,y,w,height,SWP_NOZORDER|(repaint?0:SWP_NOREDRAW));}
extern "C" BOOL WINAPI UiGetWindowRect(HWND h,RECT* r){Guard guard;u::Window w;if(!r||!Query(h,w))return FALSE;Copy(r,&w.bounds,sizeof(*r));return TRUE;}
extern "C" BOOL WINAPI UiGetClientRect(HWND h,RECT* r){Guard guard;u::Window w;if(!r||!Query(h,w))return FALSE;Copy(r,&w.client,sizeof(*r));return TRUE;}
extern "C" UINT WINAPI UiGetDpiForWindow(HWND h){Guard guard;u::Window w;return Query(h,w)?w.dpi:0;}
extern "C" DWORD WINAPI UiGetWindowThreadProcessId(HWND h,DWORD* pid){Guard guard;u::Window w;if(!Query(h,w))return 0;if(pid)*pid=w.pid;return w.tid;}
extern "C" BOOL WINAPI UiIsWindow(HWND h){Guard guard;u::Window w;return Query(h,w);}
extern "C" BOOL WINAPI UiIsWindowVisible(HWND h){Guard guard;u::Window w;return Query(h,w)&&w.visible;}
extern "C" BOOL WINAPI UiIsWindowEnabled(HWND h){Guard guard;u::Window w;return Query(h,w)&&w.enabled;}
extern "C" BOOL WINAPI UiIsWindowUnicode(HWND h){Guard guard;auto* w=Local(h);return w&&w->wide;}
extern "C" HWND WINAPI UiGetParent(HWND h){Guard guard;u::Window w;return Query(h,w)?reinterpret_cast<HWND>(w.parent):nullptr;}
extern "C" BOOL WINAPI UiShowWindow(HWND h,int show){Guard guard;
#ifdef GUARDIAN_GDI_EXPANDED
if(show==SW_SHOWDEFAULT)show=SW_SHOWNORMAL;
#endif
if(show!=SW_HIDE&&show!=SW_SHOW&&show!=SW_SHOWNOACTIVATE&&show!=SW_SHOWNORMAL)return Fail(ERROR_NOT_SUPPORTED,FALSE);u::Call c;c.command.op=u::Op::Show;c.command.window=uint64_t(h);c.command.flags=show?1:0;return Call(c)?BOOL(c.reply.value):FALSE;}
extern "C" BOOL WINAPI UiEnableWindow(HWND h,BOOL enable){Guard guard;u::Call c;c.command.op=u::Op::Enable;c.command.window=uint64_t(h);c.command.flags=enable?1:0;return Call(c)?BOOL(c.reply.value):FALSE;}
extern "C" HWND WINAPI UiSetFocus(HWND h){Guard guard;u::Call c;c.command.op=u::Op::Focus;c.command.window=uint64_t(h);return Call(c)?reinterpret_cast<HWND>(c.reply.value):nullptr;}
extern "C" HWND WINAPI UiGetDlgItem(HWND h,int id){Guard guard;for(auto& w:windows)if(w.h&&w.parent==h&&w.id==id)return w.h;return nullptr;}
extern "C" int WINAPI UiGetDlgCtrlID(HWND h){Guard guard;auto* w=Local(h);return w?int(w->id):0;}
extern "C" LONG_PTR WINAPI UiGetWindowLongPtrW(HWND h,int index){Guard guard;auto* w=Local(h);if(!w)return Fail(ERROR_INVALID_WINDOW_HANDLE,LONG_PTR{});
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
if(index>=0){if(!w->cls||uint32_t(index)>w->cls->extra||sizeof(LONG_PTR)>w->cls->extra-uint32_t(index))return Fail(ERROR_INVALID_INDEX,LONG_PTR{});LONG_PTR value=0;CopyMemory(&value,w->extra+index,sizeof(value));return value;}
#endif
if(index==GWLP_USERDATA)return w->user;if(index==GWLP_WNDPROC)return LONG_PTR(w->proc);if(index==GWLP_ID)return w->id;u::Window info;if(!Query(h,info))return 0;if(index==GWL_STYLE)return info.style;if(index==GWL_EXSTYLE)return info.exStyle;if(index==GWLP_HWNDPARENT)return LONG_PTR(info.parent);return Fail(ERROR_NOT_SUPPORTED,LONG_PTR{});}
extern "C" LONG_PTR WINAPI UiGetWindowLongPtrA(HWND h,int index){return UiGetWindowLongPtrW(h,index);}
extern "C" LONG_PTR WINAPI UiSetWindowLongPtrW(HWND h,int index,LONG_PTR value){Guard guard;auto* w=Local(h);if(!w||w->tid!=GetCurrentThreadId())return Fail(ERROR_INVALID_WINDOW_HANDLE,LONG_PTR{});
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
if(index>=0){if(!w->cls||uint32_t(index)>w->cls->extra||sizeof(LONG_PTR)>w->cls->extra-uint32_t(index))return Fail(ERROR_INVALID_INDEX,LONG_PTR{});LONG_PTR old=0;CopyMemory(&old,w->extra+index,sizeof(old));CopyMemory(w->extra+index,&value,sizeof(value));return old;}
#endif
if(index==GWL_STYLE){u::Call c;c.command.op=u::Op::Style;c.command.window=uint64_t(h);c.command.style=uint32_t(value);if(!Call(c))return 0;Drain();return LONG_PTR(c.reply.value);}if(index==GWLP_USERDATA){const auto old=w->user;w->user=value;return old;}if(index==GWLP_WNDPROC&&w->kind==u::Class::Custom){const auto old=w->proc;w->proc=reinterpret_cast<WNDPROC>(value);return LONG_PTR(old);}return Fail(ERROR_NOT_SUPPORTED,LONG_PTR{});}
extern "C" LONG_PTR WINAPI UiSetWindowLongPtrA(HWND h,int index,LONG_PTR v){return UiSetWindowLongPtrW(h,index,v);}
extern "C" LRESULT WINAPI UiCallWindowProcW(WNDPROC proc,HWND h,UINT m,WPARAM w,LPARAM l){return proc?proc(h,m,w,l):UiDefWindowProcW(h,m,w,l);}
extern "C" LRESULT WINAPI UiCallWindowProcA(WNDPROC proc,HWND h,UINT m,WPARAM w,LPARAM l){return UiCallWindowProcW(proc,h,m,w,l);}
extern "C" UINT_PTR WINAPI UiSetTimer(HWND h,UINT_PTR id,UINT ms,TIMERPROC proc){Guard guard;Timer* slot=nullptr;for(auto& t:timers)if(t.h==h&&t.id==id){slot=&t;break;}if(!slot)for(auto& t:timers)if(!t.h){slot=&t;break;}if(!slot)return Fail(ERROR_NOT_ENOUGH_QUOTA,UINT_PTR{});u::Call c;c.command.op=u::Op::Timer;c.command.window=uint64_t(h);c.command.wp=id;c.command.lp=ms;if(!Call(c))return 0;*slot={h,id,proc};return id;}
extern "C" BOOL WINAPI UiKillTimer(HWND h,UINT_PTR id){Guard guard;u::Call c;c.command.op=u::Op::KillTimer;c.command.window=uint64_t(h);c.command.wp=id;if(!Call(c))return FALSE;for(auto& t:timers)if(t.h==h&&t.id==id)t={};return TRUE;}
#ifdef GUARDIAN_GDI_EXPANDED
extern "C" BOOL WINAPI UiInvalidateRect(HWND h,const RECT* rect,BOOL erase){Guard guard;u::Call c;c.command.op=static_cast<u::Op>(nc::Op::Redraw);c.command.window=uint64_t(h);c.command.flags=erase?2u:0u;if(rect){c.command.flags|=1;Copy(&c.command.rect,rect,sizeof(*rect));}return Call(c)?BOOL(c.reply.value):FALSE;}
extern "C" BOOL WINAPI UiUpdateWindow(HWND h){Guard guard;u::Call c;c.command.op=static_cast<u::Op>(nc::Op::Redraw);c.command.window=uint64_t(h);c.command.flags=4;return Call(c)?BOOL(c.reply.value):FALSE;}
#else
extern "C" BOOL WINAPI UiInvalidateRect(HWND h,const RECT* rect,BOOL){Guard guard;if(rect)return Fail(ERROR_NOT_SUPPORTED,FALSE);u::Call c;c.command.op=u::Op::Invalidate;c.command.window=uint64_t(h);return Call(c);}
extern "C" BOOL WINAPI UiUpdateWindow(HWND h){return UiInvalidateRect(h,nullptr,TRUE);}
#endif

extern "C" HWND WINAPI UiGetFocus(){Guard guard;u::Call c;c.command.op=u::Op::Focus;c.command.flags=1;return Call(c)?reinterpret_cast<HWND>(c.reply.value):nullptr;}
extern "C" HWND WINAPI UiSetParent(HWND h,HWND parent){Guard guard;u::Call c;c.command.op=u::Op::Parent;c.command.window=uint64_t(h);c.command.other=uint64_t(parent);if(!Call(c))return nullptr;auto* local=Local(h);if(local)local->parent=parent;Drain();return reinterpret_cast<HWND>(c.reply.value);}
extern "C" int WINAPI UiMapWindowPoints(HWND from,HWND to,POINT* points,UINT count){Guard guard;if(!points||count>256)return Fail(ERROR_INVALID_PARAMETER,0);u::Window a{},b{};if((from&&!Query(from,a))||(to&&!Query(to,b)))return 0;const auto x=a.originX-b.originX,y=a.originY-b.originY;for(UINT i=0;i<count;++i){points[i].x+=x;points[i].y+=y;}return int(MAKELONG(WORD(x),WORD(y)));}
extern "C" BOOL WINAPI UiClientToScreen(HWND h,POINT* point){if(!h||!point)return Fail(ERROR_INVALID_PARAMETER,FALSE);SetLastError(0);UiMapWindowPoints(h,nullptr,point,1);return GetLastError()==0;}
extern "C" BOOL WINAPI UiScreenToClient(HWND h,POINT* point){if(!h||!point)return Fail(ERROR_INVALID_PARAMETER,FALSE);SetLastError(0);UiMapWindowPoints(nullptr,h,point,1);return GetLastError()==0;}

#ifdef GUARDIAN_NATIVE_GDI
#ifdef GUARDIAN_GDI_EXPANDED
#define UiCreatePen UiLegacyCreatePen
#define UiBitBlt UiLegacyBitBlt
#define UiCreateDIBSection UiLegacyCreateDIBSection
#define UiStretchDIBits UiLegacyStretchDIBits
#define UiDeleteObject UiLegacyDeleteObject
#endif
#include "NativeGdiUser.inl"
#ifdef GUARDIAN_GDI_EXPANDED
#undef UiCreatePen
#undef UiBitBlt
#undef UiCreateDIBSection
#undef UiStretchDIBits
#undef UiDeleteObject
#endif
#endif
#ifdef GUARDIAN_NATIVE_CONTROLS
#ifdef GUARDIAN_GDI_EXPANDED
extern "C" BOOL WINAPI UiDeleteObject(HGDIOBJ);
#endif
#include "NativeControlsUser.inl"
#ifdef GUARDIAN_GDI_EXPANDED
#include "ExpandedGdiUser.inl"
#endif
#endif
