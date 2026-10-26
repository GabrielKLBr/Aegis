#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cwchar>
#include <cstdint>
namespace {
HDC canvas=nullptr;int paints=0;bool bad=false;
bool Check(bool ok,const char* text){if(!ok)std::printf("GDI FAIL %s error=%lu\n",text,GetLastError());return ok;}
LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM w,LPARAM l){if(msg==WM_PAINT){PAINTSTRUCT ps{};const auto dc=BeginPaint(h,&ps);if(!dc)bad=true;else{if(canvas&&!BitBlt(dc,0,0,160,112,canvas,0,0,SRCCOPY))bad=true;if(!EndPaint(h,&ps))bad=true;++paints;}return 0;}return DefWindowProcW(h,msg,w,l);}
void Pump(){MSG msg{};for(int i=0;i<32&&PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);++i)DispatchMessageW(&msg);}
}
int main(){WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.lpszClassName=L"Guardian.Gdi.StandardSdk";cls.hInstance=GetModuleHandleW(nullptr);if(!RegisterClassW(&cls))return 1;
    const auto window=CreateWindowExW(0,cls.lpszClassName,L"Native GDI corpus",WS_OVERLAPPEDWINDOW|WS_VISIBLE,140,140,240,200,nullptr,nullptr,cls.hInstance,nullptr);if(!window)return 2;
volatile int mode=NATIVE_GDI_MODE;if(mode==1){*static_cast<volatile int*>(nullptr)=1;return 90;}if(mode==2){for(;;)Sleep(1);}
    const auto render=canvas=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=160;info.bmiHeader.biHeight=-112;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
    const auto bitmap=CreateDIBSection(canvas,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!Check(canvas&&bitmap&&pixels,"DIB and memory DC"))return 3;const auto oldBitmap=SelectObject(canvas,bitmap);const auto white=CreateSolidBrush(RGB(255,255,255)),blue=CreateSolidBrush(RGB(25,100,210)),red=CreateSolidBrush(RGB(220,20,30));RECT all{0,0,160,112};if(!Check(FillRect(canvas,&all,white)!=0,"white fill"))return 4;
    const auto level=SaveDC(canvas);IntersectClipRect(canvas,0,0,50,35);RECT block{0,0,80,60};FillRect(canvas,&block,blue);if(!Check(RestoreDC(canvas,level)!=FALSE&&GetPixel(canvas,20,20)==RGB(25,100,210)&&GetPixel(canvas,60,20)==RGB(255,255,255),"clipping and restore"))return 5;
    const auto region=CreateRectRgn(80,0,110,30);SelectClipRgn(canvas,region);FillRect(canvas,&all,red);SelectClipRgn(canvas,nullptr);DeleteObject(region);
    const auto pen=CreatePen(PS_SOLID,2,RGB(0,120,20));const auto oldPen=SelectObject(canvas,pen);MoveToEx(canvas,5,36,nullptr);LineTo(canvas,130,36);const auto oldBrush=SelectObject(canvas,blue);Rectangle(canvas,115,2,150,28);SelectObject(canvas,oldBrush);SelectObject(canvas,oldPen);if(!Check(GetStockObject(WHITE_BRUSH)==oldBrush,"stock object identity"))return 11;
    LOGFONTW font{};font.lfHeight=-16;font.lfWeight=400;font.lfCharSet=DEFAULT_CHARSET;font.lfQuality=NONANTIALIASED_QUALITY;wcscpy_s(font.lfFaceName,L"Consolas");const auto handle=CreateFontIndirectW(&font);if(!Check(handle!=nullptr,"allowed native font"))return 6;const auto oldFont=SelectObject(canvas,handle);if(!Check(GetCurrentObject(canvas,OBJ_FONT)==handle&&GetDeviceCaps(canvas,LOGPIXELSX)==96&&GetDeviceCaps(canvas,BITSPIXEL)==32,"font selection and logical device caps"))return 12;SetTextColor(canvas,RGB(20,30,40));SetBkColor(canvas,RGB(255,255,255));SetBkMode(canvas,TRANSPARENT);SIZE extent{};TEXTMETRICW metrics{};wchar_t face[32]{};GetTextExtentPoint32W(canvas,L"MiniNT GDI",10,&extent);GetTextMetricsW(canvas,&metrics);GetTextFaceW(canvas,32,face);TextOutW(canvas,2,40,L"MiniNT GDI",10);RECT text{2,60,158,85},calculated=text;DrawTextW(canvas,L"Windows text",-1,&calculated,DT_SINGLELINE|DT_CALCRECT|DT_NOPREFIX);DrawTextW(canvas,L"Windows text",-1,&text,DT_SINGLELINE|DT_NOPREFIX);
    BITMAPINFO sourceInfo=info;sourceInfo.bmiHeader.biWidth=16;sourceInfo.bmiHeader.biHeight=-16;uint32_t source[256]{};for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)source[y*16+x]=((x^y)&1)?0x003366CC:0x00EEBB22;
    if(!Check(StretchDIBits(canvas,100,86,32,24,0,0,16,16,source,&sourceInfo,DIB_RGB_COLORS,SRCCOPY)>0,"native image scaling"))return 7;
    if(!Check(GdiFlush()!=FALSE,"flush and guest DIB coherence"))return 8;uint64_t hash=14695981039346656037ull;for(unsigned i=0;i<160*112;++i){hash^=static_cast<uint32_t*>(pixels)[i];hash*=1099511628211ull;}
    std::printf("GDI_RESULT hash=%016llx extent=%ld,%ld metrics=%ld,%ld,%ld,%ld rect=%ld,%ld face=",static_cast<unsigned long long>(hash),extent.cx,extent.cy,metrics.tmHeight,metrics.tmAscent,metrics.tmDescent,metrics.tmAveCharWidth,calculated.right,calculated.bottom);for(auto c:face){if(!c)break;std::putchar(int(c));}std::puts("");
    InvalidateRect(window,nullptr,TRUE);UpdateWindow(window);Pump();MoveWindow(window,150,150,320,240,TRUE);Pump();if(!Check(paints>=2&&!bad,"BeginPaint, BitBlt, EndPaint and resize"))return 9;
    const auto windowDc=GetDC(window);if(!Check(windowDc&&ReleaseDC(window,windowDc),"window DC lease"))return 13;
    canvas=nullptr;SelectObject(render,oldFont);SelectObject(render,oldBitmap);if(!Check(DeleteObject(bitmap)&&DeleteObject(handle)&&DeleteObject(pen)&&DeleteObject(white)&&DeleteObject(blue)&&DeleteObject(red)&&DeleteDC(render)&&DestroyWindow(window),"GDI ownership cleanup"))return 10;
    std::puts("Native GDI SDK drawing and paint lifecycle PASS");return 0;
}
