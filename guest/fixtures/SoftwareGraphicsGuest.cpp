#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <d3d9.h>
#include <wincodec.h>
#include <intrin.h>
#include <new>
#include "guardian/SoftwareGraphics.h"
namespace f=guardian::softwaregraphics;namespace g=guardian::nativegdi;
extern "C" __declspec(dllimport) DWORD GuardianNativeGdiCall(g::Call*) noexcept;
extern "C" int _fltused=0;
extern "C" int __cdecl _purecall(){__fastfail(7);for(;;)_mm_pause();}
namespace {
bool Same(REFGUID a,REFGUID b){return InlineIsEqualGUID(a,b)!=0;}
void Copy(void* to,const void* from,size_t bytes){auto* out=static_cast<BYTE*>(to);const auto* in=static_cast<const BYTE*>(from);for(size_t i=0;i<bytes;++i)out[i]=in[i];}
void Zero(void* to,size_t bytes){auto* out=static_cast<BYTE*>(to);for(size_t i=0;i<bytes;++i)out[i]=0;}
int32_t Word(float value){int32_t word;Copy(&word,&value,4);return word;}
bool Text(const wchar_t* text,uint16_t* out,uint32_t& length,uint32_t limit=127){length=0;if(!text)return false;while(length<=limit&&text[length]){if(length==limit)return false;out[length]=uint16_t(text[length]);++length;}return g::Text(out,length);}
HRESULT Send(g::Call& call){
    if(!f::Valid(call.draw))return E_INVALIDARG;const auto error=GuardianNativeGdiCall(&call);
    if(error||call.graphics.status)return HRESULT_FROM_WIN32(error?error:call.graphics.status);
    return HRESULT(call.graphics.x);
}
g::Call Command(f::Op op,uint64_t id=0){g::Call call;call.draw.op=static_cast<g::Op>(op);call.draw.dc=id;return call;}
struct GraphicsObject {
    DWORD thread=GetCurrentThreadId();uint32_t family=0;uint64_t id=0;mutable HRESULT error=S_OK;mutable bool poisoned=false;HRESULT* domain=nullptr;
    virtual ~GraphicsObject()=default;
    static void operator delete(void* memory) noexcept {HeapFree(GetProcessHeap(),0,memory);}
    virtual IUnknown* Interface()=0;
    virtual bool Accept(REFIID) const=0;
    HRESULT Check() const{return thread==GetCurrentThreadId()?S_OK:RPC_E_WRONG_THREAD;}
    HRESULT Unsupported(uint32_t method,bool deferred=false) const {if(deferred){error=E_NOTIMPL;poisoned=true;if(domain)*domain=DXGI_ERROR_UNSUPPORTED;}auto call=Command(f::Op::Unsupported);call.draw.flags=family;call.draw.mode=method;Send(call);return E_NOTIMPL;}
};
GraphicsObject* objects[128]{};volatile LONG objectLock=0;
struct Lock {Lock(){while(_InterlockedCompareExchange(&objectLock,1,0))_mm_pause();}~Lock(){_InterlockedExchange(&objectLock,0);}};
GraphicsObject* Object(const void* address){Lock lock;for(auto* p:objects)if(p&&p->Interface()==address)return p;return nullptr;}
bool Register(GraphicsObject* object){Lock lock;for(auto& slot:objects)if(!slot){slot=object;return true;}return false;}
void Remove(GraphicsObject* object){Lock lock;for(auto& slot:objects)if(slot==object){slot=nullptr;break;}}
template<class TInterface> class GraphicsUnknown:public TInterface,public GraphicsObject {
    volatile LONG refs=1;
public:
    explicit GraphicsUnknown(uint32_t value){family=value;}
    virtual ~GraphicsUnknown(){if(id){auto call=Command(f::Op::Free,id);Send(call);}Remove(this);}
    static void* operator new(size_t,void* memory) noexcept{return memory;}
    static void operator delete(void* memory) noexcept {HeapFree(GetProcessHeap(),0,memory);}
    IUnknown* Interface() override {return static_cast<TInterface*>(this);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;const auto hr=Check();if(FAILED(hr))return hr;if(!Same(iid,__uuidof(IUnknown))&&!Accept(iid))return E_NOINTERFACE;*out=static_cast<TInterface*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(_InterlockedIncrement(&refs));}
    ULONG References() const {return ULONG(refs);}
    ULONG STDMETHODCALLTYPE Release() override {if(FAILED(Check())){Unsupported(2);return ULONG(refs);}const auto count=_InterlockedDecrement(&refs);if(!count)delete this;return ULONG(count);}
};
template<class T>T* Make(){void* memory=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(T));if(!memory)return nullptr;auto* value=new(memory)T();if(!Register(value)){delete value;return nullptr;}return value;}
#include "SoftwareGraphicsStubs.inl"
template<class T> T* Owned(void* address){auto* object=Object(address);if(!object||FAILED(object->Check()))return nullptr;void* out=nullptr;if(FAILED(object->Interface()->QueryInterface(__uuidof(T),&out)))return nullptr;static_cast<T*>(out)->Release();return static_cast<T*>(out);}
struct Keep {
    IUnknown* value=nullptr;
    ~Keep(){if(value)value->Release();}
    void Set(IUnknown* object){if(object)object->AddRef();if(value)value->Release();value=object;}
};
HRESULT New(f::Op op,GraphicsObject& object,g::Call& call){call.draw.op=static_cast<g::Op>(op);const auto hr=Send(call);if(SUCCEEDED(hr))object.id=call.graphics.value;return hr;}
#include "SoftwareGraphics2D.inl"
#include "SoftwareGraphicsWic.inl"
#include "SoftwareGraphicsD3d.inl"
#include "SoftwareGraphicsDxgi.inl"
}
extern "C" HRESULT WINAPI GfxD2D1CreateFactory(D2D1_FACTORY_TYPE type,REFIID iid,const D2D1_FACTORY_OPTIONS* options,void** out){
    if(!out)return E_POINTER;*out=nullptr;if(type!=D2D1_FACTORY_TYPE_SINGLE_THREADED||options&&options->debugLevel!=D2D1_DEBUG_LEVEL_NONE)return E_INVALIDARG;
    auto* factory=Make<GraphicsFactory>();if(!factory)return E_OUTOFMEMORY;const auto hr=factory->QueryInterface(iid,out);factory->Release();return hr;
}
extern "C" HRESULT WINAPI GfxDWriteCreateFactory(DWRITE_FACTORY_TYPE type,REFIID iid,IUnknown** out){
    if(!out)return E_POINTER;*out=nullptr;if(type!=DWRITE_FACTORY_TYPE_SHARED&&type!=DWRITE_FACTORY_TYPE_ISOLATED)return E_INVALIDARG;
    auto* factory=Make<WriteFactory>();if(!factory)return E_OUTOFMEMORY;const auto hr=factory->QueryInterface(iid,reinterpret_cast<void**>(out));factory->Release();return hr;
}
extern "C" BOOL WINAPI SoftwareGraphicsEntry(HINSTANCE,DWORD,LPVOID){return TRUE;}
extern "C" HRESULT WINAPI GfxDllGetClassObject(REFCLSID cls,REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(!Same(cls,CLSID_WICImagingFactory)&&!Same(cls,CLSID_WICImagingFactory2))return CLASS_E_CLASSNOTAVAILABLE;auto* factory=Make<WicClassFactory>();if(!factory)return E_OUTOFMEMORY;const auto hr=factory->QueryInterface(iid,out);factory->Release();return hr;}
extern "C" HRESULT WINAPI GfxDllCanUnloadNow(){Lock lock;if(graphicsLocks)return S_FALSE;for(auto* object:objects)if(object)return S_FALSE;return S_OK;}
extern "C" HRESULT WINAPI GfxCreateDXGIFactory(REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;auto* factory=Make<DxgiFactory>();if(!factory)return E_OUTOFMEMORY;const auto hr=factory->QueryInterface(iid,out);factory->Release();return hr;}
extern "C" HRESULT WINAPI GfxCreateDXGIFactory2(UINT flags,REFIID iid,void** out){if(flags){if(out)*out=nullptr;return DXGI_ERROR_UNSUPPORTED;}return GfxCreateDXGIFactory(iid,out);}
extern "C" HRESULT WINAPI GfxD3D11CreateDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** out,D3D_FEATURE_LEVEL* chosen,ID3D11DeviceContext** context){return CreateDevice(adapter,driver,software,flags,levels,count,sdk,out,chosen,context);}
extern "C" HRESULT WINAPI GfxD3D11CreateDeviceAndSwapChain(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driver,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,const DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** swap,ID3D11Device** out,D3D_FEATURE_LEVEL* chosen,ID3D11DeviceContext** context){
    if(swap)*swap=nullptr;if(out)*out=nullptr;if(context)*context=nullptr;if(chosen)*chosen=D3D_FEATURE_LEVEL(0);if(!desc||!swap)return E_INVALIDARG;
    ID3D11Device* device=nullptr;ID3D11DeviceContext* immediate=nullptr;D3D_FEATURE_LEVEL feature{};
    auto hr=CreateDevice(adapter,driver,software,flags,levels,count,sdk,&device,&feature,context?&immediate:nullptr);if(FAILED(hr))return hr;
    auto* factory=Make<DxgiFactory>();if(!factory)hr=E_OUTOFMEMORY;else {auto copy=*desc;hr=factory->CreateSwapChain(device,&copy,swap);factory->Release();}
    if(FAILED(hr)){if(immediate)immediate->Release();device->Release();return hr;}if(context)*context=immediate;if(chosen)*chosen=feature;if(out)*out=device;else device->Release();return S_OK;
}
extern "C" IDirect3D9* WINAPI GfxDirect3DCreate9(UINT){auto call=Command(f::Op::Unsupported);call.draw.flags=23;Send(call);SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
extern "C" HRESULT WINAPI GfxDirect3DCreate9Ex(UINT,IDirect3D9Ex** out){if(!out)return E_POINTER;*out=nullptr;GfxDirect3DCreate9(0);return D3DERR_NOTAVAILABLE;}
