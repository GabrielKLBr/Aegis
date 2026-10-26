#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#ifndef INTEGRATED_MODE
#define INTEGRATED_MODE 0
#endif
namespace {
HWND root=nullptr,peer=nullptr,child=nullptr;unsigned keys=0,ups=0,characters=0,clicks=0,moves=0,sizes=0,focus=0,paints=0;bool modal=false,ownerRestored=false,failed=false;
void Check(bool yes,const char* label){if(!yes){failed=true;std::printf("12F FAIL %s error=%lu\n",label,GetLastError());}}
LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(h,&paint);if(!dc)return 0;RECT rect{};GetClientRect(h,&rect);const auto brush=CreateSolidBrush(h==child?RGB(30,160,70):h==peer?RGB(170,70,30):RGB(25,65,120));FillRect(dc,&rect,brush);DeleteObject(brush);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);const wchar_t text[]=L"MiniNT 12F - janelas independentes";TextOutW(dc,8,8,text,int(sizeof(text)/sizeof(wchar_t))-1);EndPaint(h,&paint);++paints;return 0;}
    if(message==WM_SETFOCUS){++focus;return 0;}
    if(message==WM_MOVE){++moves;return 0;}
    if(message==WM_SIZE){++sizes;InvalidateRect(h,nullptr,TRUE);return 0;}
    if(message==WM_LBUTTONDOWN){++clicks;return 0;}
    if(message==WM_CHAR){if(wp=='A'||wp=='a'||wp==0xE9)++characters;return 0;}
    if(message==WM_KEYDOWN){if(wp=='A'){++keys;Check(((uint64_t(lp)>>16)&255)==30,"physical scan code");}if(wp=='M'&&!modal){modal=true;Check(MessageBoxW(root,L"Dialogo executando dentro do MiniNT. Enter para fechar.",L"12F Modal",MB_OK)==IDOK,"modal result");ownerRestored=IsWindowEnabled(root)!=FALSE;Check(ownerRestored,"owner enabled after modal");}return 0;}
    if(message==WM_KEYUP){if(wp=='A')++ups;return 0;}
    if(message==WM_CLOSE){DestroyWindow(h);return 0;}
    if(message==WM_DESTROY){if(h==root)PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,message,wp,lp);
}
}
int main(){WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.lpszClassName=L"Guardian.Integrated.12F";cls.hbrBackground=GetSysColorBrush(COLOR_WINDOW);Check(RegisterClassW(&cls)!=0,"register");root=CreateWindowExW(0,cls.lpszClassName,L"12F Main",WS_OVERLAPPEDWINDOW|WS_VISIBLE,60,50,420,260,nullptr,nullptr,nullptr,nullptr);peer=CreateWindowExW(0,cls.lpszClassName,L"12F Peer",WS_OVERLAPPEDWINDOW|WS_VISIBLE,120,110,280,180,nullptr,nullptr,nullptr,nullptr);child=CreateWindowExW(0,cls.lpszClassName,L"12F Child",WS_CHILD|WS_VISIBLE,20,60,160,70,root,nullptr,nullptr,nullptr);Check(root&&peer&&child,"independent windows and child");Check(GetDpiForWindow(root)==96,"guest logical DPI");UpdateWindow(root);UpdateWindow(peer);UpdateWindow(child);SetFocus(root);const auto start=GetTickCount64();MSG message{};
    while(GetTickCount64()-start<20000){if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)break;TranslateMessage(&message);DispatchMessageW(&message);}else Sleep(1);
#if INTEGRATED_MODE == 1
        if(GetTickCount64()-start>700){*reinterpret_cast<volatile unsigned*>(uintptr_t(0))=1;}
#endif
    }
    if(IsWindow(peer))DestroyWindow(peer);if(IsWindow(root))DestroyWindow(root);
#if INTEGRATED_MODE == 0
    Check(keys>=1&&ups>=1&&characters>=2&&clicks>=1&&moves>=1&&sizes>=2&&focus>=1&&paints>=3&&modal&&ownerRestored,"window input, geometry, paints and modal corpus");
#endif
    Check(UnregisterClassW(cls.lpszClassName,nullptr)!=0,"unregister");std::printf("Integrated windows keys=%u up=%u chars=%u clicks=%u moves=%u sizes=%u paints=%u modal=%u %s\n",keys,ups,characters,clicks,moves,sizes,paints,unsigned(modal),failed?"FAIL":"PASS");return failed?83:0;
}
