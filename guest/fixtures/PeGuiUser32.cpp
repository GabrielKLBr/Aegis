// A bounded USER32 subset executed entirely in guest CPL3, without host callbacks.
#define _USER32_
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include "guardian/Graphics.h"
#include "guardian/Runtime.h"
namespace gg=guardian::graphics;
struct Packet {gg::Command command;gg::Event event;};
extern "C" uint32_t GuardianGuiCall(Packet*) noexcept;
extern "C" uint32_t GuardianRuntimeCall(guardian::runtime::Command*) noexcept;
namespace {
uint32_t (*volatile guiCall)(Packet*) noexcept=GuardianGuiCall;
struct Class {WNDCLASSA value{};char name[32]{};};
struct Window {HWND handle=nullptr;WNDPROC proc=nullptr;int width=0,height=0;bool paint=false,painting=false,destroying=false;};
Class classes[4];Window windows[4];MSG queue[16];uint32_t head=0,count=0;
bool quit=false;int quitCode=0;
void Error(DWORD value){__writegsdword(0x68,value);}
bool Name(char* out,const char* text){
    if(!text||reinterpret_cast<uintptr_t>(text)<65536){Error(ERROR_CALL_NOT_IMPLEMENTED);return false;}
    for(uint32_t i=0;i<32;++i){const auto c=static_cast<unsigned char>(text[i]);if(!c){out[i]=0;return true;}if(c<32||c>126){Error(ERROR_INVALID_PARAMETER);return false;}out[i]=char(c);}
    Error(ERROR_INVALID_PARAMETER);return false;
}
bool Equal(const char* a,const char* b){for(uint32_t i=0;i<32;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
Window* Find(HWND h){for(auto& w:windows)if(w.handle&&w.handle==h)return &w;return nullptr;}
bool Call(Packet& p){const auto r=guiCall(&p);if(!r)return true;Error(r==uint32_t(gg::Status::Full)?ERROR_NOT_ENOUGH_QUOTA:ERROR_INVALID_PARAMETER);return false;}
bool Present(){Packet p{};p.command.op=gg::Op::Present;return Call(p);}
bool Push(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(count==16){Error(ERROR_NOT_ENOUGH_QUOTA);return false;}
    auto& message=queue[(head+count)%16];message={};message.hwnd=h;message.message=m;message.wParam=wp;message.lParam=lp;++count;return true;
}
void Input(){
    Packet p{};p.command.op=gg::Op::Poll;
    const auto r=GuardianGuiCall(&p);if(r==uint32_t(gg::Status::Empty))return;
    if(r){Error(ERROR_INVALID_DATA);return;}
    const auto& e=p.event;const auto h=reinterpret_cast<HWND>(static_cast<uintptr_t>(e.target));
    // The existing device carries translated ASCII text, not physical keys.
    if(e.type==gg::Input::Key)Push(h,WM_CHAR,e.key,1);
    else if(e.type==gg::Input::Button||e.type==gg::Input::Move){
        if(e.x<0||e.y<0)return;
        const auto pos=LPARAM(uint32_t(uint16_t(e.x))|(uint32_t(uint16_t(e.y))<<16));
        Push(h,e.type==gg::Input::Move?WM_MOUSEMOVE:(e.buttons?WM_LBUTTONDOWN:WM_LBUTTONUP),e.buttons?MK_LBUTTON:0,pos);
    }else if(e.type==gg::Input::Close){for(const auto& w:windows)if(w.handle){Push(w.handle,WM_CLOSE,0,0);break;}}
}
}
extern "C" int DllEntry(void*,uint32_t,void*){return 1;}
extern "C" ATOM WINAPI RegisterClassA(const WNDCLASSA* p){
    if(!p||!p->lpfnWndProc||p->style||p->cbClsExtra||p->cbWndExtra||p->hIcon||p->hCursor||p->hbrBackground||p->lpszMenuName){Error(ERROR_CALL_NOT_IMPLEMENTED);return 0;}
    char name[32]{};if(!Name(name,p->lpszClassName)||!name[0])return 0;
    for(const auto& c:classes)if(c.value.lpfnWndProc&&Equal(c.name,name)){Error(ERROR_CLASS_ALREADY_EXISTS);return 0;}
    for(uint32_t i=0;i<4;++i)if(!classes[i].value.lpfnWndProc){classes[i].value=*p;for(uint32_t j=0;j<32;++j)classes[i].name[j]=name[j];return ATOM(i+1);}
    Error(ERROR_NOT_ENOUGH_QUOTA);return 0;
}
extern "C" HWND WINAPI CreateWindowExA(DWORD ex,const char* cls,const char* title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,void* param){
    if(ex||style!=(WS_CAPTION|WS_VISIBLE)||parent||menu||param){Error(ERROR_CALL_NOT_IMPLEMENTED);return nullptr;}
    char name[32]{},caption[32]{};if(!Name(name,cls)||!Name(caption,title))return nullptr;
    Class* found=nullptr;for(auto& c:classes)if(c.value.lpfnWndProc&&Equal(c.name,name)&&c.value.hInstance==instance)found=&c;
    if(!found){Error(ERROR_CANNOT_FIND_WND_CLASS);return nullptr;}
    Window* w=nullptr;for(auto& candidate:windows)if(!candidate.handle){w=&candidate;break;}
    if(!w){Error(ERROR_NOT_ENOUGH_QUOTA);return nullptr;}
    if(x<0||y<0||width<40||height<36||width>320||height>240||x>320-width||y>240-height){Error(ERROR_INVALID_PARAMETER);return nullptr;}
    Packet p{};p.command.op=gg::Op::Create;p.command.x=x;p.command.y=y;p.command.w=uint32_t(width);p.command.h=uint32_t(height);
    for(uint32_t i=0;caption[i];++i)p.command.data[p.command.count++]=uint8_t(caption[i]);
    if(!Call(p))return nullptr;
    *w={reinterpret_cast<HWND>(uintptr_t(p.command.handle)),found->value.lpfnWndProc,width,height,true,false,false};
    const auto h=w->handle;
    CREATESTRUCTA cs{param,instance,menu,parent,height,width,y,x,LONG(style),title,cls,ex};
    if(!w->proc(h,WM_NCCREATE,0,reinterpret_cast<LPARAM>(&cs))||w->proc(h,WM_CREATE,0,reinterpret_cast<LPARAM>(&cs))==-1){DestroyWindow(h);return nullptr;}
    if(!Present()){DestroyWindow(h);return nullptr;}
    return h;
}
extern "C" BOOL WINAPI DestroyWindow(HWND h){
    auto* w=Find(h);if(!w||w->destroying){Error(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
    w->destroying=true;w->proc(h,WM_DESTROY,0,0);w->proc(h,WM_NCDESTROY,0,0);
    Packet p{};p.command.op=gg::Op::Destroy;p.command.handle=uint32_t(reinterpret_cast<uintptr_t>(h));
    if(!Call(p)){w->destroying=false;return FALSE;}*w={};return Present()?TRUE:FALSE;
}
extern "C" LRESULT WINAPI DefWindowProcA(HWND h,UINT message,WPARAM,LPARAM){
    if(message==WM_NCCREATE)return TRUE;
    if(message==WM_CLOSE){DestroyWindow(h);return 0;}
    return 0;
}
extern "C" BOOL WINAPI ShowWindow(HWND h,int command){if(!Find(h)){Error(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}if(command!=SW_SHOW&&command!=SW_SHOWNORMAL){Error(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}return TRUE;}
extern "C" BOOL WINAPI InvalidateRect(HWND h,const RECT* rect,BOOL erase){
    auto* w=Find(h);if(!w){Error(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
    if(rect||erase){Error(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}w->paint=true;return TRUE;
}
extern "C" BOOL WINAPI UpdateWindow(HWND h){auto* w=Find(h);if(!w){Error(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}if(w->paint)w->proc(h,WM_PAINT,0,0);return TRUE;}
extern "C" BOOL WINAPI GetClientRect(HWND h,RECT* rect){auto* w=Find(h);if(!w||!rect){Error(ERROR_INVALID_PARAMETER);return FALSE;}*rect={0,0,w->width,w->height-18};return TRUE;}
extern "C" HDC WINAPI BeginPaint(HWND h,PAINTSTRUCT* ps){
    auto* w=Find(h);if(!w||!ps||w->painting){Error(ERROR_INVALID_PARAMETER);return nullptr;}
    w->painting=true;w->paint=false;*ps={};ps->hdc=reinterpret_cast<HDC>(reinterpret_cast<uintptr_t>(h)+0x10000);GetClientRect(h,&ps->rcPaint);return ps->hdc;
}
extern "C" BOOL WINAPI EndPaint(HWND h,const PAINTSTRUCT* ps){
    auto* w=Find(h);if(!w||!ps||!w->painting||reinterpret_cast<uintptr_t>(ps->hdc)!=reinterpret_cast<uintptr_t>(h)+0x10000){Error(ERROR_INVALID_PARAMETER);return FALSE;}
    w->painting=false;return Present()?TRUE:FALSE;
}
extern "C" HBRUSH WINAPI GetSysColorBrush(int index){if(index!=COLOR_WINDOW&&index!=COLOR_HIGHLIGHT){Error(ERROR_CALL_NOT_IMPLEMENTED);return nullptr;}return reinterpret_cast<HBRUSH>(uintptr_t(index+1));}
extern "C" int WINAPI FillRect(HDC dc,const RECT* rect,HBRUSH brush){
    const auto id=reinterpret_cast<uintptr_t>(dc);if(id<0x10000){Error(ERROR_INVALID_HANDLE);return 0;}
    auto* w=Find(reinterpret_cast<HWND>(id-0x10000));const auto color=reinterpret_cast<uintptr_t>(brush);
    if(!w||!w->painting||!rect){Error(ERROR_INVALID_PARAMETER);return 0;}
    if(color!=COLOR_WINDOW+1&&color!=COLOR_HIGHLIGHT+1){Error(ERROR_CALL_NOT_IMPLEMENTED);return 0;}
    const int64_t width=int64_t(rect->right)-rect->left,height=int64_t(rect->bottom)-rect->top;
    if(width<=0||height<=0||width>1024||height>1024){Error(ERROR_INVALID_PARAMETER);return 0;}
    Packet p{};p.command.op=gg::Op::Fill;p.command.handle=uint32_t(id-0x10000);p.command.x=rect->left;p.command.y=rect->top;p.command.w=uint32_t(width);p.command.h=uint32_t(height);p.command.value=color==COLOR_WINDOW+1?0xFFFFFF:0x0078D7;
    if(!rect->left&&!rect->top&&rect->right==w->width&&rect->bottom==w->height-18)p.command.h=uint32_t(w->height);
    return Call(p)?1:0;
}
extern "C" BOOL WINAPI PeekMessageA(MSG* message,HWND h,UINT first,UINT last,UINT remove){
    if(!message){Error(ERROR_INVALID_PARAMETER);return FALSE;}
    if(h||first||last||remove>1){Error(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
    if(quit){*message={};message->message=WM_QUIT;message->wParam=WPARAM(quitCode);return TRUE;}
    if(!count)Input();
    if(count){*message=queue[head];if(remove){head=(head+1)%16;--count;}return TRUE;}
    for(const auto& w:windows)if(w.handle&&w.paint){*message={};message->hwnd=w.handle;message->message=WM_PAINT;return TRUE;}
    return FALSE;
}
extern "C" BOOL WINAPI GetMessageA(MSG* message,HWND h,UINT first,UINT last){
    if(!message||h||first||last){Error(ERROR_CALL_NOT_IMPLEMENTED);return -1;}
    for(;;){if(PeekMessageA(message,nullptr,0,0,PM_REMOVE))return message->message==WM_QUIT?0:1;
        guardian::runtime::Command c{};c.op=guardian::runtime::Op::Delay;c.argument=8;if(GuardianRuntimeCall(&c)){Error(ERROR_INVALID_DATA);return -1;}}
}
extern "C" BOOL WINAPI TranslateMessage(const MSG* message){
    if(!message){Error(ERROR_INVALID_PARAMETER);return FALSE;}
    if(message->message!=WM_KEYDOWN)return FALSE;
    auto key=message->wParam;
    if(key>='A'&&key<='Z')key+='a'-'A';
    else if(!((key>='0'&&key<='9')||key==8||key==9||key==13||key==27||key==32)){Error(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
    return Push(message->hwnd,WM_CHAR,key,message->lParam);
}
extern "C" LRESULT WINAPI DispatchMessageA(const MSG* message){if(!message){Error(ERROR_INVALID_PARAMETER);return 0;}auto* w=Find(message->hwnd);if(!w){Error(ERROR_INVALID_WINDOW_HANDLE);return 0;}return w->proc(w->handle,message->message,message->wParam,message->lParam);}
extern "C" void WINAPI PostQuitMessage(int code){quit=true;quitCode=code;}
extern "C" int WINAPI GetSystemMetrics(int index){if(index==SM_CXSCREEN)return 320;if(index==SM_CYSCREEN)return 240;Error(ERROR_CALL_NOT_IMPLEMENTED);return 0;}
