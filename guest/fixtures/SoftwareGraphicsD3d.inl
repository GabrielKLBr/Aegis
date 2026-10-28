class D3DDevice;class D3DContext;class DxgiFactory;
HRESULT DeviceDxgi(D3DDevice*,REFIID,void**);
class D3DDevice final:public Stub_ID3D11Device {
public:
    D3DContext* context=nullptr;Keep adapter;UINT flags=0;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_9_1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(Same(iid,__uuidof(IDXGIDevice))||Same(iid,__uuidof(IDXGIObject)))return DeviceDxgi(this,iid,out);return GraphicsUnknown<ID3D11Device>::QueryInterface(iid,out);}
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Resource*,const D3D11_RENDER_TARGET_VIEW_DESC*,ID3D11RenderTargetView**) override;
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC*,ID3D11Query**) override;
    void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext**) override;
    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel() override {return level;}
    UINT STDMETHODCALLTYPE GetCreationFlags() override {return flags;}
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override {if(FAILED(error))return error;auto call=Command(f::Op::Status,id);return Send(call);}
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT format,UINT* out) override {if(!out)return E_POINTER;*out=0;if(format!=DXGI_FORMAT_R8G8B8A8_UNORM&&format!=DXGI_FORMAT_B8G8R8A8_UNORM)return E_INVALIDARG;*out=D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_DISPLAY;return S_OK;}
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT format,UINT samples,UINT* out) override {if(!out)return E_POINTER;*out=0;if(format!=DXGI_FORMAT_R8G8B8A8_UNORM&&format!=DXGI_FORMAT_B8G8R8A8_UNORM)return E_INVALIDARG;if(samples==1)*out=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D11_FEATURE feature,void* data,UINT size) override {if(feature!=D3D11_FEATURE_THREADING)return E_NOTIMPL;if(!data||size!=sizeof(D3D11_FEATURE_DATA_THREADING))return E_INVALIDARG;auto& info=*static_cast<D3D11_FEATURE_DATA_THREADING*>(data);info.DriverConcurrentCreates=FALSE;info.DriverCommandLists=FALSE;return S_OK;}
};
class D3DTexture final:public Stub_ID3D11Texture2D {
public:
    Keep device;D3D11_TEXTURE2D_DESC desc{};BYTE* mapping=nullptr;
    ~D3DTexture(){if(mapping)HeapFree(GetProcessHeap(),0,mapping);}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** out) override {if(out){*out=static_cast<ID3D11Device*>(device.value);if(*out)(*out)->AddRef();}}
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* out) override {if(out)*out=D3D11_RESOURCE_DIMENSION_TEXTURE2D;}
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE2D_DESC* out) override {if(out)*out=desc;}
};
class D3DView final:public Stub_ID3D11RenderTargetView {
public:
    Keep texture;
    D3DTexture* Texture() const {return static_cast<D3DTexture*>(texture.value);}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** out) override {Texture()->GetDevice(out);}
    void STDMETHODCALLTYPE GetResource(ID3D11Resource** out) override {if(out){*out=Texture();(*out)->AddRef();}}
    void STDMETHODCALLTYPE GetDesc(D3D11_RENDER_TARGET_VIEW_DESC* out) override {if(out){Zero(out,sizeof(*out));out->Format=Texture()->desc.Format;out->ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;}}
};
class D3DQuery final:public Stub_ID3D11Query {
public:
    Keep device;
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** out) override {if(out){*out=static_cast<ID3D11Device*>(device.value);if(*out)(*out)->AddRef();}}
    UINT STDMETHODCALLTYPE GetDataSize() override {return sizeof(BOOL);}
    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC* out) override {if(out)*out={D3D11_QUERY_EVENT,0};}
};
class D3DContext final:public Stub_ID3D11DeviceContext {
public:
    Keep device,bound;D3D11_VIEWPORT viewport{};UINT viewports=0;
    D3DDevice* Device() const {return static_cast<D3DDevice*>(device.value);}
    HRESULT Call(g::Call& call){if(FAILED(Check()))return Check();const auto hr=Send(call);if(FAILED(hr))Device()->error=hr;return hr;}
    void Failure(){Device()->error=DXGI_ERROR_INVALID_CALL;Unsupported(114);}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** out) override {if(out){*out=Device();(*out)->AddRef();}}
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType() override {return D3D11_DEVICE_CONTEXT_IMMEDIATE;}
    UINT STDMETHODCALLTYPE GetContextFlags() override {return 0;}
    void STDMETHODCALLTYPE Flush() override {auto call=Command(f::Op::Flush,Device()->id);Call(call);}
    void STDMETHODCALLTYPE ClearState() override {auto call=Command(f::Op::ClearState,Device()->id);if(SUCCEEDED(Call(call))){bound.Set(nullptr);viewports=0;Zero(&viewport,sizeof(viewport));}}
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* view,const FLOAT color[4]) override {
        auto* object=Object(view);if(!object||object->family!=16||!color||static_cast<D3DView*>(object)->Texture()->device.value!=device.value){Failure();return;}
        auto call=Command(f::Op::ClearView,object->id);for(unsigned i=0;i<4;++i)call.draw.args[i]=Word(color[i]);Call(call);
    }
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT count,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* depth) override {
        if(count>1||depth||count&&(!views||!views[0])){Failure();return;}auto* object=count?Object(views[0]):nullptr;
        if(count&&(!object||object->family!=16||static_cast<D3DView*>(object)->Texture()->device.value!=device.value)){Failure();return;}
        auto call=Command(f::Op::Bind,Device()->id);call.draw.object=object?object->id:0;if(SUCCEEDED(Call(call)))bound.Set(count?views[0]:nullptr);
    }
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT count,ID3D11RenderTargetView** out,ID3D11DepthStencilView** depth) override {if(depth)*depth=nullptr;if(count>1||count&&!out){Failure();return;}if(count){*out=static_cast<ID3D11RenderTargetView*>(bound.value);if(*out)(*out)->AddRef();}}
    void STDMETHODCALLTYPE RSSetViewports(UINT count,const D3D11_VIEWPORT* values) override {if(count!=1||!values){Failure();return;}auto call=Command(f::Op::Viewport,Device()->id);Copy(call.draw.args,values,sizeof(*values));if(SUCCEEDED(Call(call))){viewport=*values;viewports=1;}}
    void STDMETHODCALLTYPE RSGetViewports(UINT* count,D3D11_VIEWPORT* out) override {if(!count){Failure();return;}if(out&&*count&&viewports)*out=viewport;*count=viewports;}
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource* to,ID3D11Resource* from) override {auto* a=Object(to);auto* b=Object(from);if(!a||!b||a==b||a->family!=15||b->family!=15||static_cast<D3DTexture*>(a)->device.value!=device.value||static_cast<D3DTexture*>(b)->device.value!=device.value||static_cast<D3DTexture*>(a)->mapping||static_cast<D3DTexture*>(b)->mapping){Failure();return;}auto call=Command(f::Op::Copy,a->id);call.draw.object=b->id;Call(call);}
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource* resource,UINT subresource,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* out) override {
        if(!out)return E_POINTER;Zero(out,sizeof(*out));auto* object=Object(resource);if(!object||object->family!=15||subresource||type!=D3D11_MAP_READ||flags)return E_INVALIDARG;auto* texture=static_cast<D3DTexture*>(object);
        if(texture->device.value!=device.value||texture->desc.Usage!=D3D11_USAGE_STAGING||texture->desc.CPUAccessFlags!=D3D11_CPU_ACCESS_READ||texture->mapping)return DXGI_ERROR_INVALID_CALL;
        const UINT size=texture->desc.Width*texture->desc.Height*4;auto* bytes=static_cast<BYTE*>(HeapAlloc(GetProcessHeap(),0,size));if(!bytes)return E_OUTOFMEMORY;
        for(UINT offset=0;offset<size;){auto call=Command(f::Op::Read,texture->id);call.draw.offset=offset;call.draw.count=size-offset>512?512:size-offset;const auto hr=Call(call);if(FAILED(hr)){HeapFree(GetProcessHeap(),0,bytes);return hr;}Copy(bytes+offset,call.graphics.pixels,call.draw.count);offset+=call.draw.count;}
        texture->mapping=bytes;out->pData=bytes;out->RowPitch=texture->desc.Width*4;out->DepthPitch=size;return S_OK;
    }
    void STDMETHODCALLTYPE Unmap(ID3D11Resource* resource,UINT subresource) override {auto* object=Object(resource);if(!object||object->family!=15||subresource||static_cast<D3DTexture*>(object)->device.value!=device.value||!static_cast<D3DTexture*>(object)->mapping){Failure();return;}auto* texture=static_cast<D3DTexture*>(object);HeapFree(GetProcessHeap(),0,texture->mapping);texture->mapping=nullptr;}
    void STDMETHODCALLTYPE End(ID3D11Asynchronous* asynchronous) override {auto* object=Object(asynchronous);if(!object||object->family!=17||static_cast<D3DQuery*>(object)->device.value!=device.value){Failure();return;}auto call=Command(f::Op::Signal,object->id);Call(call);}
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous* asynchronous,void* data,UINT size,UINT flags) override {auto* object=Object(asynchronous);if(!object||object->family!=17||static_cast<D3DQuery*>(object)->device.value!=device.value||flags||data&&size!=sizeof(BOOL)||!data&&size)return E_INVALIDARG;auto call=Command(f::Op::Poll,object->id);const auto hr=Send(call);if(hr==S_OK&&data)*static_cast<BOOL*>(data)=BOOL(call.graphics.value);return hr;}
};
void D3DDevice::GetImmediateContext(ID3D11DeviceContext** out){if(!out)return;*out=nullptr;if(FAILED(Check()))return;
    const auto* existing=context?Object(static_cast<ID3D11DeviceContext*>(context)):nullptr;if(!existing||existing->family!=14||context->device.value!=this){context=Make<D3DContext>();if(!context){error=E_OUTOFMEMORY;return;}context->device.Set(this);context->domain=&error;*out=context;return;}context->AddRef();*out=context;
}
HRESULT D3DDevice::CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc,const D3D11_SUBRESOURCE_DATA* initial,ID3D11Texture2D** out){
    if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(!desc)return E_POINTER;if(initial)return E_NOTIMPL;
    if(desc->MipLevels!=1||desc->ArraySize!=1||desc->SampleDesc.Count!=1||desc->SampleDesc.Quality||desc->MiscFlags||desc->Usage!=D3D11_USAGE_DEFAULT&&desc->Usage!=D3D11_USAGE_STAGING||desc->BindFlags&~D3D11_BIND_RENDER_TARGET||desc->CPUAccessFlags!=UINT(desc->Usage==D3D11_USAGE_STAGING?D3D11_CPU_ACCESS_READ:0)||desc->Usage==D3D11_USAGE_STAGING&&desc->BindFlags)return E_INVALIDARG;
    auto call=Command(f::Op::Texture,id);call.draw.args[0]=INT(desc->Width);call.draw.args[1]=INT(desc->Height);call.draw.flags=desc->Usage==D3D11_USAGE_STAGING;call.draw.mode=desc->BindFlags?1u:0u;call.draw.color=desc->Format;
    auto* texture=Make<D3DTexture>();if(!texture)return E_OUTOFMEMORY;const auto hr=New(f::Op::Texture,*texture,call);if(FAILED(hr)){texture->Release();return hr;}texture->desc=*desc;texture->device.Set(this);*out=texture;return S_OK;
}
HRESULT D3DDevice::CreateRenderTargetView(ID3D11Resource* resource,const D3D11_RENDER_TARGET_VIEW_DESC* desc,ID3D11RenderTargetView** out){
    if(!out)return E_POINTER;*out=nullptr;auto* object=Object(resource);if(!object||object->family!=15||static_cast<D3DTexture*>(object)->device.value!=this||desc)return E_INVALIDARG;
    auto* view=Make<D3DView>();if(!view)return E_OUTOFMEMORY;auto call=Command(f::Op::View,object->id);const auto hr=New(f::Op::View,*view,call);if(FAILED(hr)){view->Release();return hr;}view->texture.Set(static_cast<D3DTexture*>(object));*out=view;return S_OK;
}
HRESULT D3DDevice::CreateQuery(const D3D11_QUERY_DESC* desc,ID3D11Query** out){if(!out)return E_POINTER;*out=nullptr;if(!desc||desc->Query!=D3D11_QUERY_EVENT||desc->MiscFlags)return E_INVALIDARG;auto* query=Make<D3DQuery>();if(!query)return E_OUTOFMEMORY;auto call=Command(f::Op::Query,id);const auto hr=New(f::Op::Query,*query,call);if(FAILED(hr)){query->Release();return hr;}query->device.Set(this);*out=query;return S_OK;}
