// This PE is supplied as a separate file at boot; it is never embedded in the kernel.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#ifdef GUARDIAN_GUI_GUEST_TEST
#include "guardian/Graphics.h"
#include "guardian/NtCompatibility.h"
extern "C" uint32_t GuardianGuiCall(void*) noexcept;
extern "C" uint32_t GuardianCompatibilityCall(void*) noexcept;
#endif
namespace {
bool key=false,click=false,painted=false;int error=0;
LRESULT CALLBACK Procedure(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_NCCREATE)return TRUE;
    if(m==WM_PAINT){PAINTSTRUCT ps{};const auto dc=BeginPaint(h,&ps);if(!dc){error=10;return 0;}
        RECT client{};GetClientRect(h,&client);
        if(!FillRect(dc,&client,GetSysColorBrush(COLOR_WINDOW)))error=11;
        RECT box{20,30,key?100:60,click?90:60};
        if(!FillRect(dc,&box,GetSysColorBrush(COLOR_HIGHLIGHT)))error=12;
        if(!EndPaint(h,&ps))error=13;painted=true;return 0;}
    if(m==WM_CHAR){key=wp=='G';InvalidateRect(h,nullptr,FALSE);UpdateWindow(h);return 0;}
    if(m==WM_LBUTTONDOWN){click=true;InvalidateRect(h,nullptr,FALSE);UpdateWindow(h);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcA(h,m,wp,lp);
}
// A real address-bearing class binding also exercises the loader's DIR64 fixup.
WNDPROC volatile windowProcedure=Procedure;
}
extern "C" int GuiEntry(){
    const char* title="PE GUI";
#ifdef GUARDIAN_GUI_GUEST_TEST
    guardian::nt::Query version{};const auto versionStatus=GuardianCompatibilityCall(&version);
    if(!versionStatus){
        const auto t=guardian::nt::Find(version.target.id);
        if(!guardian::nt::Matches(t,version.target.major,version.target.minor,version.target.build))return 23;
        if(version.reported.major!=6||version.reported.minor!=(t.id==2?1u:2u))return 24;
        title=t.id==1?"PE NT 19045":t.id==2?"PE NT 7601":"PE NT 22631";
        version.manifest=2;if(GuardianCompatibilityCall(&version)||version.reported.build!=t.build)return 25;
        version.manifest=1;if(GuardianCompatibilityCall(&version)||version.reported.minor!=(t.id==2?1u:3u))return 26;
        version.manifest=3;if(GuardianCompatibilityCall(&version)!=0xC000000Du)return 27;
        if(GuardianCompatibilityCall(nullptr)!=0xC0000022u||GuardianCompatibilityCall(reinterpret_cast<void*>(UINT64_MAX-8))!=0xC0000022u)return 28;
    }else if(versionStatus!=0xC00000BBu)return 29;
    if(GuardianGuiCall(nullptr)!=0xC0000022u||GuardianGuiCall(reinterpret_cast<void*>(0x1F0000))!=0xC0000022u||GuardianGuiCall(reinterpret_cast<void*>(UINT64_MAX-32))!=0xC0000022u)return 20;
    struct Packet {guardian::graphics::Command command;guardian::graphics::Event event;};
    Packet denied{};denied.command.op=guardian::graphics::Op::Bridge;denied.command.value=uint32_t(guardian::graphics::Bridge::HostWindow);
    if(GuardianGuiCall(&denied)!=uint32_t(guardian::graphics::Status::Denied))return 21;
    Packet forged{};forged.command.op=guardian::graphics::Op::Destroy;forged.command.handle=UINT32_MAX;
    if(GuardianGuiCall(&forged)!=uint32_t(guardian::graphics::Status::Invalid))return 22;
#endif
    WNDCLASSA c{};c.lpfnWndProc=windowProcedure;c.lpszClassName="AegisPeGui";
    if(!RegisterClassA(&c))return 1;
    const auto h=CreateWindowExA(0,c.lpszClassName,title,WS_CAPTION|WS_VISIBLE,12,12,200,160,nullptr,nullptr,nullptr,nullptr);
    if(!h)return 2;
    if(GetSystemMetrics(SM_CXSCREEN)<200||GetSystemMetrics(SM_CYSCREEN)<160)return 3;
    if(!UpdateWindow(h))return 4;
#ifdef GUARDIAN_GUI_NATIVE_ORACLE
    PostMessageA(h,WM_CHAR,'G',1);PostMessageA(h,WM_LBUTTONDOWN,MK_LBUTTON,0);PostMessageA(h,WM_CLOSE,0,0);
#endif
    MSG message{};int result=0;
    while((result=GetMessageA(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageA(&message);}
    return result<0?5:(error?error:(!painted?6:(!key?7:(!click?8:0))));
}
