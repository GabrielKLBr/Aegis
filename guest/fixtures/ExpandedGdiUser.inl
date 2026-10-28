namespace {
g::Call ECall(e::Op op,HDC dc=nullptr){g::Call c;c.draw.op=static_cast<g::Op>(op);c.draw.dc=uint64_t(dc);return c;}
bool ESend(g::Call& c,bool draw=false){if(!e::Valid(c.draw)){SetLastError(ERROR_INVALID_PARAMETER);return false;}return draw?Draw(c):Graphics(c);}
uint64_t EState(HDC dc,e::State state,bool set,int value=0){auto c=ECall(set?e::Op::SetState:e::Op::GetState,dc);c.draw.flags=uint32_t(state);if(set)c.draw.args[0]=value;return ESend(c)?c.graphics.value:0;}
BOOL EPoint(HDC dc,e::PointState state,bool set,int x,int y,POINT* result){auto c=ECall(set?e::Op::SetPoint:e::Op::GetPoint,dc);c.draw.flags=uint32_t(state);if(set){c.draw.args[0]=x;c.draw.args[1]=y;}if(!ESend(c))return FALSE;if(result)*result={c.graphics.x,c.graphics.y};return BOOL(c.graphics.value);}
BOOL ERect(e::Op op,HDC dc,int left,int top,int right,int bottom,int a=0,int b=0,int d=0,int f=0){auto c=ECall(op,dc);c.draw.rect={left,top,right,bottom};c.draw.args[0]=a;c.draw.args[1]=b;c.draw.args[2]=d;c.draw.args[3]=f;return ESend(c,true)?BOOL(c.graphics.value):FALSE;}
BOOL EPoints(e::Op op,HDC dc,const POINT* points,DWORD count,const int* counts=nullptr,int polygons=0){if(!points||!count||count>e::MaxPoints||polygons<0||polygons>8)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(op,dc);c.draw.count=count;Copy(c.draw.pixels,points,size_t(count)*sizeof(POINT));if(counts){c.draw.flags=uint32_t(polygons);Copy(c.draw.args,counts,size_t(polygons)*sizeof(int));}return ESend(c,true)?BOOL(c.graphics.value):FALSE;}
BOOL EPath(HDC dc,uint32_t action,int mode=0){auto c=ECall(e::Op::Path,dc);c.draw.flags=action;c.draw.mode=uint32_t(mode);return ESend(c,true)?BOOL(c.graphics.value):FALSE;}
bool EAnsi(LPCSTR text,int count,g::Command& c){if(!text||count<0||count>127)return false;const auto n=MultiByteToWideChar(1252,0,text,count,reinterpret_cast<wchar_t*>(c.text),128);c.count=uint32_t(n);return n==count&&g::Text(c.text,c.count);}
BOOL ERaster(HDC dc,int x,int y,int width,int height,HDC source,int sx,int sy,int sw,int sh,DWORD rop,uint32_t kind){
    if(width<=0||height<=0||width>4096||height>4096||!e::Coord(x)||!e::Coord(y)||sw<0||sh<0||sw>4096||sh>4096||!e::Coord(sx)||!e::Coord(sy))return Fail(ERROR_INVALID_PARAMETER,FALSE);
    auto c=ECall(e::Op::Raster,dc);c.draw.flags=kind;c.draw.other=uint64_t(source);c.draw.color=rop;c.draw.rect={x,y,x+width,y+height};if(kind<2)c.draw.source={sx,sy,sx+sw,sy+sh};return ESend(c,true)?BOOL(c.graphics.value):FALSE;
}
}
extern "C" BOOL WINAPI UiEllipse(HDC dc,int l,int t,int r,int b){Guard lock;return ERect(e::Op::Ellipse,dc,l,t,r,b);}
extern "C" BOOL WINAPI UiRoundRect(HDC dc,int l,int t,int r,int b,int w,int h){Guard lock;return ERect(e::Op::RoundRect,dc,l,t,r,b,w,h);}
extern "C" BOOL WINAPI UiArc(HDC dc,int l,int t,int r,int b,int x1,int y1,int x2,int y2){Guard lock;return ERect(e::Op::Arc,dc,l,t,r,b,x1,y1,x2,y2);}
extern "C" BOOL WINAPI UiChord(HDC dc,int l,int t,int r,int b,int x1,int y1,int x2,int y2){Guard lock;return ERect(e::Op::Chord,dc,l,t,r,b,x1,y1,x2,y2);}
extern "C" BOOL WINAPI UiPie(HDC dc,int l,int t,int r,int b,int x1,int y1,int x2,int y2){Guard lock;return ERect(e::Op::Pie,dc,l,t,r,b,x1,y1,x2,y2);}
extern "C" BOOL WINAPI UiPolyline(HDC dc,const POINT* p,int count){Guard lock;return EPoints(e::Op::Polyline,dc,p,DWORD(count));}
extern "C" BOOL WINAPI UiPolygon(HDC dc,const POINT* p,int count){Guard lock;return EPoints(e::Op::Polygon,dc,p,DWORD(count));}
extern "C" BOOL WINAPI UiPolyBezier(HDC dc,const POINT* p,DWORD count){Guard lock;return EPoints(e::Op::Bezier,dc,p,count);}
extern "C" BOOL WINAPI UiPolylineTo(HDC dc,const POINT* p,DWORD count){Guard lock;return EPoints(e::Op::PolylineTo,dc,p,count);}
extern "C" BOOL WINAPI UiPolyBezierTo(HDC dc,const POINT* p,DWORD count){Guard lock;return EPoints(e::Op::BezierTo,dc,p,count);}
extern "C" BOOL WINAPI UiPolyPolygon(HDC dc,const POINT* p,const int* counts,int count){Guard lock;if(!counts||count<1||count>8)return Fail(ERROR_INVALID_PARAMETER,FALSE);DWORD total=0;for(int i=0;i<count;++i){if(counts[i]<2||counts[i]>32)return Fail(ERROR_INVALID_PARAMETER,FALSE);total+=DWORD(counts[i]);}return EPoints(e::Op::PolyPolygon,dc,p,total,counts,count);}
extern "C" HBRUSH WINAPI UiCreateHatchBrush(int style,COLORREF color){Guard lock;auto c=ECall(e::Op::Hatch);c.draw.flags=UINT(style);c.draw.color=color;return ESend(c)?reinterpret_cast<HBRUSH>(c.graphics.value):nullptr;}
extern "C" HPEN WINAPI UiCreatePen(int style,int width,COLORREF color){Guard lock;auto c=ECall(e::Op::StyledPen);c.draw.flags=UINT(style);c.draw.args[0]=width;c.draw.color=color;return ESend(c)?reinterpret_cast<HPEN>(c.graphics.value):nullptr;}
#define E_STATE_PAIR(SET,GET,STATE,TYPE) \
extern "C" TYPE WINAPI Ui##SET(HDC dc,TYPE n){Guard lock;return TYPE(EState(dc,e::State::STATE,true,int(n)));} \
extern "C" TYPE WINAPI Ui##GET(HDC dc){Guard lock;return TYPE(EState(dc,e::State::STATE,false));}
E_STATE_PAIR(SetROP2,GetROP2,Rop2,int)
E_STATE_PAIR(SetPolyFillMode,GetPolyFillMode,PolyFill,int)
E_STATE_PAIR(SetStretchBltMode,GetStretchBltMode,StretchMode,int)
E_STATE_PAIR(SetGraphicsMode,GetGraphicsMode,Graphics,int)
E_STATE_PAIR(SetMapMode,GetMapMode,MapMode,int)
E_STATE_PAIR(SetTextAlign,GetTextAlign,TextAlign,UINT)
E_STATE_PAIR(SetTextCharacterExtra,GetTextCharacterExtra,CharExtra,int)
E_STATE_PAIR(SetArcDirection,GetArcDirection,ArcDirection,int)
E_STATE_PAIR(SetDCPenColor,GetDCPenColor,DcPen,COLORREF)
E_STATE_PAIR(SetDCBrushColor,GetDCBrushColor,DcBrush,COLORREF)
#undef E_STATE_PAIR
#define E_POINT_PAIR(SET,GET,STATE) \
extern "C" BOOL WINAPI Ui##SET(HDC dc,int x,int y,POINT* old){Guard lock;return EPoint(dc,e::PointState::STATE,true,x,y,old);} \
extern "C" BOOL WINAPI Ui##GET(HDC dc,POINT* p){Guard lock;if(!p)return Fail(ERROR_INVALID_PARAMETER,FALSE);return EPoint(dc,e::PointState::STATE,false,0,0,p);}
E_POINT_PAIR(SetWindowOrgEx,GetWindowOrgEx,WindowOrg)
E_POINT_PAIR(SetViewportOrgEx,GetViewportOrgEx,ViewportOrg)
E_POINT_PAIR(SetBrushOrgEx,GetBrushOrgEx,BrushOrg)
#undef E_POINT_PAIR
extern "C" BOOL WINAPI UiSetWindowExtEx(HDC dc,int x,int y,SIZE* old){Guard lock;POINT p{};const auto ok=EPoint(dc,e::PointState::WindowExt,true,x,y,&p);if(ok&&old)*old={p.x,p.y};return ok;}
extern "C" BOOL WINAPI UiGetWindowExtEx(HDC dc,SIZE* out){Guard lock;if(!out)return Fail(ERROR_INVALID_PARAMETER,FALSE);POINT p{};const auto ok=EPoint(dc,e::PointState::WindowExt,false,0,0,&p);if(ok)*out={p.x,p.y};return ok;}
extern "C" BOOL WINAPI UiSetViewportExtEx(HDC dc,int x,int y,SIZE* old){Guard lock;POINT p{};const auto ok=EPoint(dc,e::PointState::ViewportExt,true,x,y,&p);if(ok&&old)*old={p.x,p.y};return ok;}
extern "C" BOOL WINAPI UiGetViewportExtEx(HDC dc,SIZE* out){Guard lock;if(!out)return Fail(ERROR_INVALID_PARAMETER,FALSE);POINT p{};const auto ok=EPoint(dc,e::PointState::ViewportExt,false,0,0,&p);if(ok)*out={p.x,p.y};return ok;}
extern "C" BOOL WINAPI UiGetCurrentPositionEx(HDC dc,POINT* p){Guard lock;if(!p)return Fail(ERROR_INVALID_PARAMETER,FALSE);return EPoint(dc,e::PointState::Current,false,0,0,p);}
extern "C" BOOL WINAPI UiGetWorldTransform(HDC dc,XFORM* f){Guard lock;if(!f)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::World,dc);if(!ESend(c))return FALSE;Copy(f,c.graphics.pixels,sizeof(*f));return BOOL(c.graphics.value);}
extern "C" BOOL WINAPI UiSetWorldTransform(HDC dc,const XFORM* f){Guard lock;if(!f)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::World,dc);c.draw.flags=1;Copy(c.draw.args,f,sizeof(*f));return ESend(c)?BOOL(c.graphics.value):FALSE;}
extern "C" BOOL WINAPI UiModifyWorldTransform(HDC dc,const XFORM* f,DWORD mode){Guard lock;if(!f&&mode!=MWT_IDENTITY)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::World,dc);c.draw.flags=2;c.draw.mode=mode;if(f&&mode!=MWT_IDENTITY)Copy(c.draw.args,f,sizeof(*f));return ESend(c)?BOOL(c.graphics.value):FALSE;}
extern "C" BOOL WINAPI UiLPtoDP(HDC dc,POINT* p,int n){Guard lock;if(!p||n<1||n>32)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::MapPoints,dc);c.draw.count=UINT(n);Copy(c.draw.pixels,p,size_t(n)*sizeof(POINT));if(!ESend(c))return FALSE;if(c.graphics.value)Copy(p,c.graphics.pixels,size_t(n)*sizeof(POINT));return BOOL(c.graphics.value);}
extern "C" BOOL WINAPI UiDPtoLP(HDC dc,POINT* p,int n){Guard lock;if(!p||n<1||n>32)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::MapPoints,dc);c.draw.flags=1;c.draw.count=UINT(n);Copy(c.draw.pixels,p,size_t(n)*sizeof(POINT));if(!ESend(c))return FALSE;if(c.graphics.value)Copy(p,c.graphics.pixels,size_t(n)*sizeof(POINT));return BOOL(c.graphics.value);}
extern "C" HRGN WINAPI UiCreateEllipticRgn(int l,int t,int r,int b){Guard lock;auto c=ECall(e::Op::EllipticRegion);c.draw.rect={l,t,r,b};return ESend(c)?reinterpret_cast<HRGN>(c.graphics.value):nullptr;}
extern "C" HRGN WINAPI UiCreateRoundRectRgn(int l,int t,int r,int b,int w,int h){Guard lock;auto c=ECall(e::Op::RoundRegion);c.draw.rect={l,t,r,b};c.draw.args[0]=w;c.draw.args[1]=h;return ESend(c)?reinterpret_cast<HRGN>(c.graphics.value):nullptr;}
extern "C" int WINAPI UiCombineRgn(HRGN d,HRGN a,HRGN b,int mode){Guard lock;auto c=ECall(e::Op::Combine,reinterpret_cast<HDC>(d));c.draw.object=uint64_t(a);c.draw.other=uint64_t(b);c.draw.flags=UINT(mode);return ESend(c)?int(c.graphics.value):ERROR;}
extern "C" int WINAPI UiOffsetRgn(HRGN r,int x,int y){Guard lock;auto c=ECall(e::Op::OffsetRegion,reinterpret_cast<HDC>(r));c.draw.args[0]=x;c.draw.args[1]=y;return ESend(c)?int(c.graphics.value):ERROR;}
extern "C" int WINAPI UiGetRgnBox(HRGN r,RECT* box){Guard lock;if(!box)return Fail(ERROR_INVALID_PARAMETER,ERROR);auto c=ECall(e::Op::RegionBox,reinterpret_cast<HDC>(r));if(!ESend(c))return ERROR;Copy(box,&c.graphics.rect,sizeof(*box));return int(c.graphics.value);}
extern "C" BOOL WINAPI UiPtInRegion(HRGN r,int x,int y){Guard lock;auto c=ECall(e::Op::PointRegion,reinterpret_cast<HDC>(r));c.draw.args[0]=x;c.draw.args[1]=y;return ESend(c)?BOOL(c.graphics.value):FALSE;}
extern "C" BOOL WINAPI UiRectInRegion(HRGN r,const RECT* box){Guard lock;if(!box)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::RectRegion,reinterpret_cast<HDC>(r));Copy(&c.draw.rect,box,sizeof(*box));return ESend(c)?BOOL(c.graphics.value):FALSE;}
extern "C" BOOL WINAPI UiSetRectRgn(HRGN r,int l,int t,int right,int b){Guard lock;auto c=ECall(e::Op::SetRegion,reinterpret_cast<HDC>(r));c.draw.rect={l,t,right,b};return ESend(c)?BOOL(c.graphics.value):FALSE;}
extern "C" int WINAPI UiExtSelectClipRgn(HDC dc,HRGN r,int mode){Guard lock;auto c=ECall(e::Op::ClipEx,dc);c.draw.object=uint64_t(r);c.draw.flags=UINT(mode);return ESend(c)?int(c.graphics.value):ERROR;}
extern "C" int WINAPI UiExcludeClipRect(HDC dc,int l,int t,int r,int b){Guard lock;auto c=ECall(e::Op::Exclude,dc);c.draw.rect={l,t,r,b};return ESend(c)?int(c.graphics.value):ERROR;}
extern "C" int WINAPI UiOffsetClipRgn(HDC dc,int x,int y){Guard lock;auto c=ECall(e::Op::OffsetClip,dc);c.draw.args[0]=x;c.draw.args[1]=y;return ESend(c)?int(c.graphics.value):ERROR;}
extern "C" int WINAPI UiGetClipBox(HDC dc,RECT* box){Guard lock;if(!box)return Fail(ERROR_INVALID_PARAMETER,ERROR);auto c=ECall(e::Op::ClipBox,dc);if(!ESend(c))return ERROR;Copy(box,&c.graphics.rect,sizeof(*box));return int(c.graphics.value);}
extern "C" BOOL WINAPI UiBeginPath(HDC dc){Guard lock;return EPath(dc,1);}
extern "C" BOOL WINAPI UiEndPath(HDC dc){Guard lock;return EPath(dc,2);}
extern "C" BOOL WINAPI UiAbortPath(HDC dc){Guard lock;return EPath(dc,3);}
extern "C" BOOL WINAPI UiCloseFigure(HDC dc){Guard lock;return EPath(dc,4);}
extern "C" BOOL WINAPI UiFillPath(HDC dc){Guard lock;return EPath(dc,5);}
extern "C" BOOL WINAPI UiStrokePath(HDC dc){Guard lock;return EPath(dc,6);}
extern "C" BOOL WINAPI UiStrokeAndFillPath(HDC dc){Guard lock;return EPath(dc,7);}
extern "C" BOOL WINAPI UiFlattenPath(HDC dc){Guard lock;return EPath(dc,8);}
extern "C" BOOL WINAPI UiWidenPath(HDC dc){Guard lock;return EPath(dc,9);}
extern "C" HRGN WINAPI UiPathToRegion(HDC dc){Guard lock;auto c=ECall(e::Op::Path,dc);c.draw.flags=10;return ESend(c)?reinterpret_cast<HRGN>(c.graphics.value):nullptr;}
extern "C" BOOL WINAPI UiSelectClipPath(HDC dc,int mode){Guard lock;return EPath(dc,11,mode);}
extern "C" int WINAPI UiGetPath(HDC dc,POINT* points,BYTE* kinds,int size){Guard lock;auto c=ECall(e::Op::GetPath,dc);if(!ESend(c))return -1;const auto count=int(c.graphics.value);if(!size)return count;if(size<count||size<0||!points||!kinds)return Fail(ERROR_INVALID_PARAMETER,-1);Copy(points,c.graphics.pixels,size_t(count)*sizeof(POINT));for(int i=0;i<count;++i)kinds[i]=BYTE(c.graphics.pixels[2*e::MaxPoints+i]);return count;}
extern "C" BOOL WINAPI UiBitBlt(HDC dc,int x,int y,int w,int h,HDC src,int sx,int sy,DWORD rop){Guard lock;return ERaster(dc,x,y,w,h,src,sx,sy,w,h,rop,0);}
extern "C" BOOL WINAPI UiStretchBlt(HDC dc,int x,int y,int w,int h,HDC src,int sx,int sy,int sw,int sh,DWORD rop){Guard lock;return ERaster(dc,x,y,w,h,src,sx,sy,sw,sh,rop,1);}
extern "C" BOOL WINAPI UiPatBlt(HDC dc,int x,int y,int w,int h,DWORD rop){Guard lock;return ERaster(dc,x,y,w,h,nullptr,0,0,0,0,rop,2);}
extern "C" COLORREF WINAPI UiSetPixel(HDC dc,int x,int y,COLORREF color){Guard lock;auto c=ECall(e::Op::SetPixel,dc);c.draw.args[0]=x;c.draw.args[1]=y;c.draw.color=color;return ESend(c,true)?COLORREF(c.graphics.value):CLR_INVALID;}
extern "C" COLORREF WINAPI UiGetTextColor(HDC dc){Guard lock;auto c=ECall(e::Op::GetColor,dc);return ESend(c)?COLORREF(c.graphics.value):CLR_INVALID;}
extern "C" COLORREF WINAPI UiGetBkColor(HDC dc){Guard lock;auto c=ECall(e::Op::GetColor,dc);c.draw.flags=1;return ESend(c)?COLORREF(c.graphics.value):CLR_INVALID;}
extern "C" BOOL WINAPI UiSetTextJustification(HDC dc,int extra,int breaks){Guard lock;auto c=ECall(e::Op::Justify,dc);c.draw.args[0]=extra;c.draw.args[1]=breaks;return ESend(c)?BOOL(c.graphics.value):FALSE;}
extern "C" BOOL WINAPI UiExtTextOutW(HDC dc,int x,int y,UINT flags,const RECT* box,LPCWSTR text,UINT count,const INT* dx){Guard lock;auto c=ECall(e::Op::ExtText,dc);if(count>64||count&&!text)return Fail(ERROR_INVALID_PARAMETER,FALSE);c.draw.args[0]=x;c.draw.args[1]=y;c.draw.flags=flags;c.draw.count=count;if(text)Copy(c.draw.text,text,size_t(count)*2);if(box){c.draw.mode|=1;Copy(&c.draw.rect,box,sizeof(*box));}if(dx){c.draw.mode|=2;Copy(c.draw.pixels,dx,size_t(count)*((flags&ETO_PDY)?2:1)*sizeof(INT));}return ESend(c,true)?BOOL(c.graphics.value):FALSE;}
extern "C" BOOL WINAPI UiExtTextOutA(HDC dc,int x,int y,UINT flags,const RECT* box,LPCSTR text,UINT count,const INT* dx){if(flags&ETO_GLYPH_INDEX)return Fail(ERROR_NOT_SUPPORTED,FALSE);g::Command c;if(!EAnsi(text,int(count),c))return Fail(ERROR_INVALID_PARAMETER,FALSE);return UiExtTextOutW(dc,x,y,flags,box,reinterpret_cast<LPCWSTR>(c.text),c.count,dx);}
extern "C" BOOL WINAPI UiGetTextExtentExPointW(HDC dc,LPCWSTR text,int count,int max,int* fit,int* dx,SIZE* size){Guard lock;if(!size)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::ExtentEx,dc);if(!GdiText(text,count,c.draw))return Fail(ERROR_INVALID_PARAMETER,FALSE);c.draw.args[0]=max;if(!ESend(c))return FALSE;*size={c.graphics.x,c.graphics.y};if(fit)*fit=c.graphics.rect.left;if(dx)Copy(dx,c.graphics.pixels,size_t(c.draw.count)*sizeof(int));return BOOL(c.graphics.value);}
extern "C" BOOL WINAPI UiGetTextExtentExPointA(HDC dc,LPCSTR text,int count,int max,int* fit,int* dx,SIZE* size){g::Command c;if(!EAnsi(text,count,c))return Fail(ERROR_INVALID_PARAMETER,FALSE);return UiGetTextExtentExPointW(dc,reinterpret_cast<LPCWSTR>(c.text),count,max,fit,dx,size);}
extern "C" DWORD WINAPI UiGetGlyphIndicesW(HDC dc,LPCWSTR text,int count,WORD* glyphs,DWORD flags){Guard lock;if(!glyphs)return Fail(ERROR_INVALID_PARAMETER,DWORD(GDI_ERROR));auto c=ECall(e::Op::Glyphs,dc);if(!GdiText(text,count,c.draw))return Fail(ERROR_INVALID_PARAMETER,DWORD(GDI_ERROR));c.draw.flags=flags;if(!ESend(c))return GDI_ERROR;if(c.graphics.value!=GDI_ERROR)for(unsigned i=0;i<c.draw.count;++i)glyphs[i]=WORD(c.graphics.pixels[i]);return DWORD(c.graphics.value);}
extern "C" DWORD WINAPI UiGetGlyphIndicesA(HDC dc,LPCSTR text,int count,WORD* glyphs,DWORD flags){g::Command c;if(!EAnsi(text,count,c))return Fail(ERROR_INVALID_PARAMETER,DWORD(GDI_ERROR));return UiGetGlyphIndicesW(dc,reinterpret_cast<LPCWSTR>(c.text),count,glyphs,flags);}
extern "C" BOOL WINAPI UiGetCharWidth32W(HDC dc,UINT first,UINT last,int* widths){Guard lock;if(!widths)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::Widths,dc);c.draw.args[0]=int(first);c.draw.args[1]=int(last);if(!ESend(c))return FALSE;if(c.graphics.value)Copy(widths,c.graphics.pixels,size_t(last-first+1)*sizeof(int));return BOOL(c.graphics.value);}
extern "C" BOOL WINAPI UiGetCharABCWidthsW(HDC dc,UINT first,UINT last,ABC* widths){Guard lock;if(!widths)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::Widths,dc);c.draw.flags=1;c.draw.args[0]=int(first);c.draw.args[1]=int(last);if(!ESend(c))return FALSE;if(c.graphics.value)Copy(widths,c.graphics.pixels,size_t(last-first+1)*sizeof(ABC));return BOOL(c.graphics.value);}
extern "C" BOOL WINAPI UiGetCharWidth32A(HDC dc,UINT first,UINT last,int* widths){Guard lock;if(!widths||last<first||last>255||last-first>=32)return Fail(ERROR_INVALID_PARAMETER,FALSE);for(UINT i=first;i<=last;++i){const char ch=char(i);wchar_t wide{};if(MultiByteToWideChar(1252,0,&ch,1,&wide,1)!=1||!UiGetCharWidth32W(dc,wide,wide,widths+i-first))return FALSE;}return TRUE;}
extern "C" BOOL WINAPI UiGetCharABCWidthsA(HDC dc,UINT first,UINT last,ABC* widths){Guard lock;if(!widths||last<first||last>255||last-first>=32)return Fail(ERROR_INVALID_PARAMETER,FALSE);for(UINT i=first;i<=last;++i){const char ch=char(i);wchar_t wide{};if(MultiByteToWideChar(1252,0,&ch,1,&wide,1)!=1||!UiGetCharABCWidthsW(dc,wide,wide,widths+i-first))return FALSE;}return TRUE;}
extern "C" BOOL WINAPI UiGetTextExtentPoint32A(HDC dc,LPCSTR text,int count,SIZE* size){g::Command c;if(!EAnsi(text,count,c))return Fail(ERROR_INVALID_PARAMETER,FALSE);return UiGetTextExtentPoint32W(dc,reinterpret_cast<LPCWSTR>(c.text),count,size);}
extern "C" int WINAPI UiDrawTextA(HDC dc,LPCSTR text,int count,RECT* box,UINT flags){if(count==-1){count=0;if(text)while(count<128&&text[count])++count;}g::Command c;if(!EAnsi(text,count,c))return Fail(ERROR_INVALID_PARAMETER,0);return UiDrawTextW(dc,reinterpret_cast<LPCWSTR>(c.text),count,box,flags);}
extern "C" BOOL WINAPI UiGetTextMetricsA(HDC dc,TEXTMETRICA* out){Guard lock;if(!out)return Fail(ERROR_INVALID_PARAMETER,FALSE);auto c=ECall(e::Op::MetricsAnsi,dc);if(!ESend(c))return FALSE;if(c.graphics.value)Copy(out,c.graphics.pixels,sizeof(*out));return BOOL(c.graphics.value);}
extern "C" int WINAPI UiGetTextFaceA(HDC dc,int count,LPSTR out){Guard lock;wchar_t face[32]{};const auto length=UiGetTextFaceW(dc,32,face);if(!length)return 0;if(!count)return length+1;if(!out||count<1)return Fail(ERROR_INVALID_PARAMETER,0);const auto copied=length<count-1?length:count-1;const auto n=WideCharToMultiByte(1252,0,face,copied,out,count-1,nullptr,nullptr);out[n]=0;return n;}
extern "C" DWORD WINAPI UiGetObjectType(HGDIOBJ object){Guard lock;auto c=ECall(e::Op::Describe,reinterpret_cast<HDC>(object));return ESend(c)?DWORD(c.graphics.x):0;}
extern "C" int WINAPI UiGetObjectW(HGDIOBJ object,int bytes,void* out){Guard lock;auto c=ECall(e::Op::Describe,reinterpret_cast<HDC>(object));if(!ESend(c))return 0;const auto needed=int(c.graphics.value);if(!out)return needed;if(bytes<needed||bytes<0)return Fail(ERROR_INVALID_PARAMETER,0);if(c.graphics.x==OBJ_BITMAP){auto* b=Bitmap(uint64_t(object));if(b){auto* native=reinterpret_cast<BITMAP*>(c.graphics.pixels);native->bmBits=b->storage?b->storage:b->bits;native->bmBitsPixel=WORD(b->bitCount);native->bmWidthBytes=LONG(b->stride?b->stride:b->width*4);}}Copy(out,c.graphics.pixels,size_t(needed));return needed;}
extern "C" int WINAPI UiGetObjectA(HGDIOBJ object,int bytes,void* out){Guard lock;if(UiGetObjectType(object)!=OBJ_FONT)return UiGetObjectW(object,bytes,out);LOGFONTW wide{};if(!UiGetObjectW(object,sizeof(wide),&wide))return 0;LOGFONTA ansi{};Copy(&ansi,&wide,28);WideCharToMultiByte(1252,0,wide.lfFaceName,-1,ansi.lfFaceName,32,nullptr,nullptr);if(!out)return sizeof(ansi);if(bytes<int(sizeof(ansi)))return Fail(ERROR_INVALID_PARAMETER,0);Copy(out,&ansi,sizeof(ansi));return sizeof(ansi);}
extern "C" HBITMAP WINAPI UiCreateDIBSection(HDC dc,const BITMAPINFO* info,UINT usage,void** bits,HANDLE section,DWORD offset){Guard lock;
    if(bits)*bits=nullptr;
    if(!info||!bits||section||offset||usage!=DIB_RGB_COLORS||info->bmiHeader.biSize!=sizeof(BITMAPINFOHEADER)||info->bmiHeader.biPlanes!=1||(info->bmiHeader.biBitCount!=24&&info->bmiHeader.biBitCount!=32)||info->bmiHeader.biCompression!=BI_RGB||!info->bmiHeader.biHeight||info->bmiHeader.biHeight<-512||info->bmiHeader.biHeight>512||(dc&&!Dc(dc)))return Fail(ERROR_NOT_SUPPORTED,HBITMAP{});
    const int height=info->bmiHeader.biHeight<0?-info->bmiHeader.biHeight:info->bmiHeader.biHeight;void* canonical=nullptr;const auto bitmap=NewBitmap(info->bmiHeader.biWidth,height,&canonical);if(!bitmap)return nullptr;auto* b=Bitmap(uint64_t(bitmap));
    if(info->bmiHeader.biBitCount==32&&info->bmiHeader.biHeight<0){*bits=canonical;return bitmap;}
    b->bitCount=info->bmiHeader.biBitCount;b->stride=((b->width*b->bitCount+31)/32)*4;b->bottomUp=info->bmiHeader.biHeight>0;b->storage=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,SIZE_T(b->stride)*b->height);if(!b->storage){UiLegacyDeleteObject(bitmap);return Fail(ERROR_NOT_ENOUGH_MEMORY,HBITMAP{});}*bits=b->storage;return bitmap;
}
extern "C" BOOL WINAPI UiDeleteObject(HGDIOBJ object){Guard lock;auto* b=Bitmap(uint64_t(object));void* storage=b?b->storage:nullptr;if(!UiLegacyDeleteObject(object))return FALSE;if(storage)HeapFree(GetProcessHeap(),0,storage);return TRUE;}
extern "C" int WINAPI UiStretchDIBits(HDC dc,int x,int y,int width,int height,int sx,int sy,int sw,int sh,const void* bits,const BITMAPINFO* info,UINT usage,DWORD rop){Guard lock;
    if(!bits||!info||usage!=DIB_RGB_COLORS||rop!=SRCCOPY||info->bmiHeader.biSize!=sizeof(BITMAPINFOHEADER)||info->bmiHeader.biPlanes!=1||(info->bmiHeader.biBitCount!=24&&info->bmiHeader.biBitCount!=32)||info->bmiHeader.biCompression||!info->bmiHeader.biHeight||info->bmiHeader.biHeight<-512||info->bmiHeader.biHeight>512||!e::Coord(x)||!e::Coord(y)||width<=0||width>4096||height<=0||height>4096||sx<0||sy<0||sw<=0||sh<=0)return Fail(ERROR_NOT_SUPPORTED,0);
    const int bh=info->bmiHeader.biHeight<0?-info->bmiHeader.biHeight:info->bmiHeader.biHeight;if(sw>512||sh>512||sx>512-sw||sy>bh-sh)return Fail(ERROR_INVALID_PARAMETER,0);
    void* memory=nullptr;const auto bitmap=NewBitmap(info->bmiHeader.biWidth,bh,&memory);if(!bitmap)return 0;auto* b=Bitmap(uint64_t(bitmap));b->storage=const_cast<void*>(bits);b->stride=((b->width*info->bmiHeader.biBitCount+31)/32)*4;b->bitCount=info->bmiHeader.biBitCount;b->bottomUp=info->bmiHeader.biHeight>0;
    const bool ok=Upload(b);b->storage=nullptr;g::Call c;c.draw.op=g::Op::Stretch;c.draw.dc=uint64_t(dc);c.draw.object=uint64_t(bitmap);c.draw.rect={x,y,x+width,y+height};const int sourceY=info->bmiHeader.biHeight>0?bh-sy-sh:sy;c.draw.source={sx,sourceY,sx+sw,sourceY+sh};const bool sent=ok&&Draw(c);UiDeleteObject(bitmap);return sent?int(c.graphics.value):0;
}


