class WicStream final:public Stub_IWICStream {
public:
    BYTE* data=nullptr;DWORD length=0,position=0;
    ~WicStream(){if(data)HeapFree(GetProcessHeap(),0,data);}
    HRESULT STDMETHODCALLTYPE InitializeFromMemory(BYTE* source,DWORD count) override {
        if(FAILED(Check()))return Check();if(id)return WINCODEC_ERR_WRONGSTATE;if(!source||!count||count>f::MaxEncoded)return E_INVALIDARG;
        data=static_cast<BYTE*>(HeapAlloc(GetProcessHeap(),0,count));if(!data)return E_OUTOFMEMORY;Copy(data,source,count);
        auto call=Command(f::Op::Stream);call.draw.count=count;auto hr=New(f::Op::Stream,*this,call);
        if(SUCCEEDED(hr))for(uint32_t offset=0;offset<count;){auto part=Command(f::Op::Write,id);part.draw.offset=offset;part.draw.count=count-offset>512?512:count-offset;Copy(part.draw.pixels,data+offset,part.draw.count);hr=Send(part);if(FAILED(hr))break;offset+=part.draw.count;}
        if(FAILED(hr)){if(id){auto drop=Command(f::Op::Free,id);Send(drop);id=0;}HeapFree(GetProcessHeap(),0,data);data=nullptr;return hr;}
        length=count;position=0;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InitializeFromFilename(const WCHAR* filename,DWORD access) override {
        if(!filename||access!=GENERIC_READ||id)return E_INVALIDARG;
        const auto file=CreateFileW(filename,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());LARGE_INTEGER size{};
        if(!GetFileSizeEx(file,&size)||size.QuadPart<1||size.QuadPart>f::MaxEncoded){CloseHandle(file);return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;}
        auto* bytes=static_cast<BYTE*>(HeapAlloc(GetProcessHeap(),0,SIZE_T(size.QuadPart)));if(!bytes){CloseHandle(file);return E_OUTOFMEMORY;}
        DWORD read=0;const auto ok=ReadFile(file,bytes,DWORD(size.QuadPart),&read,nullptr);const auto last=GetLastError();CloseHandle(file);
        const auto hr=ok&&read==DWORD(size.QuadPart)?InitializeFromMemory(bytes,read):HRESULT_FROM_WIN32(last?last:ERROR_HANDLE_EOF);
        HeapFree(GetProcessHeap(),0,bytes);return hr;
    }
    HRESULT STDMETHODCALLTYPE Read(void* buffer,ULONG count,ULONG* read) override {if(read)*read=0;if(FAILED(Check()))return Check();if(!data)return WINCODEC_ERR_NOTINITIALIZED;if(!buffer&&count)return E_POINTER;const auto amount=count<length-position?count:length-position;Copy(buffer,data+position,amount);position+=amount;if(read)*read=amount;return amount==count?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG* written) override {if(written)*written=0;return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move,DWORD origin,ULARGE_INTEGER* result) override {if(FAILED(Check()))return Check();if(!data)return WINCODEC_ERR_NOTINITIALIZED;const int64_t base=origin==STREAM_SEEK_SET?0:origin==STREAM_SEEK_CUR?position:origin==STREAM_SEEK_END?length:-1;if(base<0||move.QuadPart < -base||move.QuadPart>int64_t(length)-base)return STG_E_INVALIDFUNCTION;position=DWORD(base+move.QuadPart);if(result)result->QuadPart=position;return S_OK;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* out,DWORD flags) override {if(!out)return E_POINTER;Zero(out,sizeof(*out));if(flags>STATFLAG_NONAME)return E_INVALIDARG;if(!data)return WINCODEC_ERR_NOTINITIALIZED;out->type=STGTY_STREAM;out->cbSize.QuadPart=length;out->grfMode=STGM_READ;return S_OK;}
};
template<class Base> class WicSource:public Base {
public:
    using Base::id;using Base::Check;
    UINT width=0,height=0,bpp=0,container=0;GUID pixel{};double dpiX=96,dpiY=96;
    HRESULT Info(){auto call=Command(f::Op::SourceInfo,id);const auto hr=Send(call);if(FAILED(hr))return hr;width=UINT(call.graphics.rect.right);height=UINT(call.graphics.rect.bottom);bpp=UINT(call.graphics.selected[0]);container=UINT(call.graphics.selected[1]);Copy(&pixel,call.graphics.pixels,16);Copy(&dpiX,call.graphics.pixels+4,8);Copy(&dpiY,call.graphics.pixels+6,8);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSize(UINT* x,UINT* y) override {if(!x||!y)return E_POINTER;*x=*y=0;if(FAILED(Check()))return Check();if(!id)return WINCODEC_ERR_NOTINITIALIZED;*x=width;*y=height;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelFormat(GUID* out) override {if(!out)return E_POINTER;Zero(out,16);if(FAILED(Check()))return Check();if(!id)return WINCODEC_ERR_NOTINITIALIZED;*out=pixel;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetResolution(double* x,double* y) override {if(!x||!y)return E_POINTER;if(FAILED(Check()))return Check();if(!id)return WINCODEC_ERR_NOTINITIALIZED;*x=dpiX;*y=dpiY;return S_OK;}
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* requested,UINT stride,UINT size,BYTE* data) override {
        if(FAILED(Check()))return Check();if(!id)return WINCODEC_ERR_NOTINITIALIZED;if(!data)return E_POINTER;if(!bpp)return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        const WICRect all{0,0,INT(width),INT(height)};const auto& rect=requested?*requested:all;
        if(rect.X<0||rect.Y<0||rect.Width<1||rect.Height<1||uint64_t(rect.X)+uint32_t(rect.Width)>width||uint64_t(rect.Y)+uint32_t(rect.Height)>height)return E_INVALIDARG;
        const auto row=uint64_t(rect.Width)*bpp;if(stride<row||stride>8192||uint64_t(stride)*(rect.Height-1)+row>size)return E_INVALIDARG;
        for(INT y=0;y<rect.Height;++y)for(uint32_t offset=0;offset<row;){auto call=Command(f::Op::Read,id);call.draw.offset=uint32_t((uint64_t(rect.Y+y)*width+uint32_t(rect.X))*bpp+offset);call.draw.count=uint32_t(row-offset>512?512:row-offset);
            const auto hr=Send(call);if(FAILED(hr))return hr;Copy(data+size_t(y)*stride+offset,call.graphics.pixels,call.draw.count);offset+=call.draw.count;}
        return S_OK;
    }
};
class WicFrame final:public WicSource<Stub_IWICBitmapFrameDecode> {};
class WicConverter final:public WicSource<Stub_IWICFormatConverter> {
public:
    HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* source,REFGUID destination,WICBitmapDitherType dither,IWICPalette* palette,double alpha,WICBitmapPaletteType paletteType) override {
        if(FAILED(Check()))return Check();if(id)return WINCODEC_ERR_WRONGSTATE;if(dither!=WICBitmapDitherTypeNone||palette||alpha!=0||paletteType!=WICBitmapPaletteTypeCustom)return E_NOTIMPL;
        auto* object=Object(source);if(!object||FAILED(object->Check())||object->family!=11&&object->family!=12||!object->id)return E_INVALIDARG;
        const auto format=Same(destination,GUID_WICPixelFormat32bppPBGRA)?1u:Same(destination,GUID_WICPixelFormat32bppBGRA)?2u:0u;if(!format)return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        auto call=Command(f::Op::Convert,object->id);call.draw.flags=format;const auto hr=Send(call);if(FAILED(hr))return hr;id=call.graphics.value;return Info();
    }
    HRESULT STDMETHODCALLTYPE CanConvert(REFGUID source,REFGUID destination,BOOL* result) override {if(!result)return E_POINTER;*result=FALSE;if(!Same(destination,GUID_WICPixelFormat32bppPBGRA)&&!Same(destination,GUID_WICPixelFormat32bppBGRA))return S_OK;*result=Same(source,GUID_WICPixelFormat24bppBGR)||Same(source,GUID_WICPixelFormat24bppRGB)||Same(source,GUID_WICPixelFormat32bppBGRA)||Same(source,GUID_WICPixelFormat32bppRGBA)||Same(source,GUID_WICPixelFormat32bppPBGRA)||Same(source,GUID_WICPixelFormat8bppIndexed);return S_OK;}
};
class WicDecoder final:public Stub_IWICBitmapDecoder {
public:
    Keep frame;GUID expected{};
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream,WICDecodeOptions options) override {
        if(FAILED(Check()))return Check();if(frame.value)return WINCODEC_ERR_WRONGSTATE;if(options!=WICDecodeMetadataCacheOnDemand&&options!=WICDecodeMetadataCacheOnLoad)return E_INVALIDARG;
        auto* source=Object(stream);if(!source||source->family!=9||FAILED(source->Check())||!source->id)return E_INVALIDARG;
        auto call=Command(f::Op::Decode,source->id);const auto hr=Send(call);if(FAILED(hr))return hr;
        auto* result=Make<WicFrame>();if(!result){auto drop=Command(f::Op::Free,call.graphics.value);Send(drop);return E_OUTOFMEMORY;}result->id=call.graphics.value;
        const auto info=result->Info();if(FAILED(info)){result->Release();return info;}
        const GUID container=result->container==1?GUID_ContainerFormatPng:result->container==2?GUID_ContainerFormatBmp:GUID_ContainerFormatJpeg;
        if(!Same(expected,GUID_NULL)&&!Same(expected,container)){result->Release();return WINCODEC_ERR_BADIMAGE;}
        expected=container;frame.Set(result);result->Release();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* out) override {if(!out)return E_POINTER;*out=expected;return Same(expected,GUID_NULL)?WINCODEC_ERR_NOTINITIALIZED:S_OK;}
    HRESULT STDMETHODCALLTYPE GetFrameCount(UINT* count) override {if(!count)return E_POINTER;*count=0;if(!frame.value)return WINCODEC_ERR_NOTINITIALIZED;*count=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetFrame(UINT index,IWICBitmapFrameDecode** out) override {if(!out)return E_POINTER;*out=nullptr;if(!frame.value)return WINCODEC_ERR_NOTINITIALIZED;if(index)return WINCODEC_ERR_FRAMEMISSING;*out=static_cast<IWICBitmapFrameDecode*>(frame.value);(*out)->AddRef();return S_OK;}
};
class WicFactory final:public Stub_IWICImagingFactory {
public:
    HRESULT STDMETHODCALLTYPE CreateStream(IWICStream** out) override {if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();auto* stream=Make<WicStream>();if(!stream)return E_OUTOFMEMORY;*out=stream;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateFormatConverter(IWICFormatConverter** out) override {if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();auto* converter=Make<WicConverter>();if(!converter)return E_OUTOFMEMORY;*out=converter;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateDecoder(REFGUID container,const GUID* vendor,IWICBitmapDecoder** out) override {if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(vendor&&!Same(*vendor,GUID_VendorMicrosoft))return WINCODEC_ERR_COMPONENTNOTFOUND;if(!Same(container,GUID_ContainerFormatPng)&&!Same(container,GUID_ContainerFormatBmp)&&!Same(container,GUID_ContainerFormatJpeg))return WINCODEC_ERR_COMPONENTNOTFOUND;auto* decoder=Make<WicDecoder>();if(!decoder)return E_OUTOFMEMORY;decoder->expected=container;*out=decoder;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateDecoderFromStream(IStream* stream,const GUID* vendor,WICDecodeOptions options,IWICBitmapDecoder** out) override {if(!out)return E_POINTER;*out=nullptr;if(vendor&&!Same(*vendor,GUID_VendorMicrosoft))return WINCODEC_ERR_COMPONENTNOTFOUND;auto* decoder=Make<WicDecoder>();if(!decoder)return E_OUTOFMEMORY;const auto hr=decoder->Initialize(stream,options);if(FAILED(hr)){decoder->Release();return hr;}*out=decoder;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateDecoderFromFilename(const WCHAR* filename,const GUID* vendor,DWORD access,WICDecodeOptions options,IWICBitmapDecoder** out) override {
        if(!out)return E_POINTER;*out=nullptr;auto* stream=Make<WicStream>();if(!stream)return E_OUTOFMEMORY;auto hr=stream->InitializeFromFilename(filename,access);
        if(SUCCEEDED(hr))hr=CreateDecoderFromStream(stream,vendor,options,out);stream->Release();return hr;
    }
};
volatile LONG graphicsLocks=0;
class WicClassFactory final:public GraphicsUnknown<IClassFactory> {
public:
    WicClassFactory():GraphicsUnknown(22){}
    bool Accept(REFIID iid) const override{return Same(iid,__uuidof(IClassFactory));}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;auto* value=Make<WicFactory>();if(!value)return E_OUTOFMEMORY;const auto hr=value->QueryInterface(iid,out);value->Release();return hr;}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL locked) override {if(locked)_InterlockedIncrement(&graphicsLocks);else {Lock guard;if(graphicsLocks<=0)return E_UNEXPECTED;_InterlockedDecrement(&graphicsLocks);}return S_OK;}
};
