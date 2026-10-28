#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <intrin.h>
#include <new>
#include "guardian/Audio.h"
namespace a=guardian::audio;namespace g=guardian::nativegdi;
extern "C" __declspec(dllimport) DWORD GuardianNativeGdiCall(g::Call*) noexcept;
extern "C" int __cdecl _purecall(){__fastfail(7);for(;;)_mm_pause();}
namespace {
bool Same(REFGUID x,REFGUID y){return InlineIsEqualGUID(x,y)!=0;}
void Copy(void* to,const void* from,size_t n){auto* d=static_cast<BYTE*>(to);const auto* s=static_cast<const BYTE*>(from);for(size_t i=0;i<n;++i)d[i]=s[i];}
g::Call C(a::Op op,uint64_t id=0){g::Call c;c.draw.op=static_cast<g::Op>(op);c.draw.dc=id;return c;}
HRESULT Send(g::Call& c){const auto error=GuardianNativeGdiCall(&c);return error||c.graphics.status?HRESULT_FROM_WIN32(error?error:c.graphics.status):HRESULT(c.graphics.x);}
bool Supported(const WAVEFORMATEX* f){return f&&f->wFormatTag==WAVE_FORMAT_PCM&&a::Format(f->nSamplesPerSec,f->nChannels,f->wBitsPerSample)&&f->nBlockAlign==f->nChannels*2&&f->nAvgBytesPerSec==f->nBlockAlign*f->nSamplesPerSec&&!f->cbSize;}
template<class T>class Unknown:public T {
    volatile LONG refs=1;
public:
    DWORD tid=GetCurrentThreadId();
    virtual ~Unknown()=default;
    static void* operator new(size_t n){return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n);}
    static void operator delete(void* p){HeapFree(GetProcessHeap(),0,p);}
    HRESULT Check() const{return GetCurrentThreadId()==tid?S_OK:RPC_E_WRONG_THREAD;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(!Same(iid,__uuidof(IUnknown))&&!Same(iid,__uuidof(T)))return E_NOINTERFACE;*out=static_cast<T*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ULONG(_InterlockedIncrement(&refs));}
    ULONG STDMETHODCALLTYPE Release() override{const auto count=_InterlockedDecrement(&refs);if(!count)delete this;return ULONG(count);}
};
class Client;
class Render final:public Unknown<IAudioRenderClient> {
public:Client* parent;explicit Render(Client*);~Render();
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT32,BYTE**) override;
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32,DWORD) override;
};
class Clock final:public Unknown<IAudioClock> {
public:Client* parent;explicit Clock(Client*);~Clock();
    HRESULT STDMETHODCALLTYPE GetFrequency(UINT64*) override;
    HRESULT STDMETHODCALLTYPE GetPosition(UINT64*,UINT64*) override;
    HRESULT STDMETHODCALLTYPE GetCharacteristics(DWORD* out) override{if(!out)return E_POINTER;if(FAILED(Check()))return Check();*out=AUDIOCLOCK_CHARACTERISTIC_FIXED_FREQ;return S_OK;}
};
class Client final:public Unknown<IAudioClient> {
public:uint64_t id=0;WAVEFORMATEX format{};UINT32 capacity=0,reserved=0;BYTE* buffer=nullptr;bool running=false;
    ~Client(){if(id){auto c=C(a::Op::Close,id);Send(c);}if(buffer)HeapFree(GetProcessHeap(),0,buffer);}
    HRESULT Ready() const{auto hr=Check();return FAILED(hr)?hr:id?S_OK:AUDCLNT_E_NOT_INITIALIZED;}
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE mode,DWORD flags,REFERENCE_TIME duration,REFERENCE_TIME periodicity,const WAVEFORMATEX* f,LPCGUID session) override{
        if(FAILED(Check()))return Check();if(id)return AUDCLNT_E_ALREADY_INITIALIZED;
        if(mode!=AUDCLNT_SHAREMODE_SHARED||flags&~(AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY)||periodicity||session)return E_ACCESSDENIED;
        if(!Supported(f))return AUDCLNT_E_UNSUPPORTED_FORMAT;if(duration<200000||duration>5000000)return E_INVALIDARG;
        auto c=C(a::Op::Open);c.draw.flags=1;c.draw.args[0]=int32_t(f->nSamplesPerSec);c.draw.args[1]=f->nChannels;c.draw.args[2]=f->wBitsPerSample;c.draw.args[3]=int32_t(duration/10000);
        const auto hr=Send(c);if(SUCCEEDED(hr)){id=c.graphics.value;format=*f;capacity=UINT32(c.graphics.rect.right);}return hr;
    }
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32* out) override{if(!out)return E_POINTER;*out=0;auto hr=Ready();if(FAILED(hr))return hr;*out=capacity;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME* out) override{if(!out)return E_POINTER;auto hr=Ready();if(FAILED(hr))return hr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32* out) override{if(!out)return E_POINTER;*out=0;auto hr=Ready();if(FAILED(hr))return hr;auto c=C(a::Op::Position,id);hr=Send(c);if(SUCCEEDED(hr))*out=UINT32(c.graphics.rect.bottom);return hr;}
    HRESULT STDMETHODCALLTYPE IsFormatSupported(AUDCLNT_SHAREMODE mode,const WAVEFORMATEX* f,WAVEFORMATEX** closest) override{if(closest)*closest=nullptr;if(FAILED(Check()))return Check();if(mode!=AUDCLNT_SHAREMODE_SHARED)return E_ACCESSDENIED;return Supported(f)?S_OK:AUDCLNT_E_UNSUPPORTED_FORMAT;}
    HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX** out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();auto* f=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));if(!f)return E_OUTOFMEMORY;WAVEFORMATEX value{};value.wFormatTag=WAVE_FORMAT_PCM;value.nChannels=2;value.nSamplesPerSec=48000;value.wBitsPerSample=16;value.nBlockAlign=4;value.nAvgBytesPerSec=192000;*f=value;*out=f;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME*,REFERENCE_TIME*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Start() override{auto hr=Ready();if(FAILED(hr))return hr;auto c=C(a::Op::Start,id);hr=Send(c);if(SUCCEEDED(hr))running=true;return hr;}
    HRESULT STDMETHODCALLTYPE Stop() override{auto hr=Ready();if(FAILED(hr))return hr;auto c=C(a::Op::Pause,id);hr=Send(c);if(SUCCEEDED(hr))running=false;return hr;}
    HRESULT STDMETHODCALLTYPE Reset() override{auto hr=Ready();if(FAILED(hr))return hr;if(buffer)return AUDCLNT_E_BUFFER_OPERATION_PENDING;if(running)return AUDCLNT_E_NOT_STOPPED;auto c=C(a::Op::Reset,id);return Send(c);}
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetService(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;auto hr=Ready();if(FAILED(hr))return hr;if(Same(iid,__uuidof(IAudioRenderClient))){auto* r=new Render(this);if(!r)return E_OUTOFMEMORY;*out=static_cast<IAudioRenderClient*>(r);return S_OK;}if(Same(iid,__uuidof(IAudioClock))){auto* r=new Clock(this);if(!r)return E_OUTOFMEMORY;*out=static_cast<IAudioClock*>(r);return S_OK;}return E_NOINTERFACE;}
};
Render::Render(Client* p):parent(p){p->AddRef();}Render::~Render(){parent->Release();}
HRESULT Render::GetBuffer(UINT32 frames,BYTE** out){if(!out)return E_POINTER;*out=nullptr;auto hr=parent->Ready();if(FAILED(hr))return hr;if(parent->buffer)return AUDCLNT_E_OUT_OF_ORDER;if(!frames)return S_OK;UINT32 padding=0;hr=parent->GetCurrentPadding(&padding);if(FAILED(hr))return hr;if(frames>parent->capacity-padding||frames>a::MaxBuffer/parent->format.nBlockAlign)return AUDCLNT_E_BUFFER_TOO_LARGE;parent->buffer=static_cast<BYTE*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,frames*parent->format.nBlockAlign));if(!parent->buffer)return E_OUTOFMEMORY;parent->reserved=frames;*out=parent->buffer;return S_OK;}
HRESULT Render::ReleaseBuffer(UINT32 frames,DWORD flags){
    auto hr=parent->Ready();if(FAILED(hr))return hr;if(!parent->buffer)return frames?AUDCLNT_E_OUT_OF_ORDER:S_OK;if(frames>parent->reserved||flags&~AUDCLNT_BUFFERFLAGS_SILENT)return E_INVALIDARG;
    if(frames){auto c=C(a::Op::Allocate,parent->id);c.draw.count=frames*parent->format.nBlockAlign;hr=Send(c);if(FAILED(hr))return hr;const auto buffer=c.graphics.value;const auto length=frames*parent->format.nBlockAlign;
        for(UINT32 at=0;at<length;){c=C(a::Op::Upload,buffer);c.draw.offset=at;c.draw.count=length-at;if(c.draw.count>sizeof(c.draw.pixels))c.draw.count=sizeof(c.draw.pixels);if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT))Copy(c.draw.pixels,parent->buffer+at,c.draw.count);hr=Send(c);if(FAILED(hr))break;at+=c.draw.count;}
        if(SUCCEEDED(hr)){c=C(a::Op::Submit,parent->id);c.draw.object=buffer;hr=Send(c);}c=C(a::Op::Free,buffer);const auto cleanup=Send(c);if(SUCCEEDED(hr))hr=cleanup;
    }
    HeapFree(GetProcessHeap(),0,parent->buffer);parent->buffer=nullptr;parent->reserved=0;return hr;
}
Clock::Clock(Client* p):parent(p){p->AddRef();}Clock::~Clock(){parent->Release();}
HRESULT Clock::GetFrequency(UINT64* out){if(!out)return E_POINTER;auto hr=parent->Ready();if(FAILED(hr))return hr;auto c=C(a::Op::Position,parent->id);hr=Send(c);if(SUCCEEDED(hr))*out=c.graphics.selected[1];return hr;}
HRESULT Clock::GetPosition(UINT64* out,UINT64* qpc){if(!out)return E_POINTER;auto hr=parent->Ready();if(FAILED(hr))return hr;auto c=C(a::Op::Position,parent->id);hr=Send(c);if(SUCCEEDED(hr)){*out=c.graphics.value;if(qpc)*qpc=c.graphics.selected[0];}return hr;}
class Device final:public Unknown<IMMDevice> {
public:
    HRESULT STDMETHODCALLTYPE Activate(REFIID iid,DWORD context,PROPVARIANT* activation,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(context!=CLSCTX_INPROC_SERVER||activation)return E_INVALIDARG;if(!Same(iid,__uuidof(IAudioClient)))return E_NOINTERFACE;auto* c=new Client;if(!c)return E_OUTOFMEMORY;*out=static_cast<IAudioClient*>(c);return S_OK;}
    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD,IPropertyStore** out) override{if(out)*out=nullptr;return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR* out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();const wchar_t text[]=L"MiniNT.Render.Default";auto* value=static_cast<wchar_t*>(CoTaskMemAlloc(sizeof(text)));if(!value)return E_OUTOFMEMORY;Copy(value,text,sizeof(text));*out=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetState(DWORD* out) override{if(!out)return E_POINTER;if(FAILED(Check()))return Check();auto c=C(a::Op::Enumerate);const auto hr=Send(c);*out=SUCCEEDED(hr)?DEVICE_STATE_ACTIVE:DEVICE_STATE_NOTPRESENT;return hr;}
};
class Collection final:public Unknown<IMMDeviceCollection> {
public:UINT count=0;
    HRESULT STDMETHODCALLTYPE GetCount(UINT* out) override{if(!out)return E_POINTER;if(FAILED(Check()))return Check();*out=count;return S_OK;}
    HRESULT STDMETHODCALLTYPE Item(UINT index,IMMDevice** out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(index>=count)return E_INVALIDARG;*out=new Device;return *out?S_OK:E_OUTOFMEMORY;}
};
class Enumerator final:public Unknown<IMMDeviceEnumerator> {
public:
    HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow flow,DWORD mask,IMMDeviceCollection** out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(flow!=eRender)return E_ACCESSDENIED;if(!mask||mask&~DEVICE_STATEMASK_ALL)return E_INVALIDARG;auto c=C(a::Op::Enumerate);const auto hr=Send(c);if(FAILED(hr))return hr;auto* value=new Collection;if(!value)return E_OUTOFMEMORY;value->count=(mask&DEVICE_STATE_ACTIVE)?1:0;*out=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow flow,ERole role,IMMDevice** out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(flow!=eRender)return E_ACCESSDENIED;if(role!=eConsole&&role!=eMultimedia&&role!=eCommunications)return E_INVALIDARG;auto c=C(a::Op::Enumerate);auto hr=Send(c);if(FAILED(hr))return hr;*out=new Device;return *out?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR text,IMMDevice** out) override{if(!out)return E_POINTER;*out=nullptr;if(!text)return E_INVALIDARG;const wchar_t name[]=L"MiniNT.Render.Default";for(unsigned i=0;i<sizeof(name)/2;++i)if(text[i]!=name[i])return E_INVALIDARG;return GetDefaultAudioEndpoint(eRender,eMultimedia,out);}
    HRESULT STDMETHODCALLTYPE RegisterEndpointNotificationCallback(IMMNotificationClient*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnregisterEndpointNotificationCallback(IMMNotificationClient*) override{return E_NOTIMPL;}
};
class Factory final:public Unknown<IClassFactory> {
public:HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(Check()))return Check();if(outer)return CLASS_E_NOAGGREGATION;auto* e=new Enumerator;if(!e)return E_OUTOFMEMORY;const auto hr=e->QueryInterface(iid,out);e->Release();return hr;}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override{return E_NOTIMPL;}
};
}
extern "C" BOOL WINAPI AudioWasapiEntry(HINSTANCE,DWORD,LPVOID){return TRUE;}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID cls,REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(!Same(cls,__uuidof(MMDeviceEnumerator)))return CLASS_E_CLASSNOTAVAILABLE;auto* f=new Factory;if(!f)return E_OUTOFMEMORY;const auto hr=f->QueryInterface(iid,out);f->Release();return hr;}
// Keep the module resident: guest objects and audio state may outlive the factory.
extern "C" HRESULT WINAPI DllCanUnloadNow(){return S_FALSE;}
