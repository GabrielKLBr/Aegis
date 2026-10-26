#pragma once
#include "guardian/NativeGdi.h"
#include <Windows.h>
#include <array>
#include <functional>
namespace av::microvm::native_gui {
namespace g=guardian::nativegdi;
struct GdiResource {
    uint64_t id=0,window=0;uint32_t pid=0,tid=0;g::Kind kind{};
    HGDIOBJ object=nullptr;HDC dc=nullptr;void* pixels=nullptr;int width=0,height=0;
    bool stock=false,paint=false;PAINTSTRUCT ps{};uint64_t selected[4]{},saved[8][4]{};int depth=0;
};
struct GdiSurface {uint64_t id=0;HWND hwnd=nullptr;HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;int width=0,height=0;RECT dirty{};bool pending=true;};
class GdiEngine {
public:
    using Resolve=std::function<HWND(uint64_t,uint32_t,uint32_t)>;
    std::array<GdiResource,g::MaxObjects> resources{};std::array<GdiSurface,g::MaxSurfaces> surfaces{};
    uint64_t session=0,next=1,bytes=0,uploaded=0,downloaded=0;uint32_t calls[uint32_t(g::Op::Caps)+1]{};
    ~GdiEngine();
    uint32_t AddWindow(uint64_t,HWND);void DropWindow(uint64_t);void Exit(uint32_t,uint32_t);void Clear();
    void Paint(uint64_t,HWND,HDC,const RECT&);void Print(uint64_t,HWND,HDC);
    uint32_t Execute(const g::Command&,g::Reply&,uint32_t,uint32_t,const Resolve&);
    void Report(g::Reply&) const;
private:
    GdiResource* Find(uint64_t);GdiResource* Allocate(g::Kind,uint32_t,uint32_t);GdiSurface* Surface(uint64_t);
    bool Selected(uint64_t) const;void CloseDc(GdiResource&);void Delete(GdiResource&);void Drop(GdiSurface&);
    uint32_t Resize(GdiSurface&);uint64_t Stock(HGDIOBJ,g::Kind,uint32_t,uint32_t);void Present(GdiResource&);
};
}