namespace {
struct ECursor {DWORD tid=0;HCURSOR cursor=nullptr;int visible=0;};ECursor eCursors[32]{};
bool CursorId(uintptr_t id){return id==32512||id==32513||id==32514||id==32515||id==32646||id==32648||id==32649||id==32650;}
ECursor* CursorThread(){const auto tid=GetCurrentThreadId();for(auto& c:eCursors)if(c.tid==tid||!c.tid){if(!c.tid){c.tid=tid;c.cursor=reinterpret_cast<HCURSOR>((11ull<<56)|32512);c.visible=0;}return &c;}SetLastError(ERROR_NOT_ENOUGH_QUOTA);return nullptr;}
bool SendCursor(const ECursor& state){u::Call c;c.command.op=static_cast<u::Op>(nc::Op::Cursor);c.command.wp=state.cursor?(uintptr_t(state.cursor)&0xFFFF):0;c.command.lp=uint64_t(int64_t(state.visible));return Call(c);}
}
extern "C" HCURSOR WINAPI UiLoadCursorW(HINSTANCE instance,LPCWSTR name){Guard lock;const auto id=uintptr_t(name);if(instance||!CursorId(id))return Fail(ERROR_NOT_SUPPORTED,HCURSOR{});return reinterpret_cast<HCURSOR>((11ull<<56)|id);}
extern "C" HCURSOR WINAPI UiLoadCursorA(HINSTANCE instance,LPCSTR name){return UiLoadCursorW(instance,reinterpret_cast<LPCWSTR>(name));}
extern "C" HCURSOR WINAPI UiSetCursor(HCURSOR cursor){Guard lock;if(cursor&&((uintptr_t(cursor)>>56)!=11||!CursorId(uintptr_t(cursor)&0xFFFFFFFFFFFFFFull)))return Fail(ERROR_INVALID_CURSOR_HANDLE,HCURSOR{});auto* state=CursorThread();if(!state)return nullptr;const auto old=state->cursor;state->cursor=cursor;if(!SendCursor(*state)){state->cursor=old;return nullptr;}return old;}
extern "C" int WINAPI UiShowCursor(BOOL show){Guard lock;auto* state=CursorThread();if(!state)return 0;const auto old=state->visible;state->visible+=show?1:-1;if(state->visible<-64||state->visible>64||!SendCursor(*state)){state->visible=old;return old;}return state->visible;}

