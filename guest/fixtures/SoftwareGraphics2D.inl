bool TargetProperties(const D2D1_RENDER_TARGET_PROPERTIES* p){
    return p&&(p->type==D2D1_RENDER_TARGET_TYPE_DEFAULT||p->type==D2D1_RENDER_TARGET_TYPE_SOFTWARE)&&
        p->usage==D2D1_RENDER_TARGET_USAGE_NONE&&(p->minLevel==D2D1_FEATURE_LEVEL_DEFAULT||p->minLevel==D2D1_FEATURE_LEVEL_9)&&
        (p->pixelFormat.format==DXGI_FORMAT_UNKNOWN||p->pixelFormat.format==DXGI_FORMAT_B8G8R8A8_UNORM)&&
        (p->pixelFormat.alphaMode==D2D1_ALPHA_MODE_UNKNOWN||p->pixelFormat.alphaMode==D2D1_ALPHA_MODE_IGNORE)&&
        (p->dpiX==0||p->dpiX==96)&&(p->dpiY==0||p->dpiY==96);
}
class TextFormat final:public Stub_IDWriteTextFormat {
public:
    wchar_t face[32]{},locale[32]{};FLOAT size=12;DWRITE_FONT_WEIGHT weight=DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style=DWRITE_FONT_STYLE_NORMAL;DWRITE_FONT_STRETCH stretch=DWRITE_FONT_STRETCH_NORMAL;
    DWRITE_TEXT_ALIGNMENT alignment=DWRITE_TEXT_ALIGNMENT_LEADING;DWRITE_PARAGRAPH_ALIGNMENT paragraph=DWRITE_PARAGRAPH_ALIGNMENT_NEAR;DWRITE_WORD_WRAPPING wrap=DWRITE_WORD_WRAPPING_WRAP;
    HRESULT State(uint32_t kind,uint32_t value){if(FAILED(Check()))return Check();auto call=Command(f::Op::FormatState,id);call.draw.flags=kind;call.draw.mode=value;return Send(call);}
    HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT value) override {const auto hr=State(0,value);if(SUCCEEDED(hr))alignment=value;return hr;}
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT value) override {const auto hr=State(1,value);if(SUCCEEDED(hr))paragraph=value;return hr;}
    HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING value) override {if(value>DWRITE_WORD_WRAPPING_NO_WRAP)return E_NOTIMPL;const auto hr=State(2,value);if(SUCCEEDED(hr))wrap=value;return hr;}
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() override {return alignment;}
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() override {return paragraph;}
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() override {return wrap;}
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() override {return weight;}
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() override {return style;}
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() override {return stretch;}
    FLOAT STDMETHODCALLTYPE GetFontSize() override {return size;}
    UINT32 STDMETHODCALLTYPE GetFontFamilyNameLength() override {UINT32 n=0;while(n<31&&face[n])++n;return n;}
    HRESULT STDMETHODCALLTYPE GetFontFamilyName(WCHAR* out,UINT32 count) override {if(!out)return E_POINTER;if(count<=GetFontFamilyNameLength())return E_NOT_SUFFICIENT_BUFFER;Copy(out,face,(GetFontFamilyNameLength()+1)*2);return S_OK;}
    UINT32 STDMETHODCALLTYPE GetLocaleNameLength() override {UINT32 n=0;while(n<31&&locale[n])++n;return n;}
    HRESULT STDMETHODCALLTYPE GetLocaleName(WCHAR* out,UINT32 count) override {if(!out)return E_POINTER;if(count<=GetLocaleNameLength())return E_NOT_SUFFICIENT_BUFFER;Copy(out,locale,(GetLocaleNameLength()+1)*2);return S_OK;}
};
class WriteFactory final:public Stub_IDWriteFactory {
public:
    HRESULT STDMETHODCALLTYPE CreateTextFormat(const WCHAR* fontName,IDWriteFontCollection* collection,DWRITE_FONT_WEIGHT weight,DWRITE_FONT_STYLE style,DWRITE_FONT_STRETCH stretch,FLOAT size,const WCHAR* locale,IDWriteTextFormat** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(collection||!fontName||!locale)return E_INVALIDARG;
        auto call=Command(f::Op::Format);uint16_t face[128]{};uint32_t length=0;
        if(!Text(fontName,face,length,31)||!length||!Text(locale,call.draw.text,call.draw.count,31))return E_INVALIDARG;
        Copy(call.draw.font.face,face,length*2);call.draw.flags=weight;call.draw.color=style;call.draw.mode=stretch;call.draw.args[0]=Word(size);
        auto* object=Make<TextFormat>();if(!object)return E_OUTOFMEMORY;const auto hr=New(f::Op::Format,*object,call);
        if(FAILED(hr)){object->Release();return hr;}Copy(object->face,face,(length+1)*2);Copy(object->locale,call.draw.text,(call.draw.count+1)*2);
        object->weight=weight;object->style=style;object->stretch=stretch;object->size=size;*out=object;return S_OK;
    }
};
class SolidBrush final:public Stub_ID2D1SolidColorBrush {
public:
    Keep target,factory;D2D1_COLOR_F color{};FLOAT opacity=1;
    void Update(){auto call=Command(f::Op::Brush,id);call.draw.args[0]=Word(color.r);call.draw.args[1]=Word(color.g);call.draw.args[2]=Word(color.b);call.draw.args[3]=Word(color.a*opacity);const auto hr=Send(call);if(FAILED(hr))error=hr;}
    void STDMETHODCALLTYPE GetFactory(ID2D1Factory** out) const override {if(out){*out=static_cast<ID2D1Factory*>(factory.value);if(*out)(*out)->AddRef();}}
    void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F* value) override {if(!value){error=E_POINTER;return;}color=*value;Update();}
    D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const override {return color;}
    void STDMETHODCALLTYPE SetOpacity(FLOAT value) override {if(!f::Float(Word(value),0,1)){error=E_INVALIDARG;return;}opacity=value;Update();}
    FLOAT STDMETHODCALLTYPE GetOpacity() const override {return opacity;}
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* out) const override {if(out)*out=D2D1::Matrix3x2F::Identity();}
};
class D2DBitmap final:public Stub_ID2D1Bitmap {
public:
    Keep target,factory;UINT32 width=0,height=0;
    void STDMETHODCALLTYPE GetFactory(ID2D1Factory** out) const override {if(out){*out=static_cast<ID2D1Factory*>(factory.value);if(*out)(*out)->AddRef();}}
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override {return {width,height};}
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {return {FLOAT(width),FLOAT(height)};}
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override {return {DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED};}
    void STDMETHODCALLTYPE GetDpi(FLOAT* x,FLOAT* y) const override {if(x)*x=96;if(y)*y=96;}
};
template<class Base> class Target:public Base {
public:
    using Base::id;using Base::error;using Base::Check;using Base::Unsupported;
    Keep factory;HWND window=nullptr;UINT32 width=0,height=0;D2D1_MATRIX_3X2_F transform=D2D1::Matrix3x2F::Identity();bool begun=false;
    HRESULT Call(g::Call& call){if(FAILED(Check()))return Check();const auto hr=Send(call);if(FAILED(hr)&&SUCCEEDED(error))error=hr;return hr;}
    void STDMETHODCALLTYPE GetFactory(ID2D1Factory** out) const override {if(out){*out=static_cast<ID2D1Factory*>(factory.value);if(*out)(*out)->AddRef();}}
    HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F* color,const D2D1_BRUSH_PROPERTIES* props,ID2D1SolidColorBrush** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(!color)return E_POINTER;if(props)return E_NOTIMPL;
        auto call=Command(f::Op::Brush,id);call.draw.args[0]=Word(color->r);call.draw.args[1]=Word(color->g);call.draw.args[2]=Word(color->b);call.draw.args[3]=Word(color->a);
        auto* brush=Make<SolidBrush>();if(!brush)return E_OUTOFMEMORY;const auto hr=Call(call);if(FAILED(hr)){brush->Release();return hr;}
        brush->id=call.graphics.value;brush->color=*color;brush->target.Set(this->Interface());brush->factory.Set(factory.value);*out=brush;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource* source,const D2D1_BITMAP_PROPERTIES* props,ID2D1Bitmap** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(props)return E_NOTIMPL;auto* image=Object(source);if(!image||FAILED(image->Check())||image->family!=11&&image->family!=12)return E_INVALIDARG;
        auto call=Command(f::Op::Bitmap,id);call.draw.object=image->id;auto* bitmap=Make<D2DBitmap>();if(!bitmap)return E_OUTOFMEMORY;
        const auto hr=Call(call);if(FAILED(hr)){bitmap->Release();return hr;}bitmap->id=call.graphics.value;bitmap->width=UINT32(call.graphics.rect.right);bitmap->height=UINT32(call.graphics.rect.bottom);
        bitmap->target.Set(this->Interface());bitmap->factory.Set(factory.value);*out=bitmap;return S_OK;
    }
    void STDMETHODCALLTYPE BeginDraw() override {if(this->poisoned)return;if(begun){error=D2DERR_WRONG_STATE;return;}error=S_OK;auto call=Command(f::Op::Begin,id);if(SUCCEEDED(Call(call))){width=UINT32(call.graphics.rect.right);height=UINT32(call.graphics.rect.bottom);begun=true;}}
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* a,D2D1_TAG* b) override {if(a)*a=0;if(b)*b=0;if(!begun)return FAILED(error)?error:D2DERR_WRONG_STATE;const auto prior=error;auto call=Command(f::Op::End,id);const auto hr=Call(call);begun=false;return FAILED(prior)?prior:hr;}
    void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F* color) override {const D2D1_COLOR_F zero{};if(!color)color=&zero;auto call=Command(f::Op::Clear,id);call.draw.args[0]=Word(color->r);call.draw.args[1]=Word(color->g);call.draw.args[2]=Word(color->b);call.draw.args[3]=Word(color->a);Call(call);}
    bool Brush(ID2D1Brush* brush,g::Command& command){auto* object=Object(brush);if(!object||object->family!=4||FAILED(object->Check())){error=E_INVALIDARG;return false;}command.object=object->id;return true;}
    void Shape(f::Op op,const D2D1_RECT_F* rect,ID2D1Brush* brush,FLOAT stroke,bool fill,ID2D1StrokeStyle* style=nullptr){
        if(!rect){error=E_POINTER;return;}if(style){Unsupported(32,true);return;}auto call=Command(op,id);if(!Brush(brush,call.draw))return;
        call.draw.flags=fill?1u:0u;call.draw.args[0]=Word(rect->left);call.draw.args[1]=Word(rect->top);call.draw.args[2]=Word(rect->right);call.draw.args[3]=Word(rect->bottom);call.draw.args[4]=Word(stroke);Call(call);
    }
    void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F* rect,ID2D1Brush* brush,FLOAT stroke,ID2D1StrokeStyle* style) override {Shape(f::Op::Rectangle,rect,brush,stroke,false,style);}
    void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F* rect,ID2D1Brush* brush) override {Shape(f::Op::Rectangle,rect,brush,0,true);}
    void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F a,D2D1_POINT_2F b,ID2D1Brush* brush,FLOAT stroke,ID2D1StrokeStyle* style) override {const D2D1_RECT_F rect{a.x,a.y,b.x,b.y};Shape(f::Op::Line,&rect,brush,stroke,false,style);}
    void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE* ellipse,ID2D1Brush* brush,FLOAT stroke,ID2D1StrokeStyle* style) override {if(!ellipse){error=E_POINTER;return;}const D2D1_RECT_F rect{ellipse->point.x,ellipse->point.y,ellipse->radiusX,ellipse->radiusY};Shape(f::Op::Ellipse,&rect,brush,stroke,false,style);}
    void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE* ellipse,ID2D1Brush* brush) override {if(!ellipse){error=E_POINTER;return;}const D2D1_RECT_F rect{ellipse->point.x,ellipse->point.y,ellipse->radiusX,ellipse->radiusY};Shape(f::Op::Ellipse,&rect,brush,0,true);}
    void STDMETHODCALLTYPE DrawText(const WCHAR* text,UINT32 length,IDWriteTextFormat* format,const D2D1_RECT_F* rect,ID2D1Brush* brush,D2D1_DRAW_TEXT_OPTIONS options,DWRITE_MEASURING_MODE mode) override {
        if(!text||!rect){error=E_POINTER;return;}auto* value=Object(format);if(!value||value->family!=7||FAILED(value->Check())||length>127||mode!=DWRITE_MEASURING_MODE_NATURAL){error=E_INVALIDARG;return;}
        auto call=Command(f::Op::Text,id);if(!Brush(brush,call.draw))return;call.draw.other=value->id;call.draw.count=length;Copy(call.draw.text,text,size_t(length)*2);
        call.draw.flags=options;call.draw.args[0]=Word(rect->left);call.draw.args[1]=Word(rect->top);call.draw.args[2]=Word(rect->right);call.draw.args[3]=Word(rect->bottom);Call(call);
    }
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap* bitmap,const D2D1_RECT_F* destination,FLOAT opacity,D2D1_BITMAP_INTERPOLATION_MODE mode,const D2D1_RECT_F* source) override {
        auto* value=Object(bitmap);if(!value||value->family!=5||FAILED(value->Check())||source){error=E_INVALIDARG;return;}auto* image=static_cast<D2DBitmap*>(value);
        const D2D1_RECT_F all{0,0,FLOAT(image->width),FLOAT(image->height)};if(!destination)destination=&all;
        auto call=Command(f::Op::DrawBitmap,id);call.draw.object=value->id;call.draw.flags=mode;call.draw.args[0]=Word(destination->left);call.draw.args[1]=Word(destination->top);call.draw.args[2]=Word(destination->right);call.draw.args[3]=Word(destination->bottom);call.draw.args[4]=Word(opacity);Call(call);
    }
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* value) override {if(!value){error=E_POINTER;return;}auto call=Command(f::Op::Transform,id);Copy(call.draw.args,value,24);if(SUCCEEDED(Call(call)))transform=*value;}
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* out) const override {if(out)*out=transform;}
    void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F* rect,D2D1_ANTIALIAS_MODE mode) override {if(!rect){error=E_POINTER;return;}auto call=Command(f::Op::Clip,id);call.draw.flags=mode;call.draw.args[0]=Word(rect->left);call.draw.args[1]=Word(rect->top);call.draw.args[2]=Word(rect->right);call.draw.args[3]=Word(rect->bottom);Call(call);}
    void STDMETHODCALLTYPE PopAxisAlignedClip() override {auto call=Command(f::Op::PopClip,id);Call(call);}
    void STDMETHODCALLTYPE SetDpi(FLOAT x,FLOAT y) override {if((x!=96||y!=96)&&(x!=0||y!=0))Unsupported(51,true);}
    void STDMETHODCALLTYPE GetDpi(FLOAT* x,FLOAT* y) const override {if(x)*x=96;if(y)*y=96;}
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {return {FLOAT(width),FLOAT(height)};}
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override {return {width,height};}
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override {return {DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE};}
    UINT32 STDMETHODCALLTYPE GetMaximumBitmapSize() const override {return f::MaxDimension;}
    BOOL STDMETHODCALLTYPE IsSupported(const D2D1_RENDER_TARGET_PROPERTIES* props) const override {return TargetProperties(props);}
};
class HwndTarget final:public Target<Stub_ID2D1HwndRenderTarget> {
public:
    D2D1_WINDOW_STATE STDMETHODCALLTYPE CheckWindowState() override {return D2D1_WINDOW_STATE_NONE;}
    HWND STDMETHODCALLTYPE GetHwnd() const override {return window;}
    HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U* size) override {if(!size)return E_POINTER;RECT rect{};if(!GetClientRect(window,&rect)||size->width!=UINT32(rect.right)||size->height!=UINT32(rect.bottom)||size->width>1280||size->height>720||!size->width||!size->height)return E_INVALIDARG;width=size->width;height=size->height;return S_OK;}
};
class DcTarget final:public Target<Stub_ID2D1DCRenderTarget> {
public:
    HRESULT STDMETHODCALLTYPE BindDC(const HDC dc,const RECT* rect) override {
        if(!dc||!rect||rect->left||rect->top||rect->right<1||rect->bottom<1||rect->right>1280||rect->bottom>720||begun)return E_INVALIDARG;
        auto call=id?Command(f::Op::Rebind,id):Command(f::Op::Target,uint64_t(dc));Copy(&call.draw.rect,rect,sizeof(*rect));if(id)call.draw.object=uint64_t(dc);else call.draw.flags=1;const auto hr=Call(call);if(FAILED(hr))return hr;
        id=call.graphics.value;width=UINT32(call.graphics.rect.right);height=UINT32(call.graphics.rect.bottom);
        if(width!=UINT32(rect->right)||height!=UINT32(rect->bottom)){auto release=Command(f::Op::Free,id);Send(release);id=0;return E_INVALIDARG;}
        return S_OK;
    }
};
class GraphicsFactory final:public Stub_ID2D1Factory {
public:
    void STDMETHODCALLTYPE GetDesktopDpi(FLOAT* x,FLOAT* y) override {if(x)*x=96;if(y)*y=96;}
    bool Properties(const D2D1_RENDER_TARGET_PROPERTIES* props){return TargetProperties(props);}
    HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* props,const D2D1_HWND_RENDER_TARGET_PROPERTIES* hwnd,ID2D1HwndRenderTarget** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(!Properties(props)||!hwnd||!hwnd->hwnd||hwnd->presentOptions>1)return D2DERR_UNSUPPORTED_OPERATION;
        auto* target=Make<HwndTarget>();if(!target)return E_OUTOFMEMORY;auto call=Command(f::Op::Target,uint64_t(hwnd->hwnd));const auto hr=New(f::Op::Target,*target,call);
        if(FAILED(hr)){target->Release();return hr;}if(hwnd->pixelSize.width&&hwnd->pixelSize.width!=UINT32(call.graphics.rect.right)||hwnd->pixelSize.height&&hwnd->pixelSize.height!=UINT32(call.graphics.rect.bottom)){target->Release();return E_INVALIDARG;}
        target->width=UINT32(call.graphics.rect.right);target->height=UINT32(call.graphics.rect.bottom);target->window=hwnd->hwnd;target->factory.Set(this);*out=target;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* props,ID2D1DCRenderTarget** out) override {if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(!Properties(props))return D2DERR_UNSUPPORTED_OPERATION;auto* target=Make<DcTarget>();if(!target)return E_OUTOFMEMORY;target->factory.Set(this);*out=target;return S_OK;}
};
