class DxgiFactory final:public Stub_IDXGIFactory {
public:
    HWND association=nullptr;
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT index,IDXGIAdapter** out) override;
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**) override;
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND window,UINT flags) override {RECT rect{};if(flags&~7u||window&&!GetClientRect(window,&rect))return DXGI_ERROR_INVALID_CALL;association=window;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* out) override {if(!out)return E_POINTER;*out=association;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE,IDXGIAdapter** out) override {if(out)*out=nullptr;return DXGI_ERROR_UNSUPPORTED;}
};
class DxgiAdapter final:public Stub_IDXGIAdapter {
public:
    Keep factory;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;return factory.value?factory.value->QueryInterface(iid,out):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT,IDXGIOutput** out) override {if(!out)return E_POINTER;*out=nullptr;return DXGI_ERROR_NOT_FOUND;}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* out) override {if(!out)return E_POINTER;Zero(out,sizeof(*out));const wchar_t description[]=L"Aegis WARP software adapter";Copy(out->Description,description,sizeof(description));return S_OK;}
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID guid,LARGE_INTEGER* version) override {if(version)version->QuadPart=0;return Same(guid,__uuidof(ID3D11Device))?S_OK:DXGI_ERROR_UNSUPPORTED;}
};
class DxgiDevice final:public Stub_IDXGIDevice {
public:
    Keep device;
    D3DDevice* Device() const {return static_cast<D3DDevice*>(device.value);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(Same(iid,__uuidof(IUnknown))||Same(iid,__uuidof(ID3D11Device)))return device.value->QueryInterface(iid,out);return GraphicsUnknown<IDXGIDevice>::QueryInterface(iid,out);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid,void** out) override {IDXGIAdapter* adapter=nullptr;const auto hr=GetAdapter(&adapter);if(FAILED(hr))return hr;const auto result=adapter->QueryInterface(iid,out);adapter->Release();return result;}
    HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter** out) override {
        if(!out)return E_POINTER;*out=nullptr;auto* value=Device();if(!value->adapter.value){auto* factory=Make<DxgiFactory>();if(!factory)return E_OUTOFMEMORY;IDXGIAdapter* adapter=nullptr;const auto hr=factory->EnumAdapters(0,&adapter);factory->Release();if(FAILED(hr))return hr;value->adapter.Set(adapter);adapter->Release();}
        return value->adapter.value->QueryInterface(__uuidof(IDXGIAdapter),reinterpret_cast<void**>(out));
    }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT priority) override {return priority?DXGI_ERROR_UNSUPPORTED:S_OK;}
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* out) override {if(!out)return E_POINTER;*out=0;return S_OK;}
};
class DxgiSwapChain final:public Stub_IDXGISwapChain {
public:
    Keep device,factory,buffer;DXGI_SWAP_CHAIN_DESC desc{};UINT presents=0;
    HRESULT Replace(UINT width,UINT height,DXGI_FORMAT format){
        D3D11_TEXTURE2D_DESC texture{};texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=1;texture.Format=format;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D* created=nullptr;const auto hr=static_cast<D3DDevice*>(device.value)->CreateTexture2D(&texture,nullptr,&created);if(FAILED(hr))return hr;
        buffer.Set(created);created->Release();desc.BufferDesc.Width=width;desc.BufferDesc.Height=height;desc.BufferDesc.Format=format;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;return device.value->QueryInterface(iid,out);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;return factory.value->QueryInterface(iid,out);}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(index||!buffer.value)return DXGI_ERROR_INVALID_CALL;return buffer.value->QueryInterface(iid,out);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* out) override {if(!out)return E_POINTER;*out=desc;return S_OK;}
    HRESULT STDMETHODCALLTYPE Present(UINT interval,UINT flags) override {
        if(FAILED(Check()))return Check();if(interval||flags&~DXGI_PRESENT_TEST)return DXGI_ERROR_UNSUPPORTED;
        if(flags&DXGI_PRESENT_TEST){RECT client{};return GetClientRect(desc.OutputWindow,&client)?S_OK:DXGI_ERROR_INVALID_CALL;}
        auto* texture=static_cast<D3DTexture*>(buffer.value);if(!texture||texture->mapping)return DXGI_ERROR_INVALID_CALL;
        auto call=Command(f::Op::Present,texture->id);call.draw.object=uint64_t(desc.OutputWindow);const auto hr=Send(call);if(SUCCEEDED(hr))++presents;return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) override {
        if(FAILED(Check()))return Check();auto* texture=static_cast<D3DTexture*>(buffer.value);if(!texture||texture->References()!=1||texture->mapping||count>1||flags)return DXGI_ERROR_INVALID_CALL;
        RECT client{};if(!GetClientRect(desc.OutputWindow,&client))return DXGI_ERROR_INVALID_CALL;if(!width)width=UINT(client.right);if(!height)height=UINT(client.bottom);if(format==DXGI_FORMAT_UNKNOWN)format=desc.BufferDesc.Format;
        return Replace(width,height,format);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen,IDXGIOutput* output) override {return fullscreen||output?DXGI_ERROR_UNSUPPORTED:S_OK;}
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen,IDXGIOutput** output) override {if(!fullscreen)return E_POINTER;*fullscreen=FALSE;if(output)*output=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** out) override {if(!out)return E_POINTER;*out=nullptr;return DXGI_ERROR_NOT_FOUND;}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* out) override {if(!out)return E_POINTER;*out=presents;return S_OK;}
};
HRESULT DxgiFactory::EnumAdapters(UINT index,IDXGIAdapter** out){if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(index)return DXGI_ERROR_NOT_FOUND;auto* adapter=Make<DxgiAdapter>();if(!adapter)return E_OUTOFMEMORY;adapter->factory.Set(this);*out=adapter;return S_OK;}
HRESULT DxgiFactory::CreateSwapChain(IUnknown* deviceValue,DXGI_SWAP_CHAIN_DESC* requested,IDXGISwapChain** out){
    if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();auto* object=Object(deviceValue);
    if(!requested||!object||object->family!=13||FAILED(object->Check())||!requested->Windowed||requested->Flags||requested->BufferCount!=1||requested->BufferUsage!=DXGI_USAGE_RENDER_TARGET_OUTPUT||requested->SampleDesc.Count!=1||requested->SampleDesc.Quality||requested->SwapEffect!=DXGI_SWAP_EFFECT_DISCARD&&requested->SwapEffect!=DXGI_SWAP_EFFECT_SEQUENTIAL)return DXGI_ERROR_UNSUPPORTED;
    RECT client{};if(!GetClientRect(requested->OutputWindow,&client))return DXGI_ERROR_INVALID_CALL;
    auto* chain=Make<DxgiSwapChain>();if(!chain)return E_OUTOFMEMORY;chain->device.Set(deviceValue);chain->factory.Set(this);chain->desc=*requested;
    const auto hr=chain->Replace(requested->BufferDesc.Width?requested->BufferDesc.Width:UINT(client.right),requested->BufferDesc.Height?requested->BufferDesc.Height:UINT(client.bottom),requested->BufferDesc.Format);
    if(FAILED(hr)){chain->Release();return hr;}*out=chain;return S_OK;
}
HRESULT DeviceDxgi(D3DDevice* device,REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(FAILED(device->Check()))return device->Check();auto* proxy=Make<DxgiDevice>();if(!proxy)return E_OUTOFMEMORY;proxy->device.Set(device);const auto hr=proxy->QueryInterface(iid,out);proxy->Release();return hr;}
HRESULT CreateDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** out,D3D_FEATURE_LEVEL* chosen,ID3D11DeviceContext** context){
    if(out)*out=nullptr;if(context)*context=nullptr;if(chosen)*chosen=D3D_FEATURE_LEVEL(0);
    if(sdk!=D3D11_SDK_VERSION||software||flags&~UINT(D3D11_CREATE_DEVICE_SINGLETHREADED|D3D11_CREATE_DEVICE_BGRA_SUPPORT)||levels&&(!count||count>8)||!levels&&count)return E_INVALIDARG;
    if(adapter){auto* object=Object(adapter);if(!object||object->family!=19||driver!=D3D_DRIVER_TYPE_UNKNOWN)return E_INVALIDARG;}
    else if(driver!=D3D_DRIVER_TYPE_WARP)return DXGI_ERROR_UNSUPPORTED;
    bool supported=!levels;for(UINT i=0;levels&&i<count;++i)if(levels[i]==D3D_FEATURE_LEVEL_9_1)supported=true;if(!supported)return DXGI_ERROR_UNSUPPORTED;
    auto* device=Make<D3DDevice>();if(!device)return E_OUTOFMEMORY;auto call=Command(f::Op::Device);const auto hr=New(f::Op::Device,*device,call);if(FAILED(hr)){device->Release();return hr;}
    device->flags=flags;device->level=D3D_FEATURE_LEVEL(call.graphics.selected[0]);if(adapter)device->adapter.Set(adapter);
    if(context){device->GetImmediateContext(context);if(!*context){device->Release();return E_OUTOFMEMORY;}}
    if(chosen)*chosen=device->level;if(out)*out=device;else device->Release();return S_OK;
}
