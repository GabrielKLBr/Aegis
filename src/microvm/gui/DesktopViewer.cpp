#include "DesktopViewer.h"
#include "../state/Files.h"
#include <Windows.h>
#include <windowsx.h>
#include <deque>
#include <algorithm>
namespace av::microvm::desktop_session {
namespace d=guardian::desktop;
struct Viewer::State {
    HWND window=nullptr;std::deque<d::Input> queue;std::vector<uint32_t> pixels=std::vector<uint32_t>(d::PixelCount,0xFF172536);bool overflow=false,closing=false;uint16_t surrogate=0;
    static uint32_t Modifiers(){return (GetKeyState(VK_SHIFT)&0x8000?1u:0u)|(GetKeyState(VK_CONTROL)&0x8000?2u:0u)|(GetKeyState(VK_MENU)&0x8000?4u:0u)|(GetKeyState(VK_CAPITAL)&1?8u:0u);}
    void Push(d::Input event){if(!d::ValidInput(event)){overflow=true;return;}if(event.type==d::InputType::Move&&!queue.empty()&&queue.back().type==d::InputType::Move){queue.back()=event;return;}if(queue.size()==2048){overflow=true;return;}queue.push_back(event);}
    void Text(uint32_t scalar){d::Input input;input.type=d::InputType::Text;input.key=scalar;Push(input);}
    LRESULT Event(UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_CLOSE){if(!closing){d::Input input;input.type=d::InputType::Close;Push(input);closing=true;}else {d::Input input;input.type=d::InputType::Close;Push(input);}return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=LONG(d::Width);bitmap.bmiHeader.biHeight=-LONG(d::Height);bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;StretchDIBits(dc,0,0,client.right,client.bottom,0,0,int(d::Width),int(d::Height),pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);EndPaint(window,&paint);return 0;}
        if(message==WM_KILLFOCUS){d::Input input;input.type=d::InputType::Blur;Push(input);surrogate=0;return 0;}
        if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){d::Input input;input.type=message==WM_KEYDOWN||message==WM_SYSKEYDOWN?d::InputType::KeyDown:d::InputType::KeyUp;input.key=uint32_t(wp);input.scan=uint32_t(uint64_t(lp)>>16)&255;input.flags=uint32_t(uint64_t(lp)>>24)&1;input.modifiers=Modifiers();Push(input);return 0;}
        // Physical keys are translated in MiniNT. No host TranslateMessage call
        // generates duplicate text; committed text remains a separate input kind.
        if(message==WM_UNICHAR){if(wp==UNICODE_NOCHAR)return TRUE;Text(uint32_t(wp));return 0;}
        if(message==WM_CHAR){const auto code=uint16_t(wp);if(code>=0xD800&&code<=0xDBFF)surrogate=code;else if(code>=0xDC00&&code<=0xDFFF){if(surrogate)Text(0x10000+((uint32_t(surrogate)-0xD800)<<10)+(code-0xDC00));surrogate=0;}else {surrogate=0;if(code)Text(code);}return 0;}
        if(message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_MBUTTONDOWN||message==WM_MBUTTONUP){RECT client{};GetClientRect(window,&client);if(!client.right||!client.bottom)return 0;d::Input input;input.type=message==WM_MOUSEMOVE?d::InputType::Move:d::InputType::Button;input.x=std::clamp<int>(int(GET_X_LPARAM(lp))*int(d::Width)/client.right,0,int(d::Width)-1);input.y=std::clamp<int>(int(GET_Y_LPARAM(lp))*int(d::Height)/client.bottom,0,int(d::Height)-1);input.buttons=(wp&MK_LBUTTON?1u:0u)|(wp&MK_RBUTTON?2u:0u)|(wp&MK_MBUTTON?4u:0u);input.modifiers=Modifiers();if(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN){SetFocus(window);SetCapture(window);}if(!input.buttons&&GetCapture()==window)ReleaseCapture();Push(input);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<State*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(self));}if(!self)return DefWindowProcW(h,message,wp,lp);try{return self->Event(message,wp,lp);}catch(...){self->overflow=true;return 0;}}
};
Viewer::Viewer(bool visible):state_(std::make_unique<State>()) {WNDCLASSW cls{};cls.lpfnWndProc=State::Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Aegis.MiniNT.DesktopViewer.14.7";cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));state::Require(RegisterClassW(&cls)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Cannot register desktop viewer");RECT bounds{0,0,int(d::Width),int(d::Height)};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);state_->window=CreateWindowExW(0,cls.lpszClassName,L"MiniNT - desktop convidado",WS_OVERLAPPEDWINDOW|(visible?WS_VISIBLE:0),CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,cls.hInstance,state_.get());state::Require(state_->window!=nullptr,"Cannot create desktop viewer");}
Viewer::~Viewer(){if(state_->window)DestroyWindow(state_->window);}
d::Input Viewer::Poll(){MSG message{};while(PeekMessageW(&message,state_->window,0,0,PM_REMOVE))DispatchMessageW(&message);state::Require(!state_->overflow,"Desktop viewer input queue rejected an event");if(state_->queue.empty())return {};const auto input=state_->queue.front();state_->queue.pop_front();return input;}
void Viewer::Display(const d::Frame& frame,const uint32_t* pixels){state::Require(d::ValidFrame(frame)&&pixels,"Desktop viewer frame rejected");std::copy_n(pixels,d::PixelCount,state_->pixels.begin());InvalidateRect(state_->window,nullptr,FALSE);UpdateWindow(state_->window);}
}
