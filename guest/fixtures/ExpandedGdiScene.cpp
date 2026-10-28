#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
namespace {
constexpr int W=192,H=144;
unsigned checks=0,failures=0,paints=0,keys=0,clicks=0;
HDC canvas=nullptr;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("EXPANDED_FAIL %s error=%lu\n",name,GetLastError());}}
uint64_t Hash32(const uint32_t* p){uint64_t h=14695981039346656037ull;for(unsigned i=0;i<W*H;++i){h^=p[i]&0xFFFFFF;h*=1099511628211ull;}return h;}
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT){InvalidateRect(h,nullptr,TRUE);PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);if(dc&&canvas)BitBlt(dc,0,0,W,H,canvas,0,0,SRCCOPY);if(dc)EndPaint(h,&p);++paints;return 0;}if(m==WM_CHAR){if(w==L'A'||w==0x00E1)++keys;return 0;}if(m==WM_LBUTTONUP){++clicks;return 0;}if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
void Page(HDC dc,unsigned page){
    const auto level=SaveDC(dc);RECT all{0,0,W,H};FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    if(page==0){
        auto brush=CreateHatchBrush(HS_DIAGCROSS,RGB(80,30,180));auto pen=CreatePen(PS_DASH,0,RGB(0,90,40));Check(brush&&pen,"hatch and styled pen");auto b=SelectObject(dc,brush),p=SelectObject(dc,pen);
        Check(Ellipse(dc,4,4,70,60)&&RoundRect(dc,74,4,150,60,16,20),"native curved shapes");
        Check(Arc(dc,10,64,70,125,5,60,80,130)&&Chord(dc,76,64,130,125,74,60,136,130)&&Pie(dc,136,64,190,128,130,60,190,140),"arcs chord pie");
        POINT line[]{{4,4},{80,68},{140,6},{188,60}};Check(Polyline(dc,line,4)&&PolyBezier(dc,line,4),"polyline and Bezier");POINT polygon[]{{5,90},{25,70},{40,110},{5,130}};Check(Polygon(dc,polygon,4),"polygon");
        SetPolyFillMode(dc,WINDING);Check(GetPolyFillMode(dc)==WINDING,"fill state");int counts[]{4,4};POINT polygons[]{{4,4},{20,4},{20,20},{4,20},{24,4},{40,4},{40,20},{24,20}};Check(PolyPolygon(dc,polygons,counts,2),"poly polygon");
        Check(SetArcDirection(dc,AD_CLOCKWISE)!=0&&GetArcDirection(dc)==AD_CLOCKWISE,"arc direction");
        SelectObject(dc,b);SelectObject(dc,p);Check(DeleteObject(brush)&&DeleteObject(pen),"shape resources");
    }else if(page==1){
        const auto a=CreateEllipticRgn(8,8,110,110),b=CreateRoundRectRgn(55,30,180,132,20,20),c=CreateRectRgn(0,0,0,0);
        Check(a&&b&&c,"region creation");Check(CombineRgn(c,a,b,RGN_XOR)!=ERROR,"region xor");RECT box{};Check(GetRgnBox(c,&box)!=ERROR&&PtInRegion(a,50,50)&&!PtInRegion(a,1,1),"region bounds and membership");RECT inside{30,30,50,50};Check(RectInRegion(a,&inside)&&OffsetRgn(c,2,1)!=ERROR,"region rectangle and offset");
        Check(ExtSelectClipRgn(dc,c,RGN_COPY)!=ERROR&&GetClipBox(dc,&box)!=ERROR,"extended clip");SetDCBrushColor(dc,RGB(20,80,190));FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        Check(ExcludeClipRect(dc,70,40,100,80)!=ERROR&&OffsetClipRgn(dc,1,1)!=ERROR,"exclude and offset clip");SetDCBrushColor(dc,RGB(230,120,30));FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        Check(ExtSelectClipRgn(dc,nullptr,RGN_COPY)!=ERROR&&SetRectRgn(c,0,0,5,5),"clear clip and region reset");Check(DeleteObject(a)&&DeleteObject(b)&&DeleteObject(c),"region cleanup");
    }else if(page==2){
        Check(SetGraphicsMode(dc,GM_ADVANCED)!=0&&GetGraphicsMode(dc)==GM_ADVANCED,"advanced graphics");XFORM f{1.f,.125f,-.125f,1.f,15.f,8.f},found{};Check(SetWorldTransform(dc,&f)&&GetWorldTransform(dc,&found)&&std::memcmp(&f,&found,sizeof(f))==0,"world transform");XFORM shift{1,0,0,1,3,4};Check(ModifyWorldTransform(dc,&shift,MWT_RIGHTMULTIPLY),"combined world transform");
        SetDCBrushColor(dc,RGB(90,180,40));auto b=SelectObject(dc,GetStockObject(DC_BRUSH));Rectangle(dc,5,5,110,90);POINT points[]{{0,0},{100,40},{40,100}};Check(LPtoDP(dc,points,3)&&DPtoLP(dc,points,3),"point conversion");SelectObject(dc,b);Check(ModifyWorldTransform(dc,nullptr,MWT_IDENTITY),"identity world transform");
        Check(SetMapMode(dc,MM_ANISOTROPIC)!=0&&GetMapMode(dc)==MM_ANISOTROPIC,"mapping mode");SIZE old{},size{};POINT oldPoint{},point{};Check(SetWindowExtEx(dc,100,100,&old)&&SetViewportExtEx(dc,120,110,&old)&&GetWindowExtEx(dc,&size)&&size.cx==100,"mapping extents");
        Check(SetWindowOrgEx(dc,2,3,&oldPoint)&&GetWindowOrgEx(dc,&point)&&point.x==2&&SetViewportOrgEx(dc,10,12,&oldPoint)&&GetViewportOrgEx(dc,&point),"mapping origins");Check(GetViewportExtEx(dc,&size)&&size.cx==120,"viewport extent");Check(SetBrushOrgEx(dc,2,1,&oldPoint)&&GetBrushOrgEx(dc,&point),"brush origin");MoveToEx(dc,10,10,nullptr);Check(GetCurrentPositionEx(dc,&point)&&point.x==10&&point.y==10,"current point");LineTo(dc,130,110);
    }else if(page==3){
        Check(BeginPath(dc),"begin path");MoveToEx(dc,10,10,nullptr);POINT p[]{{40,6},{90,110},{150,20}};Check(PolyBezierTo(dc,p,3),"Bezier path");POINT line[]{{170,110},{20,125}};Check(PolylineTo(dc,line,2)&&CloseFigure(dc)&&EndPath(dc),"path geometry");const auto count=GetPath(dc,nullptr,nullptr,0);POINT points[32]{};BYTE types[32]{};Check(count>0&&count<=32&&GetPath(dc,points,types,32)==count,"path return");Check(StrokeAndFillPath(dc),"stroke fill path");
        Check(BeginPath(dc)&&Ellipse(dc,20,30,110,100)&&EndPath(dc)&&FlattenPath(dc),"flatten path");Check(StrokePath(dc),"stroke flattened");
        Check(BeginPath(dc)&&Rectangle(dc,70,40,165,130)&&EndPath(dc),"region path");auto region=PathToRegion(dc);Check(region!=nullptr&&DeleteObject(region),"path region");Check(BeginPath(dc)&&RoundRect(dc,30,30,170,120,10,10)&&EndPath(dc)&&SelectClipPath(dc,RGN_COPY),"path clip");PatBlt(dc,0,0,W,H,DSTINVERT);SelectClipRgn(dc,nullptr);
        Check(BeginPath(dc)&&MoveToEx(dc,20,20,nullptr)&&LineTo(dc,70,50)&&EndPath(dc)&&WidenPath(dc)&&FillPath(dc),"widen fill");Check(BeginPath(dc)&&AbortPath(dc),"path abort");
    }else if(page==4){
        auto src=CreateCompatibleDC(dc);BITMAPINFO i{};i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=16;i.bmiHeader.biHeight=16;i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=24;void* pixels=nullptr;auto bitmap=CreateDIBSection(dc,&i,0,&pixels,nullptr,0);Check(src&&bitmap&&pixels,"24 bit bottom up DIB");
        auto raw=static_cast<BYTE*>(pixels);for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){auto* p=raw+y*48+x*3;p[0]=BYTE(x*13);p[1]=BYTE(y*13);p[2]=BYTE((x^y)*13);}
        auto old=SelectObject(src,bitmap);BITMAP object{};Check(GetObjectW(bitmap,sizeof(object),&object)==sizeof(object)&&object.bmBits==pixels&&object.bmBitsPixel==24,"bitmap descriptor guest pointer");
        Check(SetStretchBltMode(dc,HALFTONE)!=0&&GetStretchBltMode(dc)==HALFTONE,"stretch mode");Check(StretchBlt(dc,4,4,70,70,src,0,0,16,16,SRCCOPY),"stretch blit");Check(BitBlt(dc,80,4,16,16,src,0,0,SRCINVERT),"raster source xor");Check(PatBlt(dc,4,85,120,40,PATINVERT),"pattern raster");
        Check(SetROP2(dc,R2_XORPEN)!=0&&GetROP2(dc)==R2_XORPEN,"rop2 state");SetDCPenColor(dc,RGB(30,90,140));Check(GetDCPenColor(dc)==RGB(30,90,140),"DC pen state");auto p=SelectObject(dc,GetStockObject(DC_PEN));MoveToEx(dc,0,0,nullptr);LineTo(dc,180,140);SelectObject(dc,p);
        Check(SetDCBrushColor(dc,RGB(190,20,40))!=CLR_INVALID&&GetDCBrushColor(dc)==RGB(190,20,40),"DC brush state");SetPixel(src,3,4,RGB(1,2,3));Check(GdiFlush()!=FALSE,"24 bit storage synchronization");const auto row=raw+(15-4)*48+3*3;Check(row[0]==3&&row[1]==2&&row[2]==1,"24 bit CPU coherence");
        Check(StretchDIBits(dc,130,75,48,48,0,0,16,16,raw,&i,0,SRCCOPY)>0,"24 bit bottom up source");i.bmiHeader.biHeight=-16;Check(StretchDIBits(dc,130,4,48,48,0,0,16,16,raw,&i,0,SRCCOPY)>0,"24 bit top down source");
        SelectObject(src,old);Check(DeleteObject(bitmap)&&DeleteDC(src),"raster cleanup");
    }else{
        const auto font=CreateFontW(-19,0,0,0,400,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,NONANTIALIASED_QUALITY,0,L"Tahoma");Check(font!=nullptr,"expanded font catalogue");auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(20,30,70));SetBkColor(dc,RGB(230,235,245));Check(GetTextColor(dc)==RGB(20,30,70)&&GetBkColor(dc)==RGB(230,235,245),"text color state");
        Check(SetTextAlign(dc,TA_LEFT|TA_TOP)!=GDI_ERROR&&GetTextAlign(dc)==(TA_LEFT|TA_TOP),"text alignment");SetTextCharacterExtra(dc,1);Check(GetTextCharacterExtra(dc)==1,"text spacing");Check(SetTextJustification(dc,2,1),"text justification");RECT r{2,2,190,40};int dx[]{16,17,16,16,15,16,16};Check(ExtTextOutW(dc,4,5,ETO_CLIPPED|ETO_OPAQUE,&r,L"Olá Ω Ж",7,dx),"Unicode spaced text");
        WORD glyphs[8]{};const auto n=GetGlyphIndicesW(dc,L"Glyph",5,glyphs,GGI_MARK_NONEXISTING_GLYPHS);Check(n==5&&ExtTextOutW(dc,4,42,ETO_GLYPH_INDEX,nullptr,reinterpret_cast<LPCWSTR>(glyphs),5,nullptr),"glyph output");
        SIZE size{};int fit=0,advances[32]{};Check(GetTextExtentExPointW(dc,L"Extent sample",13,75,&fit,advances,&size)&&fit>=1,"text cumulative metrics");int widths[4]{};ABC abc[4]{};Check(GetCharWidth32W(dc,L'A',L'D',widths)&&GetCharABCWidthsW(dc,L'A',L'D',abc),"Unicode character widths");Check(GetCharWidth32A(dc,'A','D',widths)&&GetCharABCWidthsA(dc,'A','D',abc),"ANSI character widths");
        Check(ExtTextOutA(dc,4,70,0,nullptr,"Ol\xE1",3,nullptr)&&GetGlyphIndicesA(dc,"AB",2,glyphs,0)==2,"ANSI CP1252 and glyphs");Check(GetTextExtentExPointA(dc,"Extent",6,70,&fit,advances,&size)&&GetTextExtentPoint32A(dc,"Extent",6,&size),"ANSI extents");RECT text{4,96,190,132};Check(DrawTextA(dc,"ANSI text",-1,&text,DT_SINGLELINE|DT_NOPREFIX)>0,"ANSI DrawText");
        TEXTMETRICA metrics{};char face[32]{};Check(GetTextMetricsA(dc,&metrics)&&GetTextFaceA(dc,32,face)>0&&GetObjectType(font)==OBJ_FONT,"ANSI font descriptors");LOGFONTA fa{};LOGFONTW fw{};Check(GetObjectA(font,sizeof(fa),&fa)==sizeof(fa)&&GetObjectW(font,sizeof(fw),&fw)==sizeof(fw),"font A W object descriptors");
        std::printf("EXPANDED_METRICS %ld %ld %d %d %d %d %s\n",size.cx,size.cy,fit,advances[0],widths[0],abc[0].abcA,face);SelectObject(dc,old);Check(DeleteObject(font),"font release");
    }
    Check(RestoreDC(dc,level)!=FALSE,"page state restore");
}
}
int main(){
    WNDCLASSW c{};c.lpfnWndProc=Proc;c.lpszClassName=L"Aegis.ExpandedGdi.Gallery";c.hInstance=GetModuleHandleW(nullptr);Check(RegisterClassW(&c)!=0,"gallery class");auto window=CreateWindowExW(0,c.lpszClassName,L"14.10E GDI Gallery",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,nullptr,nullptr,c.hInstance,nullptr);Check(window!=nullptr,"native gallery window");if(!window)return 1;ShowWindow(window,SW_SHOWDEFAULT);Check(IsWindowVisible(window)!=FALSE,"guest startup default visibility");
    canvas=CreateCompatibleDC(nullptr);BITMAPINFO i{};i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=W;i.bmiHeader.biHeight=-H;i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=32;void* pixels=nullptr;auto bitmap=CreateDIBSection(canvas,&i,0,&pixels,nullptr,0);Check(canvas&&bitmap&&pixels,"gallery bitmap");if(!bitmap)return 2;auto old=SelectObject(canvas,bitmap);uint64_t hashes[6]{};
    for(unsigned page=0;page<6;++page){Page(canvas,page);Check(GdiFlush()!=FALSE,"page synchronization");hashes[page]=Hash32(static_cast<uint32_t*>(pixels));}
    SetWindowTextW(window,L"14.10E GDI Gallery ready");InvalidateRect(window,nullptr,TRUE);UpdateWindow(window);std::printf("EXPANDED_RESULT checks=%u failures=%u hashes=",checks,failures);for(auto h:hashes)std::printf("%016llx,",static_cast<unsigned long long>(h));std::puts("");std::fflush(stdout);
#ifdef EXPANDED_GDI_AUTOTEST
    DestroyWindow(window);
#else
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    std::printf("EXPANDED_INPUT keys=%u clicks=%u paints=%u\n",keys,clicks,paints);
#endif
    auto dc=canvas;canvas=nullptr;SelectObject(dc,old);Check(DeleteObject(bitmap)&&DeleteDC(dc),"gallery cleanup");return failures?3:0;
}

