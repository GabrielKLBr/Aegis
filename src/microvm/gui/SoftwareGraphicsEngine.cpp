#include "SoftwareGraphicsEngine.h"
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>
namespace av::microvm::native_gui {
namespace f=guardian::softwaregraphics;namespace g=guardian::nativegdi;
using Microsoft::WRL::ComPtr;
namespace {
float Float(int32_t word){float result;std::memcpy(&result,&word,4);return result;}
D2D1_COLOR_F Color(const g::Command& c){return {Float(c.args[0]),Float(c.args[1]),Float(c.args[2]),Float(c.args[3])};}
bool Face(const uint16_t* name){const auto* text=reinterpret_cast<const wchar_t*>(name);for(auto face:{L"Arial",L"Consolas",L"Segoe UI",L"Verdana",L"Tahoma",L"Times New Roman",L"Courier New",L"Calibri",L"Cambria",L"Georgia"})if(!_wcsicmp(text,face))return true;return false;}
struct Resource {
    uint64_t id=0,parent=0,window=0,charge=0;uint32_t pid=0,tid=0,written=0,width=0,height=0,bpp=0,format=0,clips=0;
    f::Kind kind{};bool begun=false,signaled=false;HRESULT error=S_OK;double dpiX=96,dpiY=96;
    ComPtr<ID2D1DCRenderTarget> target;ComPtr<ID2D1SolidColorBrush> brush;ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<IDWriteTextFormat> text;ComPtr<IWICBitmapSource> image;GUID pixelFormat{};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;ComPtr<ID3D11Query> query;
    std::vector<uint8_t> data;
};
}
struct SoftwareGraphicsEngine::State {
    uint64_t session=0,next=1,bytes=0,calls=0,errors=0,presents=0,used=0;HRESULT com=S_OK;HMODULE codecs=nullptr;
    std::array<Resource,f::MaxObjects> resources{};
    ComPtr<ID2D1Factory> d2d;ComPtr<IDWriteFactory> write;ComPtr<IWICImagingFactory> wic;
    explicit State(uint64_t value):session(value){com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);}
    ~State(){Clear();wic.Reset();write.Reset();d2d.Reset();if(SUCCEEDED(com))CoUninitialize();if(codecs)FreeLibrary(codecs);}
    HRESULT Codec(REFCLSID cls,REFIID iid,void** out){
        if(!out)return E_POINTER;*out=nullptr;
        if(!codecs)codecs=LoadLibraryExW(L"windowscodecs.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!codecs)return HRESULT_FROM_WIN32(GetLastError());
        using Get=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);const auto get=reinterpret_cast<Get>(GetProcAddress(codecs,"DllGetClassObject"));
        if(!get)return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);ComPtr<IClassFactory> factory;
        const auto hr=get(cls,IID_PPV_ARGS(&factory));return FAILED(hr)?hr:factory->CreateInstance(nullptr,iid,out);
    }
    Resource* Find(uint64_t id){for(auto& r:resources)if(id&&r.id==id)return &r;return nullptr;}
    Resource* Add(f::Kind kind,uint32_t pid,uint32_t tid,uint64_t charge=0){
        if(charge>f::MaxBytes-bytes)return nullptr;
        for(auto& r:resources)if(!r.id){r.id=(0x50ull<<56)|((session&0xFFFFFF)<<32)|next++;r.kind=kind;r.pid=pid;r.tid=tid;r.charge=charge;bytes+=charge;return &r;}return nullptr;
    }
    void Drop(Resource& r){if(r.begun&&r.target){while(r.clips){r.target->PopAxisAlignedClip();--r.clips;}r.target->EndDraw();}bytes-=r.charge;r=Resource{};}
    void Clear(){for(auto& r:resources)if(r.id)Drop(r);}
    HRESULT Factories(){
        if(FAILED(com))return com;HRESULT hr=S_OK;
        if(!d2d)hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf());
        if(SUCCEEDED(hr)&&!write)hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf()));
        if(SUCCEEDED(hr)&&!wic)hr=Codec(CLSID_WICImagingFactory,IID_PPV_ARGS(wic.GetAddressOf()));
        return hr;
    }
    HRESULT Pixels(Resource& r){
        if(!r.data.empty())return S_OK;if(!r.image||!r.bpp)return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        const auto size=uint64_t(r.width)*r.height*r.bpp;if(size>r.charge)return E_OUTOFMEMORY;
        r.data.resize(size_t(size));const auto hr=r.image->CopyPixels(nullptr,r.width*r.bpp,UINT(size),r.data.data());
        if(FAILED(hr))r.data.clear();return hr;
    }
    HRESULT ReadTexture(Resource& r){
        if(!r.texture||!r.device||!r.context)return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC desc{};r.texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;auto hr=r.device->CreateTexture2D(&desc,nullptr,&staging);if(FAILED(hr))return hr;
        r.context->CopyResource(staging.Get(),r.texture.Get());D3D11_MAPPED_SUBRESOURCE map{};hr=r.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
        if(FAILED(hr))return hr;
        r.data.resize(size_t(r.width)*r.height*4);for(uint32_t y=0;y<r.height;++y)std::memcpy(r.data.data()+size_t(y)*r.width*4,static_cast<const uint8_t*>(map.pData)+size_t(y)*map.RowPitch,size_t(r.width)*4);
        r.context->Unmap(staging.Get(),0);return S_OK;
    }
};
SoftwareGraphicsEngine::SoftwareGraphicsEngine(uint64_t session):state_(std::make_unique<State>(session)){}
SoftwareGraphicsEngine::~SoftwareGraphicsEngine()=default;
uint64_t SoftwareGraphicsEngine::Bytes() const{return state_->bytes;}
void SoftwareGraphicsEngine::Final(g::Reply& reply) const {reply.selected[0]=state_->calls;reply.selected[1]=state_->errors;reply.selected[2]=state_->presents;reply.selected[3]=state_->used;}
uint32_t SoftwareGraphicsEngine::Live() const {uint32_t n=0;for(const auto& r:state_->resources)if(r.id)++n;return n;}
void SoftwareGraphicsEngine::Clear(){state_->Clear();}
void SoftwareGraphicsEngine::Exit(uint32_t pid,uint32_t tid){for(auto& r:state_->resources)if(r.id&&r.pid==pid&&r.tid==tid)state_->Drop(r);}
void SoftwareGraphicsEngine::DropWindow(uint64_t id){for(auto& r:state_->resources)if(r.id&&r.window==id){if(r.begun&&r.target){while(r.clips){r.target->PopAxisAlignedClip();--r.clips;}r.target->EndDraw();r.begun=false;}r.window=0;r.error=D2DERR_RECREATE_TARGET;}}
uint32_t SoftwareGraphicsEngine::Execute(const g::Command& c,g::Reply& reply,uint32_t pid,uint32_t tid,const Bind& bind,const Present& present){
    if(!f::Extended(c.op)||!f::Valid(c)||!pid||!tid)return ERROR_INVALID_PARAMETER;
    auto& s=*state_;++s.calls;auto* r=s.Find(c.dc);auto* object=s.Find(c.object);auto* other=s.Find(c.other);
    auto own=[&](Resource* p){return p&&p->pid==pid&&p->tid==tid;};
    auto done=[&](HRESULT hr){reply.x=hr;if(FAILED(hr))++s.errors;return uint32_t(0);};
    auto result=[&](Resource* p){if(!p)return done(E_OUTOFMEMORY);reply.value=p->id;reply.rect={0,0,int32_t(p->width),int32_t(p->height)};return done(S_OK);};
    const auto op=static_cast<f::Op>(c.op);
    if(op==f::Op::Stats){reply.value=s.calls;reply.selected[0]=Live();reply.selected[1]=s.errors;reply.selected[2]=s.presents;reply.selected[3]=s.used;return done(S_OK);}
    if(op==f::Op::Unsupported)return done(E_NOTIMPL);
    if(op==f::Op::Stream){auto* slot=s.Add(f::Kind::Stream,pid,tid,c.count);if(!slot)return done(E_OUTOFMEMORY);slot->data.resize(c.count);return result(slot);}
    if(op==f::Op::Device){
        for(const auto& item:s.resources)if(item.id&&item.kind==f::Kind::Device)return done(DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
        auto* slot=s.Add(f::Kind::Device,pid,tid);if(!slot)return done(E_OUTOFMEMORY);
        const D3D_FEATURE_LEVEL requested[]{D3D_FEATURE_LEVEL_9_1};D3D_FEATURE_LEVEL level{};
        const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,requested,1,D3D11_SDK_VERSION,&slot->device,&level,&slot->context);
        if(FAILED(hr)){s.Drop(*slot);return done(hr);}s.used|=8;reply.selected[0]=level;return result(slot);
    }
    if(op==f::Op::Target){
        const auto binding=bind(c.dc,c.flags!=0,pid,tid);if(!binding.dc||!binding.hwnd)return ERROR_INVALID_HANDLE;
        if(c.flags&&(c.rect.right!=binding.width||c.rect.bottom!=binding.height))return done(E_INVALIDARG);
        auto hr=s.Factories();if(FAILED(hr))return done(hr);auto* slot=s.Add(f::Kind::Target,pid,tid);if(!slot)return done(E_OUTOFMEMORY);
        D2D1_RENDER_TARGET_PROPERTIES props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        hr=s.d2d->CreateDCRenderTarget(&props,&slot->target);if(SUCCEEDED(hr)){RECT rect{0,0,binding.width,binding.height};hr=slot->target->BindDC(binding.dc,&rect);}
        if(FAILED(hr)){s.Drop(*slot);return done(hr);}s.used|=1;slot->window=binding.window;slot->width=uint32_t(binding.width);slot->height=uint32_t(binding.height);return result(slot);
    }
    if(op==f::Op::Format){
        if(!Face(c.font.face))return done(E_ACCESSDENIED);auto hr=s.Factories();if(FAILED(hr))return done(hr);
        auto* slot=s.Add(f::Kind::Format,pid,tid);if(!slot)return done(E_OUTOFMEMORY);
        hr=s.write->CreateTextFormat(reinterpret_cast<const wchar_t*>(c.font.face),nullptr,DWRITE_FONT_WEIGHT(c.flags),DWRITE_FONT_STYLE(c.color),DWRITE_FONT_STRETCH(c.mode),Float(c.args[0]),reinterpret_cast<const wchar_t*>(c.text),&slot->text);
        if(FAILED(hr)){s.Drop(*slot);return done(hr);}s.used|=2;return result(slot);
    }
    if(!own(r))return ERROR_INVALID_HANDLE;
    if(op==f::Op::Free){s.Drop(*r);return done(S_OK);}
    if(op==f::Op::Write){if(r->kind!=f::Kind::Stream||c.offset!=r->written||uint64_t(c.offset)+c.count>r->data.size())return done(E_INVALIDARG);std::memcpy(r->data.data()+c.offset,c.pixels,c.count);r->written+=c.count;return done(S_OK);}
    if(op==f::Op::Decode){
        if(r->kind!=f::Kind::Stream||r->written!=r->data.size())return done(WINCODEC_ERR_WRONGSTATE);
        CLSID decoder{};if(r->data.size()>=8&&!std::memcmp(r->data.data(),"\x89PNG\r\n\x1a\n",8))decoder=CLSID_WICPngDecoder;
        else if(r->data.size()>=2&&r->data[0]=='B'&&r->data[1]=='M')decoder=CLSID_WICBmpDecoder;
        else if(r->data.size()>=3&&r->data[0]==0xFF&&r->data[1]==0xD8&&r->data[2]==0xFF)decoder=CLSID_WICJpegDecoder;
        else return done(WINCODEC_ERR_COMPONENTNOTFOUND);
        auto hr=s.Factories();if(FAILED(hr))return done(hr);
        ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> codec;ComPtr<IWICBitmapFrameDecode> frame;
        hr=s.wic->CreateStream(&stream);if(SUCCEEDED(hr))hr=stream->InitializeFromMemory(r->data.data(),DWORD(r->data.size()));
        if(SUCCEEDED(hr))hr=s.Codec(decoder,IID_PPV_ARGS(&codec));
        if(SUCCEEDED(hr))hr=codec->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand);
        UINT count=0,width=0,height=0;if(SUCCEEDED(hr))hr=codec->GetFrameCount(&count);if(SUCCEEDED(hr)&&count!=1)hr=WINCODEC_ERR_UNSUPPORTEDOPERATION;
        if(SUCCEEDED(hr))hr=codec->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=frame->GetSize(&width,&height);
        if(SUCCEEDED(hr)&&(!width||!height||width>f::MaxDimension||height>f::MaxDimension))hr=WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
        if(FAILED(hr))return done(hr);
        // Decoder streams borrow their input: snapshot pixels before the upload
        // object can be released. No native pointer or decoder crosses the wire.
        ComPtr<IWICBitmap> snapshot;hr=s.wic->CreateBitmapFromSource(frame.Get(),WICBitmapCacheOnLoad,&snapshot);if(FAILED(hr))return done(hr);
        auto* slot=s.Add(f::Kind::Image,pid,tid,uint64_t(width)*height*4);if(!slot)return done(E_OUTOFMEMORY);
        frame->GetResolution(&slot->dpiX,&slot->dpiY);slot->format=IsEqualGUID(decoder,CLSID_WICPngDecoder)?1u:IsEqualGUID(decoder,CLSID_WICBmpDecoder)?2u:3u;
        slot->image=snapshot;slot->width=width;slot->height=height;hr=snapshot->GetPixelFormat(&slot->pixelFormat);
        if(IsEqualGUID(slot->pixelFormat,GUID_WICPixelFormat24bppBGR)||IsEqualGUID(slot->pixelFormat,GUID_WICPixelFormat24bppRGB))slot->bpp=3;
        if(IsEqualGUID(slot->pixelFormat,GUID_WICPixelFormat32bppBGRA)||IsEqualGUID(slot->pixelFormat,GUID_WICPixelFormat32bppPBGRA)||IsEqualGUID(slot->pixelFormat,GUID_WICPixelFormat32bppRGBA))slot->bpp=4;
        if(FAILED(hr)){s.Drop(*slot);return done(hr);}s.used|=4;return result(slot);
    }
    if(op==f::Op::Convert){
        if(r->kind!=f::Kind::Image)return done(E_INVALIDARG);auto hr=s.Factories();if(FAILED(hr))return done(hr);ComPtr<IWICFormatConverter> converter;
        if(!r->bpp&&!IsEqualGUID(r->pixelFormat,GUID_WICPixelFormat8bppIndexed))return done(WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT);
        hr=s.wic->CreateFormatConverter(&converter);const auto format=c.flags==1?GUID_WICPixelFormat32bppPBGRA:GUID_WICPixelFormat32bppBGRA;
        if(SUCCEEDED(hr))hr=converter->Initialize(r->image.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
        if(FAILED(hr))return done(hr);auto* slot=s.Add(f::Kind::Image,pid,tid,uint64_t(r->width)*r->height*4);if(!slot)return done(E_OUTOFMEMORY);
        slot->image=converter;slot->pixelFormat=format;slot->width=r->width;slot->height=r->height;slot->bpp=4;slot->dpiX=r->dpiX;slot->dpiY=r->dpiY;slot->format=r->format;return result(slot);
    }
    if(op==f::Op::SourceInfo){if(r->kind!=f::Kind::Image&&r->kind!=f::Kind::Texture)return done(E_INVALIDARG);reply.rect={0,0,int32_t(r->width),int32_t(r->height)};reply.selected[0]=r->bpp;reply.selected[1]=r->format;std::memcpy(reply.pixels,&r->pixelFormat,16);std::memcpy(reply.pixels+4,&r->dpiX,8);std::memcpy(reply.pixels+6,&r->dpiY,8);return done(S_OK);}
    if(op==f::Op::Read){
        auto hr=r->kind==f::Kind::Texture?(c.offset==0?s.ReadTexture(*r):S_OK):r->kind==f::Kind::Image?s.Pixels(*r):E_INVALIDARG;
        if(FAILED(hr))return done(hr);if(uint64_t(c.offset)+c.count>r->data.size())return done(E_INVALIDARG);
        std::memcpy(reply.pixels,r->data.data()+c.offset,c.count);reply.value=c.count;return done(S_OK);
    }
    if(op==f::Op::Texture){
        if(r->kind!=f::Kind::Device)return done(E_INVALIDARG);const auto size=uint64_t(c.args[0])*uint32_t(c.args[1])*4;
        auto* slot=s.Add(f::Kind::Texture,pid,tid,size*2);if(!slot)return done(E_OUTOFMEMORY);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=UINT(c.args[0]);desc.Height=UINT(c.args[1]);desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT(c.color);desc.SampleDesc.Count=1;
        desc.Usage=c.flags?D3D11_USAGE_STAGING:D3D11_USAGE_DEFAULT;desc.BindFlags=c.mode?D3D11_BIND_RENDER_TARGET:0;desc.CPUAccessFlags=c.flags?D3D11_CPU_ACCESS_READ:0;
        std::vector<uint8_t> zero(size_t(size),0);D3D11_SUBRESOURCE_DATA initial{zero.data(),desc.Width*4,0};
        const auto hr=r->device->CreateTexture2D(&desc,&initial,&slot->texture);if(FAILED(hr)){s.Drop(*slot);return done(hr);}
        slot->device=r->device;slot->context=r->context;slot->parent=r->id;slot->width=desc.Width;slot->height=desc.Height;slot->format=c.color;slot->bpp=4;return result(slot);
    }
    if(op==f::Op::View){if(r->kind!=f::Kind::Texture)return done(E_INVALIDARG);auto* slot=s.Add(f::Kind::View,pid,tid);if(!slot)return done(E_OUTOFMEMORY);
        const auto hr=r->device->CreateRenderTargetView(r->texture.Get(),nullptr,&slot->view);if(FAILED(hr)){s.Drop(*slot);return done(hr);}slot->device=r->device;slot->context=r->context;slot->parent=r->id;return result(slot);}
    if(op==f::Op::ClearView){if(r->kind!=f::Kind::View)return done(E_INVALIDARG);const auto color=Color(c);const float values[]{color.r,color.g,color.b,color.a};r->context->ClearRenderTargetView(r->view.Get(),values);if(auto* parent=s.Find(r->parent))parent->data.clear();return done(S_OK);}
    if(op==f::Op::Copy){if(r->kind!=f::Kind::Texture||!own(object)||object->kind!=f::Kind::Texture||r->parent!=object->parent||r->width!=object->width||r->height!=object->height||r->format!=object->format)return done(E_INVALIDARG);r->context->CopyResource(r->texture.Get(),object->texture.Get());r->data.clear();return done(S_OK);}
    if(op==f::Op::Present){
        if(r->kind!=f::Kind::Texture)return done(E_INVALIDARG);const auto binding=bind(c.object,false,pid,tid);if(!binding.dc)return ERROR_INVALID_HANDLE;
        auto hr=s.ReadTexture(*r);if(FAILED(hr))return done(hr);std::vector<uint8_t> bgra=r->data;
        if(r->format==28)for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(r->width);info.bmiHeader.biHeight=-LONG(r->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        if(!SetDIBitsToDevice(binding.dc,0,0,r->width,r->height,0,0,0,r->height,bgra.data(),&info,DIB_RGB_COLORS))return done(E_FAIL);
        present(binding.window);++s.presents;return done(S_OK);
    }
    if(op==f::Op::Flush){if(r->kind!=f::Kind::Device)return done(E_INVALIDARG);r->context->Flush();return done(r->device->GetDeviceRemovedReason());}
    if(op==f::Op::Status){if(r->kind!=f::Kind::Device)return done(E_INVALIDARG);return done(r->device->GetDeviceRemovedReason());}
    if(op==f::Op::ClearState){if(r->kind!=f::Kind::Device)return done(E_INVALIDARG);r->context->ClearState();return done(S_OK);}
    if(op==f::Op::Bind){if(r->kind!=f::Kind::Device||c.object&&(!own(object)||object->kind!=f::Kind::View||object->device.Get()!=r->device.Get()))return done(E_INVALIDARG);ID3D11RenderTargetView* view=object?object->view.Get():nullptr;r->context->OMSetRenderTargets(view?1u:0u,view?&view:nullptr,nullptr);return done(S_OK);}
    if(op==f::Op::Viewport){if(r->kind!=f::Kind::Device||Float(c.args[4])>Float(c.args[5]))return done(E_INVALIDARG);D3D11_VIEWPORT viewport{};std::memcpy(&viewport,c.args,24);r->context->RSSetViewports(1,&viewport);return done(S_OK);}
    if(op==f::Op::Query){if(r->kind!=f::Kind::Device)return done(E_INVALIDARG);auto* slot=s.Add(f::Kind::Query,pid,tid);if(!slot)return done(E_OUTOFMEMORY);D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
        const auto hr=r->device->CreateQuery(&desc,&slot->query);if(FAILED(hr)){s.Drop(*slot);return done(hr);}slot->device=r->device;slot->context=r->context;slot->parent=r->id;return result(slot);}
    if(op==f::Op::Signal){if(r->kind!=f::Kind::Query)return done(E_INVALIDARG);r->context->End(r->query.Get());r->context->Flush();r->signaled=true;return done(S_OK);}
    if(op==f::Op::Poll){if(r->kind!=f::Kind::Query||!r->signaled)return done(DXGI_ERROR_INVALID_CALL);BOOL ready=FALSE;const auto hr=r->context->GetData(r->query.Get(),&ready,sizeof(ready),0);reply.value=ready;return done(hr);}
    if(op==f::Op::FormatState){if(r->kind!=f::Kind::Format)return done(E_INVALIDARG);return done(c.flags==0?r->text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT(c.mode)):c.flags==1?r->text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT(c.mode)):r->text->SetWordWrapping(DWRITE_WORD_WRAPPING(c.mode)));}
    if(op==f::Op::Bitmap){
        if(r->kind!=f::Kind::Target||!own(object)||object->kind!=f::Kind::Image)return done(E_INVALIDARG);
        auto* slot=s.Add(f::Kind::Bitmap,pid,tid,uint64_t(object->width)*object->height*4);if(!slot)return done(E_OUTOFMEMORY);
        const auto hr=r->target->CreateBitmapFromWicBitmap(object->image.Get(),nullptr,&slot->bitmap);if(FAILED(hr)){s.Drop(*slot);return done(hr);}
        slot->parent=r->id;slot->width=object->width;slot->height=object->height;return result(slot);
    }
    if(op==f::Op::Brush){
        if(r->kind==f::Kind::Brush){const auto color=Color(c);r->brush->SetColor(&color);return done(S_OK);}
        if(r->kind!=f::Kind::Target)return done(E_INVALIDARG);auto* slot=s.Add(f::Kind::Brush,pid,tid);if(!slot)return done(E_OUTOFMEMORY);
        const auto color=Color(c);const auto hr=r->target->CreateSolidColorBrush(&color,nullptr,&slot->brush);if(FAILED(hr)){s.Drop(*slot);return done(hr);}slot->parent=r->id;return result(slot);
    }
    if(r->kind!=f::Kind::Target)return done(E_INVALIDARG);
    if(op==f::Op::Rebind){if(r->begun)return done(D2DERR_WRONG_STATE);const auto binding=bind(c.object,true,pid,tid);if(!binding.dc)return ERROR_INVALID_HANDLE;if(c.rect.right!=binding.width||c.rect.bottom!=binding.height)return done(E_INVALIDARG);RECT rect{0,0,binding.width,binding.height};const auto hr=r->target->BindDC(binding.dc,&rect);if(FAILED(hr))return done(hr);r->window=binding.window;r->width=uint32_t(binding.width);r->height=uint32_t(binding.height);return result(r);}
    if(op==f::Op::Begin){
        if(r->begun)return done(D2DERR_WRONG_STATE);const auto binding=bind(r->window,false,pid,tid);if(!binding.dc)return done(D2DERR_RECREATE_TARGET);
        RECT rect{0,0,binding.width,binding.height};const auto hr=r->target->BindDC(binding.dc,&rect);if(FAILED(hr))return done(hr);r->width=uint32_t(binding.width);r->height=uint32_t(binding.height);r->target->BeginDraw();r->begun=true;r->error=S_OK;return result(r);
    }
    if(op==f::Op::End){if(!r->begun)return done(D2DERR_WRONG_STATE);if(r->clips&&SUCCEEDED(r->error))r->error=D2DERR_PUSH_POP_UNBALANCED;while(r->clips){r->target->PopAxisAlignedClip();--r->clips;}const auto hr=r->target->EndDraw();r->begun=false;if(SUCCEEDED(hr)&&SUCCEEDED(r->error)){present(r->window);++s.presents;}return done(FAILED(r->error)?r->error:hr);}
    if(op==f::Op::Transform){D2D1_MATRIX_3X2_F transform{};std::memcpy(&transform,c.args,24);r->target->SetTransform(&transform);return done(S_OK);}
    if(!r->begun)return done(D2DERR_WRONG_STATE);
    auto failed=[&](HRESULT hr){if(SUCCEEDED(r->error))r->error=hr;return done(hr);};
    if(op==f::Op::Clear){const auto color=Color(c);r->target->Clear(&color);return done(S_OK);}
    const D2D1_RECT_F rect{Float(c.args[0]),Float(c.args[1]),Float(c.args[2]),Float(c.args[3])};
    if(op==f::Op::Clip){if(r->clips==8)return failed(E_OUTOFMEMORY);r->target->PushAxisAlignedClip(&rect,D2D1_ANTIALIAS_MODE(c.flags));++r->clips;return done(S_OK);}
    if(op==f::Op::PopClip){if(!r->clips)return failed(D2DERR_WRONG_STATE);r->target->PopAxisAlignedClip();--r->clips;return done(S_OK);}
    if(op==f::Op::DrawBitmap){if(!own(object)||object->kind!=f::Kind::Bitmap||object->parent!=r->id)return failed(D2DERR_WRONG_RESOURCE_DOMAIN);r->target->DrawBitmap(object->bitmap.Get(),&rect,Float(c.args[4]),D2D1_BITMAP_INTERPOLATION_MODE(c.flags),nullptr);return done(S_OK);}
    if(!own(object)||object->kind!=f::Kind::Brush||object->parent!=r->id)return failed(D2DERR_WRONG_RESOURCE_DOMAIN);
    switch(op){
    case f::Op::Rectangle:if(c.flags)r->target->FillRectangle(&rect,object->brush.Get());else r->target->DrawRectangle(&rect,object->brush.Get(),Float(c.args[4]));break;
    case f::Op::Line:r->target->DrawLine({rect.left,rect.top},{rect.right,rect.bottom},object->brush.Get(),Float(c.args[4]));break;
    case f::Op::Ellipse:{const D2D1_ELLIPSE ellipse{{rect.left,rect.top},rect.right,rect.bottom};if(rect.right<0||rect.bottom<0)return failed(E_INVALIDARG);if(c.flags)r->target->FillEllipse(&ellipse,object->brush.Get());else r->target->DrawEllipse(&ellipse,object->brush.Get(),Float(c.args[4]));break;}
    case f::Op::Text:if(!own(other)||other->kind!=f::Kind::Format)return failed(E_INVALIDARG);r->target->DrawText(reinterpret_cast<const wchar_t*>(c.text),c.count,other->text.Get(),&rect,object->brush.Get(),D2D1_DRAW_TEXT_OPTIONS(c.flags),DWRITE_MEASURING_MODE_NATURAL);break;
    default:return failed(E_NOTIMPL);
    }
    return done(S_OK);
}
}
