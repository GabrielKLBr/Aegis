uint64_t GdiEngine::PaintWindow(uint64_t id,uint32_t pid,uint32_t tid){auto* r=Find(id);return r&&r->pid==pid&&r->tid==tid&&r->kind==g::Kind::Dc&&r->paint?r->window:0;}
bool GdiEngine::PendingPaint(uint64_t id) const {for(const auto& s:surfaces)if(s.id==id)return s.pending;return false;}
namespace e=guardian::expandedgdi;
namespace {
bool TransformBounded(const XFORM& f){const float values[]{f.eM11,f.eM12,f.eM21,f.eM22,f.eDx,f.eDy};for(unsigned i=0;i<6;++i)if(!std::isfinite(values[i])||std::abs(values[i])>(i<4?8.f:4096.f))return false;return std::abs(f.eM11*f.eM22-f.eM12*f.eM21)>=1.f/1024.f;}

bool RegionBounded(HRGN region){RECT r{};return GetRgnBox(region,&r)!=ERROR&&r.left>=-8192&&r.top>=-8192&&r.right<=8192&&r.bottom<=8192&&GetRegionData(region,0,nullptr)<=e::MaxRegionBytes;}
}
uint32_t GdiEngine::ExecuteExpanded(const g::Command& c,g::Reply& reply,uint32_t pid,uint32_t tid){
    const auto op=static_cast<e::Op>(c.op);auto* r=Find(c.dc);auto* object=Find(c.object);
    auto own=[&](GdiResource* v){return v&&v->pid==pid&&v->tid==tid;};
    auto region=[&](GdiResource* v){return own(v)&&v->kind==g::Kind::Region;};
    auto finish=[&](uint32_t status=0){if(!status)++calls[0];return status;};
    auto newObject=[&](g::Kind kind,HGDIOBJ native)->uint32_t{if(!native)return ERROR_INVALID_PARAMETER;auto* slot=Allocate(kind,pid,tid);if(!slot){DeleteObject(native);return Quota;}slot->object=native;reply.value=slot->id;return finish();};
    if(op==e::Op::Hatch)return newObject(g::Kind::Brush,CreateHatchBrush(int(c.flags),c.color));
    if(op==e::Op::StyledPen)return newObject(g::Kind::Pen,CreatePen(int(c.flags),c.args[0],c.color));
    if(op==e::Op::EllipticRegion||op==e::Op::RoundRegion){const auto native=op==e::Op::EllipticRegion?CreateEllipticRgn(c.rect.left,c.rect.top,c.rect.right,c.rect.bottom):CreateRoundRectRgn(c.rect.left,c.rect.top,c.rect.right,c.rect.bottom,c.args[0],c.args[1]);if(native&&!RegionBounded(native)){DeleteObject(native);return Quota;}return newObject(g::Kind::Region,native);}
    if(!own(r))return Bad;
    if(op==e::Op::Describe){
        if(r->kind==g::Kind::Dc){reply.x=r->window?OBJ_DC:OBJ_MEMDC;return finish();}
        if(r->kind==g::Kind::Bitmap){BITMAP b{};if(!GetObjectW(r->object,sizeof(b),&b))return ERROR_GEN_FAILURE;b.bmBits=nullptr;std::memcpy(reply.pixels,&b,20);reply.value=sizeof(b);reply.x=OBJ_BITMAP;}
        else if(r->kind==g::Kind::Font){LOGFONTW f{};if(!GetObjectW(r->object,sizeof(f),&f))return ERROR_GEN_FAILURE;std::memcpy(reply.pixels,&f,sizeof(f));reply.value=sizeof(f);reply.x=OBJ_FONT;}
        else if(r->kind==g::Kind::Brush){LOGBRUSH b{};if(!GetObjectW(r->object,sizeof(b),&b)||b.lbStyle!=BS_SOLID&&b.lbStyle!=BS_HATCHED&&b.lbStyle!=BS_NULL)return ERROR_NOT_SUPPORTED;std::memcpy(reply.pixels,&b,sizeof(b));reply.value=sizeof(b);reply.x=OBJ_BRUSH;}
        else if(r->kind==g::Kind::Pen){LOGPEN p{};if(!GetObjectW(r->object,sizeof(p),&p))return ERROR_NOT_SUPPORTED;std::memcpy(reply.pixels,&p,sizeof(p));reply.value=sizeof(p);reply.x=OBJ_PEN;}
        else if(r->kind==g::Kind::Region)reply.x=OBJ_REGION;else return ERROR_NOT_SUPPORTED;return finish();
    }
    if(op==e::Op::Combine||op==e::Op::OffsetRegion||op==e::Op::RegionBox||op==e::Op::PointRegion||op==e::Op::RectRegion||op==e::Op::SetRegion){
        if(!region(r))return Bad;const auto native=static_cast<HRGN>(r->object);RECT bounds{};
        if(op==e::Op::RegionBox){reply.value=GetRgnBox(native,&bounds);reply.rect={bounds.left,bounds.top,bounds.right,bounds.bottom};}
        else if(op==e::Op::PointRegion)reply.value=PtInRegion(native,c.args[0],c.args[1]);
        else if(op==e::Op::RectRegion){bounds={c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};reply.value=RectInRegion(native,&bounds);}
        else {
            const auto tmp=CreateRectRgn(0,0,0,0);if(!tmp)return ERROR_NOT_ENOUGH_MEMORY;int value=ERROR;
            if(op==e::Op::Combine){auto* other=Find(c.other);if(!region(object)||c.flags!=RGN_COPY&&!region(other)){DeleteObject(tmp);return Bad;}value=CombineRgn(tmp,static_cast<HRGN>(object->object),c.flags==RGN_COPY?nullptr:static_cast<HRGN>(other->object),int(c.flags));}
            else if(op==e::Op::OffsetRegion){CombineRgn(tmp,native,nullptr,RGN_COPY);value=OffsetRgn(tmp,c.args[0],c.args[1]);}
            else {value=SetRectRgn(tmp,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom)?GetRgnBox(tmp,&bounds):ERROR;}
            if(value==ERROR||!RegionBounded(tmp)){DeleteObject(tmp);return value==ERROR?ERROR_INVALID_PARAMETER:Quota;}
            if(CombineRgn(native,tmp,nullptr,RGN_COPY)==ERROR){DeleteObject(tmp);return ERROR_GEN_FAILURE;}DeleteObject(tmp);reply.value=op==e::Op::SetRegion?TRUE:value;
        }return finish();
    }
    if(r->kind!=g::Kind::Dc)return Bad;
    if(op==e::Op::SetState||op==e::Op::GetState){const bool set=op==e::Op::SetState;const int value=c.args[0];
        switch(static_cast<e::State>(c.flags)){
        case e::State::Rop2:if(set&&(value<1||value>16))return ERROR_INVALID_PARAMETER;reply.value=set?SetROP2(r->dc,value):GetROP2(r->dc);break;
        case e::State::PolyFill:if(set&&(value<1||value>2))return ERROR_INVALID_PARAMETER;reply.value=set?SetPolyFillMode(r->dc,value):GetPolyFillMode(r->dc);break;
        case e::State::StretchMode:if(set&&(value<1||value>4))return ERROR_INVALID_PARAMETER;reply.value=set?SetStretchBltMode(r->dc,value):GetStretchBltMode(r->dc);break;
        case e::State::Graphics:if(set&&(value<1||value>2))return ERROR_INVALID_PARAMETER;reply.value=set?SetGraphicsMode(r->dc,value):GetGraphicsMode(r->dc);break;
        case e::State::MapMode:if(set&&(value<1||value>8))return ERROR_INVALID_PARAMETER;reply.value=set?SetMapMode(r->dc,value):GetMapMode(r->dc);break;
        case e::State::TextAlign:reply.value=set?SetTextAlign(r->dc,UINT(value)):GetTextAlign(r->dc);break;
        case e::State::CharExtra:reply.value=uint32_t(set?SetTextCharacterExtra(r->dc,value):GetTextCharacterExtra(r->dc));break;
        case e::State::ArcDirection:if(set&&(value<1||value>2))return ERROR_INVALID_PARAMETER;reply.value=set?SetArcDirection(r->dc,value):GetArcDirection(r->dc);break;
        case e::State::DcPen:reply.value=set?SetDCPenColor(r->dc,COLORREF(value)):GetDCPenColor(r->dc);break;
        case e::State::DcBrush:reply.value=set?SetDCBrushColor(r->dc,COLORREF(value)):GetDCBrushColor(r->dc);break;
        default:return ERROR_INVALID_PARAMETER;}return finish();
    }
    if(op==e::Op::MetricsAnsi){TEXTMETRICA metrics{};reply.value=GetTextMetricsA(r->dc,&metrics);if(reply.value)std::memcpy(reply.pixels,&metrics,53);return finish();}
    if(op==e::Op::GetColor){reply.value=c.flags?GetBkColor(r->dc):GetTextColor(r->dc);return finish();}
    if(op==e::Op::Justify){reply.value=SetTextJustification(r->dc,c.args[0],c.args[1]);return finish();}
    if(op==e::Op::SetPoint||op==e::Op::GetPoint){const bool set=op==e::Op::SetPoint;POINT p{};SIZE s{};BOOL ok=FALSE;
        switch(static_cast<e::PointState>(c.flags)){
        case e::PointState::WindowOrg:ok=set?SetWindowOrgEx(r->dc,c.args[0],c.args[1],&p):GetWindowOrgEx(r->dc,&p);break;
        case e::PointState::ViewportOrg:ok=set?SetViewportOrgEx(r->dc,c.args[0],c.args[1],&p):GetViewportOrgEx(r->dc,&p);break;
        case e::PointState::WindowExt:case e::PointState::ViewportExt:
            if(c.flags==uint32_t(e::PointState::WindowExt))ok=set?SetWindowExtEx(r->dc,c.args[0],c.args[1],&s):GetWindowExtEx(r->dc,&s);
            else ok=set?SetViewportExtEx(r->dc,c.args[0],c.args[1],&s):GetViewportExtEx(r->dc,&s);
            p={s.cx,s.cy};break;
        case e::PointState::BrushOrg:ok=set?SetBrushOrgEx(r->dc,c.args[0],c.args[1],&p):GetBrushOrgEx(r->dc,&p);break;
        case e::PointState::Current:ok=GetCurrentPositionEx(r->dc,&p);break;
        default:return ERROR_INVALID_PARAMETER;}reply.value=ok;reply.x=p.x;reply.y=p.y;return finish();
    }
    if(op==e::Op::World){XFORM f{},old{};if(!GetWorldTransform(r->dc,&old))return ERROR_GEN_FAILURE;
        if(!c.flags){std::memcpy(reply.pixels,&old,sizeof(old));reply.value=TRUE;return finish();}
        std::memcpy(&f,c.args,sizeof(f));if(!(c.flags==2&&c.mode==MWT_IDENTITY)&&!TransformBounded(f))return ERROR_INVALID_PARAMETER;
        const auto ok=c.flags==1?SetWorldTransform(r->dc,&f):ModifyWorldTransform(r->dc,c.mode==MWT_IDENTITY?nullptr:&f,c.mode);
        if(ok){XFORM combined{};if(!GetWorldTransform(r->dc,&combined)||!TransformBounded(combined)){SetWorldTransform(r->dc,&old);return ERROR_INVALID_PARAMETER;}}
        reply.value=ok;return finish();
    }
    if(op==e::Op::MapPoints){POINT points[e::MaxPoints]{};std::memcpy(points,c.pixels,c.count*sizeof(POINT));reply.value=c.flags?DPtoLP(r->dc,points,int(c.count)):LPtoDP(r->dc,points,int(c.count));if(reply.value)std::memcpy(reply.pixels,points,c.count*sizeof(POINT));return finish();}
    if(op==e::Op::ClipEx){if(c.object&&!region(object))return Bad;reply.value=ExtSelectClipRgn(r->dc,c.object?static_cast<HRGN>(object->object):nullptr,int(c.flags));return finish();}
    if(op==e::Op::Exclude){reply.value=ExcludeClipRect(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom);return finish();}
    if(op==e::Op::OffsetClip){reply.value=OffsetClipRgn(r->dc,c.args[0],c.args[1]);return finish();}
    if(op==e::Op::ClipBox){RECT box{};reply.value=GetClipBox(r->dc,&box);reply.rect={box.left,box.top,box.right,box.bottom};return finish();}
    if(op==e::Op::Path){
        switch(c.flags){
        case 1:reply.value=BeginPath(r->dc);if(reply.value){r->pathOpen=true;r->pathPoints=0;}break;
        case 2:reply.value=EndPath(r->dc);if(reply.value){r->pathOpen=false;const auto count=GetPath(r->dc,nullptr,nullptr,0);if(count>int(e::MaxPathPoints)){AbortPath(r->dc);r->pathPoints=0;return Quota;}}break;
        case 3:reply.value=AbortPath(r->dc);r->pathOpen=false;r->pathPoints=0;break;
        case 4:reply.value=CloseFigure(r->dc);break;
        case 5:reply.value=FillPath(r->dc);r->pathPoints=0;break;
        case 6:reply.value=StrokePath(r->dc);r->pathPoints=0;break;
        case 7:reply.value=StrokeAndFillPath(r->dc);r->pathPoints=0;break;
        case 8:case 9:reply.value=c.flags==8?FlattenPath(r->dc):WidenPath(r->dc);if(reply.value&&GetPath(r->dc,nullptr,nullptr,0)>int(e::MaxPathPoints)){AbortPath(r->dc);r->pathOpen=false;r->pathPoints=0;return Quota;}break;
        case 10:{const auto native=PathToRegion(r->dc);r->pathPoints=0;if(native&&!RegionBounded(native)){DeleteObject(native);return Quota;}return newObject(g::Kind::Region,native);}
        case 11:reply.value=SelectClipPath(r->dc,int(c.mode));r->pathPoints=0;break;
        default:return ERROR_INVALID_PARAMETER;}return finish();
    }
    if(op==e::Op::GetPath){const auto count=GetPath(r->dc,nullptr,nullptr,0);if(count<0)return ERROR_INVALID_PARAMETER;if(count>int(e::MaxPoints))return Quota;POINT points[e::MaxPoints]{};BYTE kinds[e::MaxPoints]{};if(count&&GetPath(r->dc,points,kinds,count)!=count)return ERROR_GEN_FAILURE;std::memcpy(reply.pixels,points,size_t(count)*sizeof(POINT));for(int i=0;i<count;++i)reply.pixels[2*e::MaxPoints+i]=kinds[i];reply.value=count;return finish();}
    if(op==e::Op::Raster){auto* from=Find(c.other);if(c.flags<2&&(!own(from)||from->kind!=g::Kind::Dc))return Bad;const auto w=c.rect.right-c.rect.left,h=c.rect.bottom-c.rect.top,sw=c.source.right-c.source.left,sh=c.source.bottom-c.source.top;
        if(c.flags==0){if(w!=sw||h!=sh)return ERROR_INVALID_PARAMETER;reply.value=BitBlt(r->dc,c.rect.left,c.rect.top,w,h,from->dc,c.source.left,c.source.top,c.color);}
        else if(c.flags==1)reply.value=StretchBlt(r->dc,c.rect.left,c.rect.top,w,h,from->dc,c.source.left,c.source.top,sw,sh,c.color);
        else reply.value=PatBlt(r->dc,c.rect.left,c.rect.top,w,h,c.color);return finish();}
    if(op==e::Op::SetPixel){reply.value=SetPixel(r->dc,c.args[0],c.args[1],c.color);return finish();}
    if(op==e::Op::ExtText){if(r->pathOpen)return ERROR_NOT_SUPPORTED;RECT box{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};reply.value=ExtTextOutW(r->dc,c.args[0],c.args[1],c.flags,(c.mode&1)?&box:nullptr,reinterpret_cast<LPCWSTR>(c.text),c.count,(c.mode&2)?reinterpret_cast<const INT*>(c.pixels):nullptr);return finish();}
    if(op==e::Op::ExtentEx){int fit=0;SIZE size{};int dx[128]{};reply.value=GetTextExtentExPointW(r->dc,reinterpret_cast<LPCWSTR>(c.text),int(c.count),c.args[0],&fit,dx,&size);reply.x=size.cx;reply.y=size.cy;reply.rect.left=fit;std::memcpy(reply.pixels,dx,c.count*sizeof(int));return finish();}
    if(op==e::Op::Glyphs){WORD indices[128]{};reply.value=GetGlyphIndicesW(r->dc,reinterpret_cast<LPCWSTR>(c.text),int(c.count),indices,c.flags);if(reply.value!=GDI_ERROR)for(unsigned i=0;i<c.count;++i)reply.pixels[i]=indices[i];return finish();}
    if(op==e::Op::Widths){if(c.flags){ABC abc[32]{};reply.value=GetCharABCWidthsW(r->dc,UINT(c.args[0]),UINT(c.args[1]),abc);if(reply.value)std::memcpy(reply.pixels,abc,size_t(c.args[1]-c.args[0]+1)*sizeof(ABC));}else{int widths[32]{};reply.value=GetCharWidth32W(r->dc,UINT(c.args[0]),UINT(c.args[1]),widths);if(reply.value)std::memcpy(reply.pixels,widths,size_t(c.args[1]-c.args[0]+1)*sizeof(int));}return finish();}
    const uint32_t cost=(op==e::Op::Polyline||op==e::Op::Polygon||op==e::Op::Bezier||op==e::Op::PolylineTo||op==e::Op::BezierTo||op==e::Op::PolyPolygon)?c.count:20;
    if(r->pathOpen){if(r->pathPoints+cost>e::MaxPathPoints)return Quota;r->pathPoints+=cost;}
    POINT points[e::MaxPoints]{};std::memcpy(points,c.pixels,c.count*sizeof(POINT));
    switch(op){
    case e::Op::Ellipse:reply.value=Ellipse(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom);break;
    case e::Op::RoundRect:reply.value=RoundRect(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom,c.args[0],c.args[1]);break;
    case e::Op::Arc:reply.value=Arc(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom,c.args[0],c.args[1],c.args[2],c.args[3]);break;
    case e::Op::Chord:reply.value=Chord(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom,c.args[0],c.args[1],c.args[2],c.args[3]);break;
    case e::Op::Pie:reply.value=Pie(r->dc,c.rect.left,c.rect.top,c.rect.right,c.rect.bottom,c.args[0],c.args[1],c.args[2],c.args[3]);break;
    case e::Op::Polyline:reply.value=Polyline(r->dc,points,int(c.count));break;
    case e::Op::Polygon:reply.value=Polygon(r->dc,points,int(c.count));break;
    case e::Op::Bezier:reply.value=PolyBezier(r->dc,points,c.count);break;
    case e::Op::PolylineTo:reply.value=PolylineTo(r->dc,points,c.count);break;
    case e::Op::BezierTo:reply.value=PolyBezierTo(r->dc,points,c.count);break;
    case e::Op::PolyPolygon:reply.value=PolyPolygon(r->dc,points,c.args,int(c.flags));break;
    default:return ERROR_NOT_SUPPORTED;
    }return finish();
}

