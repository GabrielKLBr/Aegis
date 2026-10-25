#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <propsys.h>
#include <propvarutil.h>
#include <new>
#include <intrin.h>
extern "C" void* WINAPI CmpCoTaskMemAlloc(SIZE_T);
extern "C" void WINAPI CmpCoTaskMemFree(void*);
namespace {
void Copy(void* d,const void* s,SIZE_T n){for(SIZE_T i=0;i<n;++i)static_cast<BYTE*>(d)[i]=static_cast<const BYTE*>(s)[i];}
void Zero(void* d,SIZE_T n){for(SIZE_T i=0;i<n;++i)static_cast<BYTE*>(d)[i]=0;}
bool Key(REFPROPERTYKEY a,REFPROPERTYKEY b){return a.pid==b.pid&&IsEqualGUID(a.fmtid,b.fmtid);}
bool Scalar(VARTYPE t){return t==VT_EMPTY||t==VT_I2||t==VT_I4||t==VT_UI4||t==VT_I8||t==VT_UI8||t==VT_BOOL||t==VT_R8||t==VT_FILETIME;}
}
extern "C" HRESULT WINAPI CmpPropVariantClear(PROPVARIANT* v){if(!v)return E_INVALIDARG;if(v->vt==VT_LPWSTR)CmpCoTaskMemFree(v->pwszVal);else if(v->vt==VT_BLOB)CmpCoTaskMemFree(v->blob.pBlobData);else if(!Scalar(v->vt))return DISP_E_BADVARTYPE;Zero(v,sizeof(*v));return S_OK;}
extern "C" HRESULT WINAPI CmpPropVariantCopy(PROPVARIANT* out,const PROPVARIANT* in){if(!out||!in)return E_INVALIDARG;if(out==in)return S_OK;PROPVARIANT value=*in;if(in->vt==VT_LPWSTR){if(!in->pwszVal)return E_INVALIDARG;UINT n=0;while(n<255&&in->pwszVal[n])++n;if(in->pwszVal[n])return E_INVALIDARG;value.pwszVal=static_cast<LPWSTR>(CmpCoTaskMemAlloc((n+1)*2));if(!value.pwszVal)return E_OUTOFMEMORY;Copy(value.pwszVal,in->pwszVal,(n+1)*2);}else if(in->vt==VT_BLOB){if(in->blob.cbSize>512||(in->blob.cbSize&&!in->blob.pBlobData))return E_INVALIDARG;value.blob.pBlobData=static_cast<BYTE*>(CmpCoTaskMemAlloc(in->blob.cbSize));if(!value.blob.pBlobData)return E_OUTOFMEMORY;Copy(value.blob.pBlobData,in->blob.pBlobData,in->blob.cbSize);}else if(!Scalar(in->vt))return DISP_E_BADVARTYPE;const auto hr=CmpPropVariantClear(out);if(FAILED(hr)){CmpPropVariantClear(&value);return hr;}*out=value;return S_OK;}
namespace {
class Store final:public IPropertyStore {
public:
    LONG refs=1;volatile LONG mutex=0;DWORD count=0;struct Guard {Store& store;explicit Guard(Store& s):store(s){while(_InterlockedCompareExchange(&store.mutex,1,0))SwitchToThread();}~Guard(){_InterlockedExchange(&store.mutex,0);}};PROPERTYKEY keys[16]{};PROPVARIANT values[16]{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(!IsEqualGUID(iid,__uuidof(IUnknown))&&!IsEqualGUID(iid,__uuidof(IPropertyStore)))return E_NOINTERFACE;*out=static_cast<IPropertyStore*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ULONG(_InterlockedIncrement(&refs));}
    ULONG STDMETHODCALLTYPE Release() override{const auto n=_InterlockedDecrement(&refs);if(!n){for(auto& v:values)CmpPropVariantClear(&v);this->~Store();HeapFree(GetProcessHeap(),0,this);}return ULONG(n);}
    HRESULT STDMETHODCALLTYPE GetCount(DWORD* n) override {Guard lock(*this);if(!n)return E_POINTER;*n=count;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAt(DWORD index,PROPERTYKEY* key) override {Guard lock(*this);if(!key)return E_POINTER;if(index>=count)return E_INVALIDARG;*key=keys[index];return S_OK;}
    HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY key,PROPVARIANT* out) override {Guard lock(*this);if(!out)return E_POINTER;Zero(out,sizeof(*out));for(DWORD i=0;i<count;++i)if(Key(keys[i],key))return CmpPropVariantCopy(out,&values[i]);return S_OK;}
    HRESULT STDMETHODCALLTYPE SetValue(REFPROPERTYKEY key,REFPROPVARIANT value) override {Guard lock(*this);DWORD at=0;for(;at<count;++at)if(Key(keys[at],key))break;if(at==16)return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);const auto hr=CmpPropVariantCopy(&values[at],&value);if(SUCCEEDED(hr)&&at==count){keys[at]=key;++count;}return hr;}
    HRESULT STDMETHODCALLTYPE Commit() override {return S_OK;}
};
}
extern "C" HRESULT WINAPI CmpPSCreateMemoryPropertyStore(REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;auto* bytes=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Store));if(!bytes)return E_OUTOFMEMORY;auto* store=new(bytes)Store();const auto hr=store->QueryInterface(iid,out);store->Release();return hr;}
extern "C" HRESULT WINAPI CmpInitPropVariantFromString(PCWSTR text,PROPVARIANT* out){if(!out||!text)return E_INVALIDARG;PROPVARIANT input{};input.vt=VT_LPWSTR;input.pwszVal=const_cast<LPWSTR>(text);Zero(out,sizeof(*out));return CmpPropVariantCopy(out,&input);}
extern "C" BOOL ComponentPropertiesEntry(void*,DWORD,void*){return TRUE;}
