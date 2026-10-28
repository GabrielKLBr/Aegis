#pragma once
#include "guardian/NativeGdi.h"
#include <Windows.h>
#include <array>
#include <functional>
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
#include "SoftwareGraphicsEngine.h"
#endif
namespace av::microvm::native_gui {
namespace g=guardian::nativegdi;
struct GdiResource {
    uint64_t id=0,window=0;uint32_t pid=0,tid=0;g::Kind kind{};
    HGDIOBJ object=nullptr;HDC dc=nullptr;void* pixels=nullptr;int width=0,height=0;
    uint32_t pins=0;
#ifdef GUARDIAN_GDI_EXPANDED
    bool pathOpen=false;uint32_t pathPoints=0;
#endif
bool stock=false,paint=false;PAINTSTRUCT ps{};uint64_t selected[4]{},saved[8][4]{};int depth=0;
};
struct GdiSurface {uint64_t id=0;HWND hwnd=nullptr;HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;int width=0,height=0;RECT dirty{};bool pending=true;};
class GdiEngine {
public:
#ifdef GUARDIAN_SOFTWARE_GRAPHICS
    std::unique_ptr<SoftwareGraphicsEngine> software;
    void SoftwareFinal(g::Reply& reply) const {if(software)software->Final(reply);}
#endif
    using Resolve=std::function<HWND(uint64_t,uint32_t,uint32_t)>;
    std::array<GdiResource,g::MaxObjects> resources{};std::array<GdiSurface,g::MaxSurfaces> surfaces{};
    uint64_t session=0,next=1,bytes=0,uploaded=0,downloaded=0;uint32_t calls[uint32_t(g::Op::Caps)+1]{};
    ~GdiEngine();
    uint32_t AddWindow(uint64_t,HWND);void DropWindow(uint64_t);void Exit(uint32_t,uint32_t);void Clear();
    void Paint(uint64_t,HWND,HDC,const RECT&);void Print(uint64_t,HWND,HDC);
    uint32_t Execute(const g::Command&,g::Reply&,uint32_t,uint32_t,const Resolve&);
    void Report(g::Reply&) const;
#ifdef GUARDIAN_GDI_EXPANDED
    uint64_t PaintWindow(uint64_t,uint32_t,uint32_t);
    bool PendingPaint(uint64_t) const;
#endif
#ifdef GUARDIAN_NATIVE_CONTROLS
    HFONT ControlFont(uint64_t id,uint32_t pid,uint32_t tid);
    uint64_t DefaultControlFont(uint32_t pid,uint32_t tid);
    void UnpinFont(uint64_t id);
    uint64_t SystemBrush(int index,uint32_t pid,uint32_t tid);
    HBITMAP IconBitmap(uint64_t id,uint32_t pid,uint32_t tid,int& width,int& height);
#endif
private:
#ifdef GUARDIAN_GDI_EXPANDED
    uint32_t ExecuteExpanded(const g::Command&,g::Reply&,uint32_t,uint32_t);
#endif
    GdiResource* Find(uint64_t);GdiResource* Allocate(g::Kind,uint32_t,uint32_t);GdiSurface* Surface(uint64_t);
    bool Selected(uint64_t) const;void CloseDc(GdiResource&);void Delete(GdiResource&);void Drop(GdiSurface&);
    uint32_t Resize(GdiSurface&);uint64_t Stock(HGDIOBJ,g::Kind,uint32_t,uint32_t);void Present(GdiResource&);
};
}
