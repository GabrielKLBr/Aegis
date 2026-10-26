#include "NativeGuiCore.h"
#include "NativeGuiRestrictions.h"
#include "NativeGuiPipe.h"
#include <Windows.h>
#include <sddl.h>
#include <array>
#include <string>
#include <vector>
namespace n=guardian::nativegui;
namespace {
constexpr wchar_t WindowClass[]=L"Aegis.NativeGui.14.10A";
struct Object {uint64_t id=0,parent=0;n::Kind kind{};HWND window=nullptr;HBRUSH brush=nullptr;HDC dc=nullptr,memory=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;};
class Renderer {
public:
    std::array<Object,n::MaxObjects> objects{};uint32_t calls[8]{};
    Object* Find(uint64_t id){for(auto& o:objects)if(o.id==id&&id)return &o;return nullptr;}
    static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto* o=reinterpret_cast<Object*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(message==WM_NCCREATE){o=static_cast<Object*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(o));}if(message==WM_CLOSE)return 0;if(message==WM_PRINTCLIENT){if(o&&o->memory&&wp){RECT r{};GetClientRect(h,&r);BitBlt(reinterpret_cast<HDC>(wp),0,0,r.right,r.bottom,o->memory,0,0,SRCCOPY);}return 0;}if(message==WM_PAINT){PAINTSTRUCT ps{};const auto dc=BeginPaint(h,&ps);if(dc&&o&&o->memory){RECT r{};GetClientRect(h,&r);BitBlt(dc,0,0,r.right,r.bottom,o->memory,0,0,SRCCOPY);}EndPaint(h,&ps);return 0;}return DefWindowProcW(h,message,wp,lp);}
    static void Clear(Object& o){if(o.dc)ReleaseDC(o.window,o.dc);if(o.memory){if(o.previous)SelectObject(o.memory,o.previous);DeleteDC(o.memory);}if(o.bitmap)DeleteObject(o.bitmap);if(o.brush)DeleteObject(o.brush);if(o.window&&o.kind==n::Kind::Window)DestroyWindow(o.window);o={};}
    ~Renderer(){for(auto& o:objects)if(o.kind==n::Kind::Dc)Clear(o);for(auto& o:objects)if(o.id)Clear(o);}
    uint32_t Execute(const n::Command& c,uint64_t id,uint64_t& value){Object* target=Find(c.target);Object* extra=Find(c.object);Object* slot=nullptr;if(id){for(auto& o:objects)if(!o.id){slot=&o;break;}if(!slot)return ERROR_NOT_ENOUGH_QUOTA;}
        if(c.op==n::Op::Window){slot->id=id;slot->kind=n::Kind::Window;const auto width=c.rect.right-c.rect.left,height=c.rect.bottom-c.rect.top;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;slot->bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);slot->memory=CreateCompatibleDC(nullptr);if(!slot->bitmap||!slot->memory){Clear(*slot);return ERROR_NOT_ENOUGH_MEMORY;}slot->previous=SelectObject(slot->memory,slot->bitmap);if(!slot->previous||slot->previous==HGDI_ERROR){slot->previous=nullptr;Clear(*slot);return ERROR_INVALID_HANDLE;}RECT client{0,0,width,height};FillRect(slot->memory,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;RECT rect=client;if(!AdjustWindowRectEx(&rect,style,FALSE,0)){Clear(*slot);return ERROR_INVALID_PARAMETER;}std::wstring title=L"[MiniNT native 14.10A] ";for(auto ch:c.title){if(!ch)break;title+=wchar_t(ch);}slot->window=CreateWindowExW(0,WindowClass,title.c_str(),style,c.rect.left,c.rect.top,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,GetModuleHandleW(nullptr),slot);if(!slot->window){Clear(*slot);return ERROR_NOT_ENOUGH_MEMORY;}++calls[0];ShowWindow(slot->window,SW_SHOWNOACTIVATE);UpdateWindow(slot->window);return 0;}
        if(c.op==n::Op::Brush){slot->id=id;slot->kind=n::Kind::Brush;slot->brush=CreateSolidBrush(RGB((c.color>>16)&255,(c.color>>8)&255,c.color&255));if(!slot->brush){Clear(*slot);return ERROR_NOT_ENOUGH_MEMORY;}++calls[1];return 0;}
        if(c.op==n::Op::Dc){if(!target)return ERROR_INVALID_HANDLE;slot->id=id;slot->parent=target->id;slot->kind=n::Kind::Dc;slot->window=target->window;slot->dc=GetDC(target->window);if(!slot->dc){Clear(*slot);return ERROR_NOT_ENOUGH_MEMORY;}++calls[2];return 0;}
        if(!target)return ERROR_INVALID_HANDLE;
        if(c.op==n::Op::Fill||c.op==n::Op::Pixel){auto* window=Find(target->parent);if(!window)return ERROR_INVALID_HANDLE;if(c.op==n::Op::Fill){if(!extra)return ERROR_INVALID_HANDLE;RECT rect{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};if(!FillRect(window->memory,&rect,extra->brush))return ERROR_GEN_FAILURE;++calls[3];InvalidateRect(window->window,&rect,FALSE);UpdateWindow(window->window);}else{GdiFlush();const auto color=GetPixel(window->memory,c.rect.left,c.rect.top);if(color==CLR_INVALID)return ERROR_GEN_FAILURE;value=(uint32_t(GetRValue(color))<<16)|(uint32_t(GetGValue(color))<<8)|GetBValue(color);++calls[4];}return 0;}
        if(c.op==n::Op::ReleaseDc){if(!ReleaseDC(target->window,target->dc))return ERROR_GEN_FAILURE;target->dc=nullptr;*target={};++calls[5];return 0;}
        if(c.op==n::Op::DeleteBrush){if(!DeleteObject(target->brush))return ERROR_GEN_FAILURE;target->brush=nullptr;*target={};++calls[6];return 0;}
        if(c.op==n::Op::DestroyWindow){if(!DestroyWindow(target->window))return ERROR_GEN_FAILURE;target->window=nullptr;Clear(*target);++calls[7];return 0;}return ERROR_NOT_SUPPORTED;
    }
};

}
int Run(int argc,char** argv){if(argc!=2||std::string(argv[1])!="--native-gui-worker")return 2;const auto input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);if(GetFileType(input)!=FILE_TYPE_PIPE||GetFileType(output)!=FILE_TYPE_PIPE)return 3;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);const auto restrictions=av::microvm::native_gui::Restrictions();if(restrictions!=31)return 4;
    WNDCLASSW cls{};cls.lpfnWndProc=Renderer::Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=WindowClass;cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));if(!RegisterClassW(&cls))return 5;
    Renderer renderer;n::Packet p;DWORD read=0;if(!av::microvm::native_gui::ReadPacket(input,p)||p.magic!=n::Magic||p.version!=1||p.bytes!=sizeof(p)||!p.boot||!p.session||p.sequence||p.pid||p.tid||p.command.op!=n::Op::Hello||!n::Valid(p.command)||!n::Empty(&p.reply,sizeof(p.reply)))return 6;
    av::microvm::native_gui::Core core(p.boot,p.session,[&](const n::Command& c,uint64_t id,uint64_t& value){return renderer.Execute(c,id,value);});p.reply.restrictions=restrictions;p.reply.helperPid=GetCurrentProcessId();DWORD written=0;if(!WriteFile(output,&p,sizeof(p),&written,nullptr)||written!=sizeof(p))return 7;
    bool complete=false;MSG msg{};for(;;){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))DispatchMessageW(&msg);DWORD available=0;if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr))return complete?0:8;if(available>=sizeof(p)){if(complete||!ReadFile(input,&p,sizeof(p),&read,nullptr)||read!=sizeof(p)||!core.Exchange(p))return 9;complete=p.reply.completed!=0;p.reply.restrictions=restrictions;p.reply.helperPid=GetCurrentProcessId();for(size_t i=0;i<8;++i)p.reply.calls[i]=renderer.calls[i];if(!WriteFile(output,&p,sizeof(p),&written,nullptr)||written!=sizeof(p))return 10;}MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
}

int main(int argc,char** argv){try{return Run(argc,argv);}catch(...){return 12;}}
