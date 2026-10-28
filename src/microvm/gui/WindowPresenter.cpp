#include "WindowPresenter.h"
#include "../state/Files.h"
#include <Windows.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
namespace av::microvm::window_session {
namespace d=guardian::desktop;namespace p=guardian::presentation;
namespace {
constexpr wchar_t ClassName[]=L"Aegis.MiniNT.WindowBridge.12F";
constexpr UINT Wake=WM_APP+72;constexpr uint32_t MaxDpi=240;
struct CaptureObjects {HBITMAP bitmap=nullptr;HDC dc=nullptr;HGDIOBJ previous=nullptr;~CaptureObjects(){if(dc){if(previous&&previous!=HGDI_ERROR)SelectObject(dc,previous);DeleteDC(dc);}if(bitmap)DeleteObject(bitmap);}};

std::wstring Title(const uint16_t* s){std::wstring result;for(uint32_t i=0;i<64&&s[i];++i)result+=wchar_t(s[i]);return result;}
std::string Json(const std::wstring& s){std::ostringstream out;out<<'"';for(const auto c:s){if(c>=32&&c<=126&&c!='"'&&c!='\\')out<<char(c);else out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<uint32_t(uint16_t(c))<<std::dec;}out<<'"';return out.str();}
bool Same(d::Rect a,d::Rect b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
bool ValidDisplay(const Frame& f){if(!f.session||!f.number||!ValidMetadata(f.metadata)||f.images.size()>d::MaxWindows)return false;size_t count=0;uint64_t bytes=0;for(uint32_t i=0;i<f.metadata.count;++i){const auto& w=f.metadata.windows[i];if(w.parent||!(w.flags&1))continue;++count;uint32_t found=0;for(const auto& image:f.images)if(image.window==w.handle){++found;if(image.width!=uint32_t(w.client.right-w.client.left)||image.height!=uint32_t(w.client.bottom-w.client.top)||image.pixels.size()!=size_t(image.width)*image.height)return false;bytes+=image.pixels.size()*4;}if(found!=1)return false;}return count==f.images.size()&&bytes<=p::MaxFrameBytes;}
}
std::vector<Action> ReadWindowScript(const std::filesystem::path& path){
    const auto data=state::ReadFile(path,128u<<10);std::istringstream lines(std::string(data.begin(),data.end()));std::string line;std::vector<Action> actions;uint64_t previous=0;
    while(std::getline(lines,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty()||line[0]=='#')continue;std::replace(line.begin(),line.end(),'|','\n');std::istringstream fields(line);std::string time,title,operation,values;state::Require(bool(std::getline(fields,time))&&bool(std::getline(fields,title))&&bool(std::getline(fields,operation)),"Window script fields rejected");Action action;size_t end=0;action.ms=std::stoull(time,&end);state::Require(end==time.size()&&action.ms>=previous&&action.ms<=30000&&!title.empty()&&title.size()<=63,"Window script target/time rejected");previous=action.ms;for(const auto c:title){state::Require(c>=32&&c<=126,"Window script title must be ASCII");action.title+=wchar_t(c);}action.operation=operation;
        std::string rest((std::istreambuf_iterator<char>(fields)),{});std::replace(rest.begin(),rest.end(),'\n',' ');std::istringstream numbers(rest);std::string extra;
        if(operation=="down"||operation=="up")state::Require(bool(numbers>>action.a>>action.b>>action.c)&&action.a>0&&action.a<256&&action.b>=0&&action.b<=255&&action.c>=0&&action.c<=15,"Window script key rejected");
        else if(operation=="text")state::Require(bool(numbers>>action.a)&&action.a>0&&action.a<=0x10FFFF&&!(action.a>=0xD800&&action.a<=0xDFFF),"Window script text rejected");
        else if(operation=="move"||operation=="resize"||operation=="mouse"||operation=="button"){state::Require(bool(numbers>>action.a>>action.b),"Window script coordinates rejected");if(operation=="button")state::Require(bool(numbers>>action.c)&&action.c>=0&&action.c<=7,"Window script buttons rejected");if(operation=="resize")state::Require(action.a>0&&action.b>0&&action.a<=int32_t(p::MaxWidth)&&action.b<=int32_t(p::MaxHeight),"Window script resize rejected");else state::Require(action.a>=-32768&&action.a<=32768&&action.b>=-32768&&action.b<=32768,"Window script coordinate bounds rejected");}
        else if(operation=="scale")state::Require(bool(numbers>>action.a)&&action.a>=96&&action.a<=int32_t(MaxDpi),"Window script DPI rejected");
        else state::Require(operation=="focus"||operation=="blur"||operation=="close"||operation=="minimize"||operation=="restore"||operation=="fail","Window script operation rejected");
        state::Require(!(numbers>>extra)&&actions.size()<2048,"Window script quota/arguments rejected");actions.push_back(std::move(action));
    }return actions;
}
struct Presenter::State {
    struct Slot {~Slot();State* self=nullptr;HWND window=nullptr;d::Window info{};uint32_t dpi=96,buttons=0;uint16_t surrogate=0;bool syncing=false,geometryPending=false;uint64_t capturedHash=0;d::Rect pendingBounds{};std::shared_ptr<const Frame> frame;size_t image=0;};
    std::thread thread;std::mutex mutex;std::condition_variable readyCondition;std::atomic<DWORD> tid{0};std::atomic<bool> stopping{false};bool ready=false,visible=true,finished=false,processing=false;std::string failure;std::shared_ptr<const Frame> pending;
    std::deque<p::Event> events;std::vector<std::unique_ptr<Slot>> windows;std::vector<Action> actions;size_t action=0;std::shared_ptr<const Frame> current;
    uint64_t expectedSession=0,session=0,received=0,applied=0,coalesced=0,created=0,destroyed=0,queued=0,geometry=0,dpiChanges=0,painted=0,scriptOrigin=0;std::atomic<uint64_t> delivered{0};bool initialAwaited=false,initialPresented=false;std::filesystem::path capturePath;uint32_t captures=0;std::vector<std::string> history;int scriptModifiers=-1;
#ifdef WINDOW_BRIDGE_TESTING
    std::atomic<uint32_t> resourceFault{0};
    void ResourcePoint(uint32_t stage){if(resourceFault.load()==stage){resourceFault=0;throw std::runtime_error("Requested native resource failure test");}}
#else
    void ResourcePoint(uint32_t){}
#endif
    void Fail(const char* message){std::lock_guard lock(mutex);if(failure.empty())failure=message;}
    Slot* Find(uint64_t handle){for(const auto& s:windows)if(s->info.handle==handle)return s.get();return nullptr;}
    bool Owns(HWND h){for(const auto& s:windows)if(s->window==h)return true;return false;}
    uint32_t Modifiers(){if(scriptModifiers>=0)return uint32_t(scriptModifiers);return (GetKeyState(VK_SHIFT)&0x8000?1u:0u)|(GetKeyState(VK_CONTROL)&0x8000?2u:0u)|(GetKeyState(VK_MENU)&0x8000?4u:0u)|(GetKeyState(VK_CAPITAL)&1?8u:0u);}
    void Push(Slot& s,p::Event event){if(s.syncing||stopping)return;event.session=session;event.window=s.info.handle;if(!p::ValidEvent(event)){Fail("Native window input rejected");return;}std::lock_guard lock(mutex);if(!events.empty()&&event.window==events.back().window&&event.kind==events.back().kind&&(event.kind==p::Kind::Move||event.kind==p::Kind::Geometry)){events.back()=event;return;}if(events.size()==2048){if(failure.empty())failure="Native input queue exceeded";return;}events.push_back(event);++queued;}
    void Simple(Slot& s,p::Kind kind){p::Event event;event.kind=kind;Push(s,event);}
    DWORD Style(const d::Window& w){DWORD value=WS_CLIPCHILDREN|WS_CLIPSIBLINGS;if((w.style&WS_CAPTION)==WS_CAPTION)value|=WS_CAPTION|WS_SYSMENU;else value|=WS_POPUP;if(w.style&WS_THICKFRAME)value|=WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX;return value;}
    RECT NativeBounds(const Slot& s,const d::Window& w,uint32_t dpi){RECT result{0,0,p::Physical(w.client.right-w.client.left,dpi),p::Physical(w.client.bottom-w.client.top,dpi)};
        // Synthetic presentation DPI does not change the HWND's actual
        // nonclient metrics. Real WM_DPICHANGED already exposes the new DPI
        // through GetDpiForWindow; simulated transitions must use that same
        // measured frame while scaling only the client pixels and position.
        const auto frameDpi=s.window?GetDpiForWindow(s.window):dpi;state::Require(frameDpi&&AdjustWindowRectExForDpi(&result,Style(w),FALSE,0,frameDpi)!=FALSE,"Cannot compute native window frame");const auto width=result.right-result.left,height=result.bottom-result.top;result.left=p::Physical(w.bounds.left,dpi);result.top=p::Physical(w.bounds.top,dpi);result.right=result.left+width;result.bottom=result.top+height;return result;}
    void Geometry(Slot& s){if(s.syncing||IsIconic(s.window)||!s.frame)return;RECT outer{},client{};if(!GetWindowRect(s.window,&outer)||!GetClientRect(s.window,&client))return;const auto width=p::Logical(client.right,s.dpi),height=p::Logical(client.bottom,s.dpi);if(width<=0||height<=0||width>int32_t(p::MaxWidth)||height>int32_t(p::MaxHeight)){Fail("Native resize exceeded surface quota");return;}const auto chromeWidth=(s.info.bounds.right-s.info.bounds.left)-(s.info.client.right-s.info.client.left),chromeHeight=(s.info.bounds.bottom-s.info.bounds.top)-(s.info.client.bottom-s.info.client.top);p::Event event;event.kind=p::Kind::Geometry;event.bounds={p::Logical(outer.left,s.dpi),p::Logical(outer.top,s.dpi),0,0};event.bounds.right=event.bounds.left+width+chromeWidth;event.bounds.bottom=event.bounds.top+height+chromeHeight;s.geometryPending=true;s.pendingBounds=event.bounds;++geometry;Push(s,event);}
    void Paint(Slot& s,HDC dc){if(!s.frame||s.image>=s.frame->images.size())return;const auto& image=s.frame->images[s.image];RECT client{};if(!GetClientRect(s.window,&client))return;BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=LONG(image.width);bitmap.bmiHeader.biHeight=-LONG(image.height);bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,0,0,client.right,client.bottom,0,0,int(image.width),int(image.height),image.pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);if((s.info.style&WS_CAPTION)!=WS_CAPTION){const auto marker=CreateSolidBrush(RGB(0,170,220));state::Require(marker!=nullptr,"Native origin marker allocation failed");FrameRect(dc,&client,marker);InflateRect(&client,-1,-1);FrameRect(dc,&client,marker);DeleteObject(marker);}++painted;}
    void Capture(Slot& s){if(capturePath.empty()||captures>=16||s.image>=s.frame->images.size())return;const auto& image=s.frame->images[s.image];uint64_t hash=14695981039346656037ull;for(const auto pixel:image.pixels){hash^=pixel;hash*=1099511628211ull;}if(hash==s.capturedHash)return;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(image.width);info.bmiHeader.biHeight=-LONG(image.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* raw=nullptr;CaptureObjects objects;objects.bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0);objects.dc=CreateCompatibleDC(nullptr);state::Require(objects.bitmap&&objects.dc&&raw,"Native capture allocation failed");objects.previous=SelectObject(objects.dc,objects.bitmap);state::Require(objects.previous&&objects.previous!=HGDI_ERROR,"Native capture selection failed");ResourcePoint(2);const auto dc=objects.dc;RECT client{};GetClientRect(s.window,&client);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,client.right,client.bottom,nullptr);SetViewportExtEx(dc,int(image.width),int(image.height),nullptr);SendMessageW(s.window,WM_PRINTCLIENT,WPARAM(dc),PRF_CLIENT);GdiFlush();const std::string header="P6\n"+std::to_string(image.width)+" "+std::to_string(image.height)+"\n255\n";std::vector<uint8_t> data(header.begin(),header.end());data.reserve(data.size()+image.pixels.size()*3);const auto* colors=static_cast<const uint32_t*>(raw);for(size_t i=0;i<image.pixels.size();++i){const auto color=colors[i];data.push_back(uint8_t(color>>16));data.push_back(uint8_t(color>>8));data.push_back(uint8_t(color));}state::AtomicWrite(capturePath/("native-window-"+std::to_string(++captures)+"-"+std::to_string(s.info.handle)+".ppm"),data);s.capturedHash=hash;}
    LRESULT Event(Slot& s,UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_CLOSE){Simple(s,p::Kind::Close);return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(s.window,&paint);if(dc)Paint(s,dc);EndPaint(s.window,&paint);return 0;}
        if(message==WM_PRINTCLIENT){if(wp)Paint(s,reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_SETFOCUS){Simple(s,p::Kind::Focus);return 0;}
        if(message==WM_KILLFOCUS){Simple(s,p::Kind::Blur);s.surrogate=0;return 0;}
        if(message==WM_MOVE||message==WM_SIZE){Geometry(s);return 0;}
        if(message==WM_GETMINMAXINFO){auto* limits=reinterpret_cast<MINMAXINFO*>(lp);if(limits){d::Window max=s.info;max.client={0,0,int32_t(p::MaxWidth),int32_t(p::MaxHeight)};const auto rect=NativeBounds(s,max,s.dpi);limits->ptMaxTrackSize={rect.right-rect.left,rect.bottom-rect.top};limits->ptMaxSize=limits->ptMaxTrackSize;d::Window minimum=s.info;minimum.client={0,0,16,16};const auto small=NativeBounds(s,minimum,s.dpi);limits->ptMinTrackSize={small.right-small.left,small.bottom-small.top};}return 0;}
        if(message==WM_DPICHANGED){const auto dpi=uint32_t(LOWORD(wp));if(dpi<96||dpi>MaxDpi||HIWORD(wp)!=dpi){Fail("Native DPI outside supported range");return 0;}s.dpi=dpi;++dpiChanges;if(lp){const auto& r=*reinterpret_cast<const RECT*>(lp);if(r.right<=r.left||r.bottom<=r.top||r.right-r.left>8192||r.bottom-r.top>8192){Fail("Native DPI rectangle rejected");return 0;}SetWindowPos(s.window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);}RecordHistory(current?current->number:0,"dpi");return 0;}
        if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){p::Event event;event.kind=message==WM_KEYDOWN||message==WM_SYSKEYDOWN?p::Kind::KeyDown:p::Kind::KeyUp;event.key=uint32_t(wp);event.scan=uint32_t(uint64_t(lp)>>16)&255;event.flags=uint32_t(uint64_t(lp)>>24)&1;event.modifiers=Modifiers();Push(s,event);return 0;}
        if(message==WM_UNICHAR||message==WM_CHAR){if(message==WM_UNICHAR&&wp==UNICODE_NOCHAR)return TRUE;uint32_t scalar=uint32_t(wp);if(message==WM_CHAR){if(scalar>=0xD800&&scalar<=0xDBFF){s.surrogate=uint16_t(scalar);return 0;}if(scalar>=0xDC00&&scalar<=0xDFFF){if(!s.surrogate)return 0;scalar=0x10000+((uint32_t(s.surrogate)-0xD800)<<10)+(scalar-0xDC00);}s.surrogate=0;}p::Event event;event.kind=p::Kind::Text;event.key=scalar;Push(s,event);return 0;}
        if(message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_MBUTTONDOWN||message==WM_MBUTTONUP){if(!s.frame)return 0;const auto width=s.info.client.right-s.info.client.left,height=s.info.client.bottom-s.info.client.top;if(width<=0||height<=0)return 0;p::Event event;event.kind=message==WM_MOUSEMOVE?p::Kind::Move:p::Kind::Button;event.x=std::clamp(p::Logical(GET_X_LPARAM(lp),s.dpi),0,width-1);event.y=std::clamp(p::Logical(GET_Y_LPARAM(lp),s.dpi),0,height-1);event.buttons=(wp&MK_LBUTTON?1u:0u)|(wp&MK_RBUTTON?2u:0u)|(wp&MK_MBUTTON?4u:0u);event.modifiers=Modifiers();s.buttons=event.buttons;if(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN){SetFocus(s.window);SetCapture(s.window);}if(!event.buttons&&GetCapture()==s.window)ReleaseCapture();Push(s,event);return 0;}
        return DefWindowProcW(s.window,message,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto* slot=reinterpret_cast<Slot*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(message==WM_NCCREATE){slot=static_cast<Slot*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);slot->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(slot));}if(!slot)return DefWindowProcW(h,message,wp,lp);try{return slot->self->Event(*slot,message,wp,lp);}catch(...){slot->self->Fail("Native presentation callback failed");return 0;}}
    void Destroy(Slot& s){s.syncing=true;if(s.window){if(IsWindow(s.window)){RemovePropW(s.window,L"Aegis.Session12F");DestroyWindow(s.window);}++destroyed;}s.window=nullptr;}
    void RecordHistory(uint64_t number,const char* event){if(history.size()<4096){std::ostringstream text;text<<"{\"frame\":"<<number<<",\"event\":\""<<event<<"\",\"windows\":[";bool first=true;for(const auto& slot:windows){if(!first)text<<',';first=false;RECT client{};GetClientRect(slot->window,&client);text<<"{\"guest\":"<<slot->info.handle<<",\"owner\":"<<slot->info.owner<<",\"title\":"<<Json(Title(slot->info.title))<<",\"dpi\":"<<slot->dpi<<",\"enabled\":"<<(IsWindowEnabled(slot->window)?"true":"false")<<",\"visible\":"<<(IsWindowVisible(slot->window)?"true":"false")<<",\"clientWidth\":"<<client.right<<",\"clientHeight\":"<<client.bottom<<",\"x\":"<<slot->info.bounds.left<<",\"y\":"<<slot->info.bounds.top<<'}';}text<<"]}";history.push_back(text.str());}}
    void Apply(std::shared_ptr<const Frame> frame){
        if(session&&session!=frame->session){Fail("Native presenter session changed");return;}session=frame->session;current=frame;
        for(auto it=windows.begin();it!=windows.end();){bool found=false;for(uint32_t i=0;i<frame->metadata.count;++i)if(frame->metadata.windows[i].handle==(*it)->info.handle&&!frame->metadata.windows[i].parent)found=true;if(!found){Destroy(**it);it=windows.erase(it);}else ++it;}
        uint32_t missing=0;do{missing=0;bool progress=false;for(uint32_t i=0;i<frame->metadata.count;++i){const auto& w=frame->metadata.windows[i];if(w.parent||Find(w.handle))continue;auto* owner=w.owner?Find(w.owner):nullptr;if(w.owner&&!owner){++missing;continue;}auto slot=std::make_unique<Slot>();slot->self=this;slot->info=w;slot->syncing=true;const auto rect=NativeBounds(*slot,w,96);const auto title=L"[MiniNT] "+Title(w.title);slot->window=CreateWindowExW(0,ClassName,title.c_str(),Style(w),rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,owner?owner->window:nullptr,nullptr,GetModuleHandleW(nullptr),slot.get());state::Require(slot->window!=nullptr,"Cannot create native guest window");++created;ResourcePoint(1);slot->dpi=GetDpiForWindow(slot->window);state::Require(slot->dpi>=96&&slot->dpi<=MaxDpi,"Unsupported native monitor DPI");state::Require(SetPropW(slot->window,L"Aegis.Session12F",reinterpret_cast<HANDLE>(session))!=FALSE,"Native session property failed");windows.push_back(std::move(slot));progress=true;}if(missing&&!progress)throw std::runtime_error("Native owner graph rejected");}while(missing);
        for(auto& item:windows){auto& slot=*item;const d::Window* w=nullptr;for(uint32_t i=0;i<frame->metadata.count;++i)if(frame->metadata.windows[i].handle==slot.info.handle)w=&frame->metadata.windows[i];if(!w)continue;slot.syncing=true;
            const auto title=L"[MiniNT] "+Title(w->title);SetWindowTextW(slot.window,title.c_str());const auto style=Style(*w);const auto previousStyle=DWORD(GetWindowLongPtrW(slot.window,GWL_STYLE));const auto dynamicStyle=previousStyle&(WS_VISIBLE|WS_DISABLED|WS_MINIMIZE|WS_MAXIMIZE);if(previousStyle!=(style|dynamicStyle))SetWindowLongPtrW(slot.window,GWL_STYLE,LONG_PTR(style|dynamicStyle));
            if(slot.geometryPending&&Same(w->bounds,slot.pendingBounds))slot.geometryPending=false;
            if(!slot.geometryPending&&!IsIconic(slot.window)){const auto rect=NativeBounds(slot,*w,slot.dpi);RECT old{};GetWindowRect(slot.window,&old);if(old.left!=rect.left||old.top!=rect.top||old.right!=rect.right||old.bottom!=rect.bottom)SetWindowPos(slot.window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
            const auto* owner=w->owner?Find(w->owner):nullptr;SetWindowLongPtrW(slot.window,GWLP_HWNDPARENT,LONG_PTR(owner?owner->window:nullptr));EnableWindow(slot.window,(w->style&WS_DISABLED)==0);
            slot.info=*w;slot.frame=frame;slot.image=frame->images.size();for(size_t i=0;i<frame->images.size();++i)if(frame->images[i].window==w->handle)slot.image=i;
            if((w->flags&1)&&visible){if(!IsWindowVisible(slot.window))ShowWindow(slot.window,SW_SHOWNOACTIVATE);InvalidateRect(slot.window,nullptr,FALSE);UpdateWindow(slot.window);}else if(IsWindowVisible(slot.window))ShowWindow(slot.window,SW_HIDE);
            if(slot.image<frame->images.size()&&(w->flags&1))Capture(slot);
            slot.syncing=false;
        }
        std::vector<Slot*> stacking;for(auto& slot:windows)if(slot->info.flags&1)stacking.push_back(slot.get());
        const auto rank=[&](const Slot* slot){for(uint32_t i=0;i<frame->metadata.count;++i)if(frame->metadata.windows[i].handle==slot->info.handle)return frame->stacking[i];return uint64_t(0);};
        std::sort(stacking.begin(),stacking.end(),[&](const Slot* a,const Slot* b){if(bool(a->info.exStyle&8)!=bool(b->info.exStyle&8))return bool(a->info.exStyle&8);if(rank(a)!=rank(b))return rank(a)>rank(b);return a->info.handle>b->info.handle;});
        for(size_t i=1;i<stacking.size();++i){stacking[i]->syncing=true;SetWindowPos(stacking[i]->window,stacking[i-1]->window,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);stacking[i]->syncing=false;}
        if(Owns(GetForegroundWindow())){if(auto* focus=Target(L"@focused");focus&&IsWindowEnabled(focus->window)&&IsWindowVisible(focus->window)&&GetFocus()!=focus->window){focus->syncing=true;SetActiveWindow(focus->window);SetFocus(focus->window);focus->syncing=false;}}
        ++applied;if(!scriptOrigin&&!windows.empty())scriptOrigin=GetTickCount64();
        RecordHistory(frame->number,"frame");
    }
    Slot* Target(const std::wstring& title){Slot* result=nullptr;if(title==L"@focused"){uint64_t handle=current?current->metadata.focus:0;for(uint32_t n=0;handle&&current&&n<d::MaxWindows;++n){if(auto* root=Find(handle))return root;bool found=false;for(uint32_t i=0;i<current->metadata.count;++i)if(current->metadata.windows[i].handle==handle){handle=current->metadata.windows[i].parent;found=true;break;}if(!found)break;}for(const auto& slot:windows)if((slot->info.flags&1)&&!(slot->info.style&WS_DISABLED)&&(!result||slot->info.handle>result->info.handle))result=slot.get();return result;}
        for(const auto& slot:windows)if(Title(slot->info.title)==title){if(result)return nullptr;result=slot.get();}return result;
    }
    void Actions(){if(!scriptOrigin||action==actions.size()||!current)return;const auto now=GetTickCount64()-scriptOrigin;
        while(action<actions.size()&&actions[action].ms<=now){const auto& value=actions[action];auto* slot=Target(value.title);if(!slot){if(now>value.ms+2000){Fail("Window script target absent or ambiguous");}return;}const auto h=slot->window;const auto& op=value.operation;scriptModifiers=(op=="down"||op=="up")?value.c:0;
            if(op=="focus")SendMessageW(h,WM_SETFOCUS,0,0);else if(op=="blur")SendMessageW(h,WM_KILLFOCUS,0,0);else if(op=="close")SendMessageW(h,WM_CLOSE,0,0);
            else if(op=="down"||op=="up"){const bool up=op=="up",alt=(value.c&4)!=0;const auto message=alt?(up?WM_SYSKEYUP:WM_SYSKEYDOWN):(up?WM_KEYUP:WM_KEYDOWN);SendMessageW(h,message,WPARAM(value.a),LPARAM(1ull|(uint64_t(value.b)<<16)|(up?(3ull<<30):0)));}
            else if(op=="text")SendMessageW(h,WM_UNICHAR,WPARAM(value.a),1);
            else if(op=="mouse"||op=="button"){const auto buttons=op=="mouse"?slot->buttons:uint32_t(value.c);const auto wp=WPARAM((buttons&3)|((buttons&4)?MK_MBUTTON:0));const auto changed=slot->buttons^buttons;const bool down=(buttons&changed)!=0;const UINT message=op=="mouse"?WM_MOUSEMOVE:changed&2?(down?WM_RBUTTONDOWN:WM_RBUTTONUP):changed&4?(down?WM_MBUTTONDOWN:WM_MBUTTONUP):(down?WM_LBUTTONDOWN:WM_LBUTTONUP);SendMessageW(h,message,wp,MAKELPARAM(p::Physical(value.a,slot->dpi),p::Physical(value.b,slot->dpi)));}
            else if(op=="move"){SetWindowPos(h,nullptr,p::Physical(value.a,slot->dpi),p::Physical(value.b,slot->dpi),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
            else if(op=="resize"){auto info=slot->info;info.client={0,0,value.a,value.b};const auto r=NativeBounds(*slot,info,slot->dpi);SetWindowPos(h,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);}
            else if(op=="scale"){
                // A scripted DPI transition may precede the guest's resize
                // acknowledgement. Preserve the live logical geometry instead
                // of reverting to the bounds in an older guest frame.
                RECT client{},outer{};state::Require(GetClientRect(h,&client)&&GetWindowRect(h,&outer),"Cannot query native DPI geometry");
                auto info=slot->info;info.bounds.left=p::Logical(outer.left,slot->dpi);info.bounds.top=p::Logical(outer.top,slot->dpi);
                info.client={0,0,p::Logical(client.right,slot->dpi),p::Logical(client.bottom,slot->dpi)};
                const auto rect=NativeBounds(*slot,info,uint32_t(value.a));SendMessageW(h,WM_DPICHANGED,MAKEWPARAM(value.a,value.a),LPARAM(&rect));
            }
            else if(op=="minimize")ShowWindow(h,SW_SHOWMINNOACTIVE);else if(op=="restore")ShowWindow(h,SW_SHOWNOACTIVATE);else if(op=="fail")Fail("Requested native bridge failure test");
            scriptModifiers=-1;++action;
        }
    }
    void Run(){try{
        const auto previousDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);state::Require(previousDpi!=nullptr,"Native DPI context unavailable");WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=ClassName;cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));state::Require(RegisterClassW(&cls)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Cannot register native window presenter");MSG msg{};PeekMessageW(&msg,nullptr,0,0,PM_NOREMOVE);tid=GetCurrentThreadId();{std::lock_guard lock(mutex);ready=true;}readyCondition.notify_all();
        while(!stopping){std::shared_ptr<const Frame> next;{std::lock_guard lock(mutex);next=std::move(pending);processing=bool(next);}if(next){Apply(std::move(next));{std::lock_guard lock(mutex);processing=false;}readyCondition.notify_all();}while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){stopping=true;break;}if(msg.message!=Wake)DispatchMessageW(&msg);}Actions();MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        SetThreadDpiAwarenessContext(previousDpi);
    }catch(const std::exception& error){Fail(error.what());}catch(...){Fail("Native presenter failed");}
        for(auto& slot:windows)Destroy(*slot);windows.clear();current.reset();{std::lock_guard lock(mutex);events.clear();pending.reset();processing=false;ready=true;}readyCondition.notify_all();
    }
};
Presenter::State::Slot::~Slot(){if(window)self->Destroy(*this);}
#ifdef WINDOW_BRIDGE_TESTING
void Presenter::FailNextResourceForTesting(uint32_t stage){state::Require(stage==1||stage==2,"Native resource test stage rejected");state_->resourceFault=stage;}
#endif
Presenter::Presenter(std::vector<Action> actions,bool visible,std::filesystem::path captures):state_(std::make_unique<State>()){state_->visible=visible;state_->capturePath=std::move(captures);state_->actions=std::move(actions);state_->thread=std::thread([this]{state_->Run();});std::unique_lock lock(state_->mutex);state_->readyCondition.wait(lock,[this]{return state_->ready;});if(!state_->failure.empty()){const auto failure=state_->failure;lock.unlock();if(state_->thread.joinable())state_->thread.join();throw std::runtime_error(failure);}}
Presenter::~Presenter(){try{Finish();}catch(...){}}
void Presenter::Display(const Frame& frame){
    state::Require(ValidDisplay(frame),"Native display frame rejected");
    std::unique_lock lock(state_->mutex);
    state::Require(!state_->finished&&state_->failure.empty(),"Native presenter unavailable");
    state::Require(!state_->expectedSession||state_->expectedSession==frame.session,"Native display session rejected");
    const bool firstVisible=!state_->initialAwaited&&std::any_of(frame.metadata.windows,frame.metadata.windows+frame.metadata.count,[](const auto& w){return (w.flags&1)!=0;});
    if(firstVisible)state_->initialAwaited=true;
    state_->expectedSession=frame.session;
    if(state_->pending)++state_->coalesced;
    state_->pending=std::make_shared<Frame>(frame);++state_->received;
    PostThreadMessageW(state_->tid,Wake,0,0);
    // The guest must not outrun creation of its first visible windows. Later
    // frames retain asynchronous coalescing and the original failure contract.
    if(firstVisible){
        const bool ready=state_->readyCondition.wait_for(lock,std::chrono::seconds(2),[this]{return (!state_->pending&&!state_->processing)||!state_->failure.empty();});
        if(!ready&&state_->failure.empty())state_->failure="Initial native frame deadline";
        state_->initialPresented=ready&&state_->failure.empty();
    }
}
p::Event Presenter::Poll(){std::lock_guard lock(state_->mutex);state::Require(state_->failure.empty(),state_->failure.empty()?"Native presenter failed":state_->failure.c_str());if(state_->events.empty())return {};const auto event=state_->events.front();state_->events.pop_front();++state_->delivered;return event;}
void Presenter::Finish(const std::filesystem::path& output){if(!state_->finished){{std::unique_lock lock(state_->mutex);state_->readyCondition.wait_for(lock,std::chrono::seconds(2),[this]{return (!state_->pending&&!state_->processing)||!state_->failure.empty();});}state_->stopping=true;EnumThreadWindows(state_->tid,[](HWND h,LPARAM)->BOOL {PostMessageW(h,WM_CANCELMODE,0,0);return TRUE;},0);PostThreadMessageW(state_->tid,WM_QUIT,0,0);if(state_->thread.joinable())state_->thread.join();state_->finished=true;}if(!output.empty())state::AtomicText(output/"native-windows.json",Report());}
std::string Presenter::Report() const {state::Require(state_->finished,"Stop native presenter before reading its report");std::ostringstream text;text<<"{\"schema\":1,\"phase\":\"12F\",\"session\":"<<state_->session<<",\"framesReceived\":"<<state_->received<<",\"framesApplied\":"<<state_->applied<<",\"framesCoalesced\":"<<state_->coalesced<<",\"created\":"<<state_->created<<",\"destroyed\":"<<state_->destroyed<<",\"liveWindows\":"<<state_->windows.size()<<",\"eventsQueued\":"<<state_->queued<<",\"eventsDelivered\":"<<state_->delivered<<",\"geometryEvents\":"<<state_->geometry<<",\"dpiChanges\":"<<state_->dpiChanges<<",\"paints\":"<<state_->painted<<",\"nativeCaptures\":"<<state_->captures<<",\"initialFrameSynchronized\":"<<(state_->initialPresented?"true":"false")<<",\"scriptConsumed\":"<<state_->action<<",\"scriptTotal\":"<<state_->actions.size()<<",\"failure\":"<<Json(std::wstring(state_->failure.begin(),state_->failure.end()))<<",\"history\":[";for(size_t i=0;i<state_->history.size();++i){if(i)text<<',';text<<state_->history[i];}text<<"]}\n";return text.str();}
}
