#include "NativeGdiEngine.h"
#include <algorithm>
#include <string>
#include <cstring>
#ifdef GUARDIAN_GDI_EXPANDED
#include "guardian/ExpandedGdi.h"
#include <cmath>
#endif
namespace av::microvm::native_gui {
namespace {
constexpr uint32_t Bad=ERROR_INVALID_HANDLE,Quota=ERROR_NOT_ENOUGH_QUOTA;
int Index(g::Kind k){return k==g::Kind::Pen?0:k==g::Kind::Brush?1:k==g::Kind::Font?2:k==g::Kind::Bitmap?3:-1;}
g::Kind Type(int index){return index==OBJ_PEN?g::Kind::Pen:index==OBJ_BRUSH?g::Kind::Brush:index==OBJ_FONT?g::Kind::Font:index==OBJ_BITMAP?g::Kind::Bitmap:g::Kind{};}
bool Face(const uint16_t* face){wchar_t name[33]{};for(size_t i=0;i<32;++i){if(face[i]>127)return false;name[i]=wchar_t(face[i]);}if(face[31])return false;
#ifdef GUARDIAN_NATIVE_CONTROLS
if(!name[0]||!_wcsicmp(name,L"Sans Serif")||!_wcsicmp(name,L"MS Shell Dlg")||!_wcsicmp(name,L"MS Shell Dlg 2"))return true;
#endif
#ifdef GUARDIAN_GDI_EXPANDED
for(const auto* extra:{L"Tahoma",L"Verdana",L"Times New Roman",L"Courier New",L"Calibri",L"Cambria",L"Georgia"})if(!_wcsicmp(name,extra))return true;
#endif
return !_wcsicmp(name,L"Consolas")||!_wcsicmp(name,L"Arial")||!_wcsicmp(name,L"Segoe UI");}
HBITMAP Dib(int width,int height,void** data){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;return CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,data,nullptr,0);}
}
GdiResource* GdiEngine::Find(uint64_t id){for(auto& r:resources)if(id&&r.id==id)return &r;return nullptr;}
GdiSurface* GdiEngine::Surface(uint64_t id){for(auto& s:surfaces)if(id&&s.id==id)return &s;return nullptr;}
GdiResource* GdiEngine::Allocate(g::Kind k,uint32_t pid,uint32_t tid){for(auto& r:resources)if(!r.id){r={};r.id=(uint64_t(k)<<56)|((session&0xFFFFFF)<<32)|next++;r.pid=pid;r.tid=tid;r.kind=k;return &r;}return nullptr;}
bool GdiEngine::Selected(uint64_t id) const {for(const auto& item:resources)if(item.id==id&&item.pins)return true;for(const auto& r:resources)if(r.id&&r.kind==g::Kind::Dc){for(auto v:r.selected)if(v==id)return true;for(int i=0;i<r.depth;++i)for(auto v:r.saved[i])if(v==id)return true;}return false;}
void GdiEngine::Drop(GdiSurface& s){if(s.dc){SelectObject(s.dc,s.old);DeleteDC(s.dc);}if(s.bitmap){DeleteObject(s.bitmap);bytes-=uint64_t(s.width)*s.height*4;}s={};}
uint32_t GdiEngine::Resize(GdiSurface& s){RECT rect{};if(!GetClientRect(s.hwnd,&rect))return Bad;const auto dpi=GetDpiForWindow(s.hwnd);const int width=std::max(1,MulDiv(rect.right,96,int(dpi))),height=std::max(1,MulDiv(rect.bottom,96,int(dpi)));
    if(width==s.width&&height==s.height)return 0;for(const auto& r:resources)if(r.id&&r.window==s.id)return ERROR_BUSY;
    const auto size=uint64_t(width)*height*4,previous=uint64_t(s.width)*s.height*4;
    if(width>1280||height>720||bytes-previous+size>g::MaxBytes)return Quota;
    void* pixels=nullptr;const auto bitmap=Dib(width,height,&pixels);const auto dc=CreateCompatibleDC(nullptr);if(!bitmap||!dc){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return ERROR_NOT_ENOUGH_MEMORY;}
    const auto old=SelectObject(dc,bitmap);RECT all{0,0,width,height};FillRect(dc,&all,GetSysColorBrush(COLOR_WINDOW));if(s.dc)BitBlt(dc,0,0,std::min(width,s.width),std::min(height,s.height),s.dc,0,0,SRCCOPY);
    if(s.dc){SelectObject(s.dc,s.old);DeleteDC(s.dc);}if(s.bitmap)DeleteObject(s.bitmap);s.dc=dc;s.bitmap=bitmap;s.old=old;s.width=width;s.height=height;bytes=bytes-previous+size;return 0;
}
uint32_t GdiEngine::AddWindow(uint64_t id,HWND hwnd){for(auto& s:surfaces)if(!s.id){s.id=id;s.hwnd=hwnd;const auto error=Resize(s);if(error){s={};return error;}s.dirty={0,0,s.width,s.height};s.pending=true;return 0;}return Quota;}
void GdiEngine::Present(GdiResource& r){if(!r.window)return;auto* s=Surface(r.window);if(!s)return;RECT rect{};GetClientRect(s->hwnd,&rect);const auto dc=GetDC(s->hwnd);if(dc){SetStretchBltMode(dc,COLORONCOLOR);StretchBlt(dc,0,0,rect.right,rect.bottom,s->dc,0,0,s->width,s->height,SRCCOPY);ReleaseDC(s->hwnd,dc);}}
void GdiEngine::Paint(uint64_t id,HWND hwnd,HDC dc,const RECT& dirty){auto* s=Surface(id);if(!s){FillRect(dc,&dirty,GetSysColorBrush(COLOR_WINDOW));return;}Resize(*s);const auto dpi=GetDpiForWindow(hwnd);RECT logical{MulDiv(dirty.left,96,int(dpi)),MulDiv(dirty.top,96,int(dpi)),MulDiv(dirty.right,96,int(dpi)),MulDiv(dirty.bottom,96,int(dpi))};if(s->pending)UnionRect(&s->dirty,&s->dirty,&logical);else s->dirty=logical;s->pending=true;RECT rect{};GetClientRect(hwnd,&rect);SetStretchBltMode(dc,COLORONCOLOR);StretchBlt(dc,0,0,rect.right,rect.bottom,s->dc,0,0,s->width,s->height,SRCCOPY);}
void GdiEngine::Print(uint64_t id,HWND hwnd,HDC dc){if(auto* s=Surface(id)){RECT r{};GetClientRect(hwnd,&r);SetStretchBltMode(dc,COLORONCOLOR);StretchBlt(dc,0,0,r.right,r.bottom,s->dc,0,0,s->width,s->height,SRCCOPY);}}
uint64_t GdiEngine::Stock(HGDIOBJ object,g::Kind kind,uint32_t pid,uint32_t tid){for(auto& r:resources)if(r.id&&r.stock&&r.object==object&&r.pid==pid&&r.tid==tid)return r.id;auto* r=Allocate(kind,pid,tid);if(!r)return 0;r->object=object;r->stock=true;return r->id;}
void GdiEngine::CloseDc(GdiResource& r){if(r.paint){auto* s=Surface(r.window);if(s)EndPaint(s->hwnd,&r.ps);}if(r.dc){RestoreDC(r.dc,1);if(!r.window)DeleteDC(r.dc);else Present(r);}r={};}
void GdiEngine::Delete(GdiResource& r){if(!r.stock&&r.object){DeleteObject(r.object);if(r.kind==g::Kind::Bitmap)bytes-=uint64_t(r.width)*r.height*4;}r={};}
void GdiEngine::DropWindow(uint64_t id){for(auto& r:resources)if(r.id&&r.window==id)CloseDc(r);if(auto* s=Surface(id))Drop(*s);}
void GdiEngine::Exit(uint32_t pid,uint32_t tid){for(auto& r:resources)if(r.id&&r.pid==pid&&r.tid==tid&&r.kind==g::Kind::Dc)CloseDc(r);for(auto& r:resources)if(r.id&&r.pid==pid&&r.tid==tid)Delete(r);}
void GdiEngine::Clear(){for(auto& r:resources)if(r.id&&r.kind==g::Kind::Dc)CloseDc(r);for(auto& r:resources)if(r.id)Delete(r);for(auto& s:surfaces)if(s.id)Drop(s);}
GdiEngine::~GdiEngine(){Clear();}
#ifdef GUARDIAN_NATIVE_CONTROLS
HFONT GdiEngine::ControlFont(uint64_t id,uint32_t pid,uint32_t tid){auto* r=Find(id);if(!r||r->kind!=g::Kind::Font||r->pid!=pid||r->tid!=tid)return nullptr;++r->pins;return static_cast<HFONT>(r->object);}
uint64_t GdiEngine::DefaultControlFont(uint32_t pid,uint32_t tid){return Stock(GetStockObject(DEFAULT_GUI_FONT),g::Kind::Font,pid,tid);}
void GdiEngine::UnpinFont(uint64_t id){if(auto* r=Find(id);r&&r->pins)--r->pins;}
uint64_t GdiEngine::SystemBrush(int index,uint32_t pid,uint32_t tid){return Stock(GetSysColorBrush(index),g::Kind::Brush,pid,tid);}
HBITMAP GdiEngine::IconBitmap(uint64_t id,uint32_t pid,uint32_t tid,int& width,int& height){auto* r=Find(id);if(!r||r->kind!=g::Kind::Bitmap||r->stock||r->pid!=pid||r->tid!=tid||Selected(id)||r->width>64||r->height>64)return nullptr;width=r->width;height=r->height;GdiFlush();return static_cast<HBITMAP>(r->object);}
#endif
void GdiEngine::Report(g::Reply& reply) const {for(const auto& r:resources)if(r.id)++reply.live[r.kind==g::Kind::Dc?0:1];for(const auto& s:surfaces)if(s.id)++reply.live[2];std::copy(std::begin(calls),std::end(calls),std::begin(reply.calls));reply.pixelBytes=bytes;reply.uploaded=uploaded;reply.downloaded=downloaded;}
uint32_t GdiEngine::Execute(const g::Command& c,g::Reply& reply,uint32_t pid,uint32_t tid,const Resolve& resolve){
#ifdef GUARDIAN_GDI_EXPANDED
    if(!guardian::expandedgdi::Valid(c))return ERROR_INVALID_PARAMETER;
    if(guardian::expandedgdi::Extended(c.op))return ExecuteExpanded(c,reply,pid,tid);
#else
    if(!g::Valid(c))return ERROR_INVALID_PARAMETER;
#endif
    auto* r=Find(c.dc);auto* object=Find(c.object);auto own=[&](GdiResource* p){return p&&p->pid==pid&&p->tid==tid;};
    if(c.op==g::Op::Flush){GdiFlush();for(auto& item:resources)if(item.id&&item.pid==pid&&item.tid==tid&&item.window)Present(item);++calls[uint32_t(c.op)];return 0;}
    if(c.op==g::Op::Stock){const auto index=c.args[0];if(!((index>=0&&index<=8)||(index>=10&&index<=
#ifdef GUARDIAN_GDI_EXPANDED
19
#else
17
#endif
)))return ERROR_NOT_SUPPORTED;const auto native=GetStockObject(index);const auto kind=index<=5||index==18?g::Kind::Brush:index<=8||index==19?g::Kind::Pen:g::Kind::Font;if(index==15)return ERROR_NOT_SUPPORTED;reply.value=Stock(native,kind,pid,tid);if(!reply.value)return Quota;++calls[uint32_t(c.op)];return 0;}
    if(c.op==g::Op::Brush||c.op==g::Op::Pen||c.op==g::Op::Font||c.op==g::Op::Bitmap||c.op==g::Op::Region){
        const auto kind=c.op==g::Op::Brush?g::Kind::Brush:c.op==g::Op::Pen?g::Kind::Pen:c.op==g::Op::Font?g::Kind::Font:c.op==g::Op::Bitmap?g::Kind::Bitmap:g::Kind::Region;
        if(kind==g::Kind::Font&&!Face(c.font.face))return ERROR_NOT_SUPPORTED;
        if(kind==g::Kind::Bitmap&&bytes+uint64_t(c.args[0])*c.args[1]*4>g::MaxBytes)return Quota;
        auto* slot=Allocate(kind,pid,tid);if(!slot)return Quota;
        if(kind==g::Kind::Brush)slot->object=CreateSolidBrush(c.color);
        if(kind==g::Kind::Pen)slot->object=CreatePen(PS_SOLID,c.args[0],c.color);
        if(kind==g::Kind::Font){LOGFONTW font{};static_assert(sizeof(font)==sizeof(c.font));std::memcpy(&font,&c.font,sizeof(font));slot->object=CreateFontIndirectW(&font);}
        if(kind==g::Kind::Region)slot->object=CreateRectRgn(c.rect.left,c.rect.top,c.rect.right,c.rect.bottom);
        if(kind==g::Kind::Bitmap){slot->width=c.args[0];slot->height=c.args[1];slot->object=Dib(slot->width,slot->height,&slot->pixels);if(slot->object){std::memset(slot->pixels,0,size_t(slot->width)*slot->height*4);bytes+=uint64_t(slot->width)*slot->height*4;}}
        if(!slot->object){*slot={};return ERROR_NOT_ENOUGH_MEMORY;}reply.value=slot->id;++calls[uint32_t(c.op)];return 0;
    }
    if(c.op==g::Op::Dc||c.op==g::Op::Paint||c.op==g::Op::MemoryDc){
        const auto count=std::count_if(resources.begin(),resources.end(),[](const auto& item){return item.id&&item.kind==g::Kind::Dc;});if(count>=g::MaxDcs)return Quota;
        GdiSurface* s=nullptr;if(c.op!=g::Op::MemoryDc){const auto hwnd=resolve(c.dc,pid,tid);if(!hwnd)return Bad;s=Surface(c.dc);if(!s)return Bad;const auto error=Resize(*s);if(error)return error;for(const auto& item:resources)if(item.id&&item.window==s->id)return ERROR_BUSY;}
        else if(c.dc&&(!own(r)||r->kind!=g::Kind::Dc))return Bad;
        auto* slot=Allocate(g::Kind::Dc,pid,tid);if(!slot)return Quota;slot->window=s?s->id:0;slot->dc=s?s->dc:CreateCompatibleDC(nullptr);if(!slot->dc){*slot={};return ERROR_NOT_ENOUGH_MEMORY;}SaveDC(slot->dc);
        const int types[]{OBJ_PEN,OBJ_BRUSH,OBJ_FONT,OBJ_BITMAP};for(int i=0;i<4;++i){if(i==3&&s)continue;slot->selected[i]=Stock(GetCurrentObject(slot->dc,types[i]),Type(types[i]),pid,tid);if(!slot->selected[i]){CloseDc(*slot);return Quota;}}
        if(c.op==g::Op::Paint){RECT dirty=s->pending?s->dirty:RECT{};const auto dpi=GetDpiForWindow(s->hwnd);RECT physical{MulDiv(dirty.left,int(dpi),96),MulDiv(dirty.top,int(dpi),96),MulDiv(dirty.right,int(dpi),96),MulDiv(dirty.bottom,int(dpi),96)};if(s->pending)InvalidateRect(s->hwnd,&physical,FALSE);slot->paint=true;BeginPaint(s->hwnd,&slot->ps);IntersectClipRect(slot->dc,dirty.left,dirty.top,dirty.right,dirty.bottom);s->pending=false;reply.rect={dirty.left,dirty.top,dirty.right,dirty.bottom};}
        reply.value=slot->id;std::copy(std::begin(slot->selected),std::end(slot->selected),std::begin(reply.selected));++calls[uint32_t(c.op)];return 0;
    }
    if(!own(r))return Bad;
    if(c.op==g::Op::Delete){if(r->kind==g::Kind::Dc)return Bad;if(r->stock){reply.value=1;return 0;}if(Selected(r->id))return ERROR_BUSY;Delete(*r);reply.value=1;++calls[uint32_t(c.op)];return 0;}
    if(c.op==g::Op::Upload||c.op==g::Op::Read){if(r->kind!=g::Kind::Bitmap||r->stock||!r->pixels||uint64_t(c.offset)+c.count>uint64_t(r->width)*r->height)return ERROR_INVALID_PARAMETER;GdiFlush();if(c.op==g::Op::Upload){if(uploaded+uint64_t(c.count)*4>8u<<20)return Quota;std::memcpy(static_cast<uint32_t*>(r->pixels)+c.offset,c.pixels,c.count*4);uploaded+=c.count*4;}else{std::memcpy(reply.pixels,static_cast<uint32_t*>(r->pixels)+c.offset,c.count*4);downloaded+=c.count*4;}reply.value=c.count;++calls[uint32_t(c.op)];return 0;}
    if(r->kind!=g::Kind::Dc)return Bad;
#ifdef GUARDIAN_GDI_EXPANDED
    if(r->pathOpen){
        if(c.op==g::Op::OutputText||c.op==g::Op::FormatText)return ERROR_NOT_SUPPORTED;
        const uint32_t cost=c.op==g::Op::Rectangle?5u:c.op==g::Op::Move||c.op==g::Op::Line?1u:0u;
        if(r->pathPoints+cost>guardian::expandedgdi::MaxPathPoints)return Quota;
        r->pathPoints+=cost;
    }
#endif
    switch(c.op){
    case g::Op::Release:case g::Op::EndPaint:case g::Op::DeleteDc:if((c.op==g::Op::EndPaint)!=r->paint||(c.op==g::Op::DeleteDc)!=(r->window==0))return ERROR_INVALID_PARAMETER;CloseDc(*r);reply.value=1;break;
    case g::Op::Select:{if(!own(object))return Bad;const auto index=Index(object->kind);if(index<0||(index==3&&r->window))return ERROR_NOT_SUPPORTED;if(index==3&&!object->stock&&Selected(object->id)&&r->selected[3]!=object->id)return ERROR_BUSY;const auto previous=SelectObject(r->dc,object->object);if(!previous||previous==HGDI_ERROR)return ERROR_INVALID_PARAMETER;reply.value=r->selected[index];r->selected[index]=object->id;break;}
    case g::Op::Current:{const auto index=Index(Type(c.args[0]));if(index<0)return ERROR_NOT_SUPPORTED;reply.value=r->selected[index];break;}
    case g::Op::Save:if(r->depth==8)return Quota;if(!SaveDC(r->dc))return ERROR_GEN_FAILURE;std::copy(std::begin(r->selected),std::end(r->selected),r->saved[r->depth++]);reply.value=r->depth;break;
    case g::Op::Restore:{int level=c.args[0];if(level<0)level=r->depth+level+1;if(level<1||level>r->depth)return ERROR_INVALID_PARAMETER;if(!RestoreDC(r->dc,level+1))return ERROR_GEN_FAILURE;std::copy(std::begin(r->saved[level-1]),std::end(r->saved[level-1]),std::begin(r->selected));r->depth=level-1;reply.value=1;std::copy(std::begin(r->selected),std::end(r->selected),std::begin(reply.selected));break;}
    case g::Op::Clip:reply.value=IntersectClipRect(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom);break;
    case g::Op::ClipRegion:if(c.object&&(!own(object)||object->kind!=g::Kind::Region))return Bad;reply.value=SelectClipRgn(r->dc,c.object?static_cast<HRGN>(object->object):nullptr);break;
    case g::Op::Fill:{if(!own(object)||object->kind!=g::Kind::Brush)return Bad;RECT rect{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};reply.value=FillRect(r->dc,&rect,static_cast<HBRUSH>(object->object));break;}
    case g::Op::Rectangle:reply.value=Rectangle(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom);break;
    case g::Op::Move:{POINT previous{};reply.value=MoveToEx(r->dc,c.args[0],c.args[1],&previous);reply.x=previous.x;reply.y=previous.y;break;}
    case g::Op::Line:reply.value=LineTo(r->dc,c.args[0],c.args[1]);break;
    case g::Op::TextColor:reply.value=SetTextColor(r->dc,c.color);break;
    case g::Op::BkColor:reply.value=SetBkColor(r->dc,c.color);break;
    case g::Op::BkMode:if(c.args[0]!=TRANSPARENT&&c.args[0]!=OPAQUE)return ERROR_INVALID_PARAMETER;reply.value=SetBkMode(r->dc,c.args[0]);break;
    case g::Op::OutputText:reply.value=TextOutW(r->dc,c.args[0],c.args[1],reinterpret_cast<LPCWSTR>(c.text),int(c.count));break;
    case g::Op::FormatText:{RECT rect{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};reply.value=uint64_t(DrawTextW(r->dc,reinterpret_cast<LPCWSTR>(c.text),int(c.count),&rect,c.flags));reply.rect={rect.left,rect.top,rect.right,rect.bottom};break;}
    case g::Op::Extent:{SIZE size{};reply.value=GetTextExtentPoint32W(r->dc,reinterpret_cast<LPCWSTR>(c.text),int(c.count),&size);reply.x=size.cx;reply.y=size.cy;break;}
    case g::Op::Metrics:{TEXTMETRICW tm{};if(!GetTextMetricsW(r->dc,&tm))return ERROR_GEN_FAILURE;static_assert(sizeof(tm)==sizeof(reply.metrics));std::memcpy(&reply.metrics,&tm,57);GetTextFaceW(r->dc,32,reinterpret_cast<LPWSTR>(reply.face));reply.value=1;break;}
    case g::Op::BitBlt:{auto* from=Find(c.other);if(!own(from)||from->kind!=g::Kind::Dc)return Bad;if(c.rect.right-c.rect.left!=c.source.right-c.source.left||c.rect.bottom-c.rect.top!=c.source.bottom-c.source.top)return ERROR_INVALID_PARAMETER;reply.value=BitBlt(r->dc,c.rect.left,c.rect.top,c.rect.right-c.rect.left,c.rect.bottom-c.rect.top,from->dc,c.source.left,c.source.top,SRCCOPY);break;}
    case g::Op::Stretch:if(!own(object)||object->kind!=g::Kind::Bitmap||object->stock||!object->pixels)return Bad;else if(c.source.left<0||c.source.top<0||c.source.right>object->width||c.source.bottom>object->height)return ERROR_INVALID_PARAMETER;else{BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=object->width;info.bmiHeader.biHeight=-object->height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;GdiFlush();reply.value=uint64_t(StretchDIBits(r->dc,c.rect.left,c.rect.top,c.rect.right-c.rect.left,c.rect.bottom-c.rect.top,c.source.left,c.source.top,c.source.right-c.source.left,c.source.bottom-c.source.top,object->pixels,&info,DIB_RGB_COLORS,SRCCOPY));}break;
    case g::Op::Pixel:reply.value=GetPixel(r->dc,c.args[0],c.args[1]);break;
    case g::Op::Caps:switch(c.args[0]){case LOGPIXELSX:case LOGPIXELSY:reply.value=96;break;case BITSPIXEL:reply.value=32;break;case PLANES:reply.value=1;break;default:return ERROR_NOT_SUPPORTED;}break;
    default:return ERROR_NOT_SUPPORTED;
    }
    ++calls[uint32_t(c.op)];return 0;
}
#ifdef GUARDIAN_GDI_EXPANDED
#include "ExpandedGdiEngine.inl"
#endif

}
