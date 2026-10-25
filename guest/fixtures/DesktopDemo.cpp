#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <dwmapi.h>
static HWND root=nullptr,child=nullptr;static int keyDowns=0,keyUps=0,characters=0,paints=0,timers=0;static bool failed=false;static HBRUSH brush=nullptr;static HFONT font=nullptr;
#define Check(ok) do {if(!(ok)){failed=true;std::printf("Check failed at %d: %s, error=%lu\n",__LINE__,#ok,GetLastError());}} while(0)
static LRESULT CALLBACK Window(HWND h,UINT id,WPARAM wp,LPARAM lp){
    switch(id){case WM_PAINT:{PAINTSTRUCT p{};const auto dc=BeginPaint(h,&p);Check(dc!=nullptr);if(!dc)return 0;RECT r{};Check(GetClientRect(h,&r)!=0);Check(FillRect(dc,&r,brush)!=0);const auto previous=SelectObject(dc,font);Check(previous!=nullptr);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,240,240));const wchar_t text[]=L"MiniNT GUI 14.7";Check(TextOutW(dc,12,12,text,14)!=0);SIZE size{};Check(GetTextExtentPoint32W(dc,text,14,&size)!=0&&size.cx>0&&size.cy>=18&&size.cy<=24);
#ifndef DESKTOP_REFERENCE
Check(!DeleteObject(font));
#endif
Check(SelectObject(dc,previous)==font);Check(EndPaint(h,&p)!=0);++paints;return 0;}
    case WM_KEYDOWN:if(wp=='A'){++keyDowns;Check((lp&(1ll<<31))==0&&((lp>>16)&255)==30);InvalidateRect(h,nullptr,TRUE);}return 0;
    case WM_KEYUP:if(wp=='A'){++keyUps;Check((lp&(1ll<<31))!=0);}return 0;
    case WM_CHAR:if(wp=='A'||wp==0xE9)++characters;return 0;
    case WM_TIMER:++timers;if(timers==2){Check(MoveWindow(root,90,80,460,300,TRUE)!=0);Check(MoveWindow(child,24,80,220,90,TRUE)!=0);}return 0;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:if(h==root)PostQuitMessage(0);return 0;
    default:return DefWindowProcW(h,id,wp,lp);}
}
static LRESULT CALLBACK SelfDestroy(HWND h,UINT message,WPARAM wp,LPARAM lp){if(message==WM_CREATE){DestroyWindow(h);return 0;}return DefWindowProcW(h,message,wp,lp);}
int main(){WNDCLASSW temporary{};temporary.lpfnWndProc=SelfDestroy;temporary.hInstance=GetModuleHandleW(nullptr);temporary.lpszClassName=L"Guardian.SelfDestroy";Check(RegisterClassW(&temporary)!=0);Check(CreateWindowExW(0,temporary.lpszClassName,L"temporary",WS_POPUP,0,0,20,20,nullptr,nullptr,temporary.hInstance,nullptr)==nullptr);Check(UnregisterClassW(temporary.lpszClassName,temporary.hInstance)!=0);brush=CreateSolidBrush(RGB(28,67,110));font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Arial");Check(brush&&font);WNDCLASSW c{};c.style=CS_HREDRAW|CS_VREDRAW;c.lpfnWndProc=Window;c.hInstance=GetModuleHandleW(nullptr);c.hbrBackground=GetSysColorBrush(COLOR_WINDOW);c.lpszClassName=L"Guardian.Desktop.Demo";Check(RegisterClassW(&c)!=0);root=CreateWindowExW(0,c.lpszClassName,L"GUI 14.7",WS_OVERLAPPEDWINDOW|WS_VISIBLE,60,50,420,260,nullptr,nullptr,c.hInstance,nullptr);Check(root!=nullptr);child=CreateWindowExW(0,c.lpszClassName,L"Child",WS_CHILD|WS_VISIBLE,20,70,180,80,root,reinterpret_cast<HMENU>(1),c.hInstance,nullptr);Check(child!=nullptr);Check(GetParent(child)==root&&IsChild(root,child));POINT origin{};Check(ClientToScreen(child,&origin)!=0);Check(ScreenToClient(child,&origin)!=0&&origin.x==0&&origin.y==0);Check(GetDpiForWindow(root)==96);Check(SetFocus(root)==root||GetFocus()==root);BOOL dark=TRUE;Check(SUCCEEDED(DwmSetWindowAttribute(root,20,&dark,sizeof(dark))));Check(SetTimer(root,1,40,nullptr)==1);Check(UpdateWindow(root)!=0);Check(UpdateWindow(child)!=0);
#ifdef DESKTOP_REFERENCE
    MSG physical{};physical.hwnd=root;physical.message=WM_KEYDOWN;physical.wParam='A';physical.lParam=1|(30<<16);TranslateMessage(&physical);DispatchMessageW(&physical);PostMessageW(root,WM_KEYUP,'A',LPARAM(1ull|(30ull<<16)|(1ull<<30)|(1ull<<31)));PostMessageW(root,WM_CHAR,0xE9,1);
#endif
    const auto begin=GetTickCount64();MSG m{};while(GetTickCount64()-begin<2500){if(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)break;TranslateMessage(&m);DispatchMessageW(&m);}else Sleep(1);
#ifdef DESKTOP_REFERENCE
        if(timers>=3&&IsWindow(root))DestroyWindow(root);
#endif
    }
    if(IsWindow(root))DestroyWindow(root);Check(paints>=2&&timers>=2);
#ifndef DESKTOP_REFERENCE
    Check(keyDowns>=1&&keyUps>=1&&characters>=2);
#endif
    Check(UnregisterClassW(c.lpszClassName,c.hInstance)!=0);Check(DeleteObject(brush)!=0&&DeleteObject(font)!=0);std::printf("Desktop demo paint=%d timers=%d down=%d up=%d chars=%d %s\n",paints,timers,keyDowns,keyUps,characters,failed?"FAIL":"PASS");return failed?1:0;
}
