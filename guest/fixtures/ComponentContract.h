#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <new>
inline constexpr CLSID ComponentInproc{0x6a140801,0x1480,0x4148,{0x81,0x48,0,0,0,0,0,1}};
inline constexpr CLSID ComponentLocal{0x6a140802,0x1480,0x4148,{0x81,0x48,0,0,0,0,0,2}};
inline constexpr CLSID ComponentMissing{0x6a140803,0x1480,0x4148,{0x81,0x48,0,0,0,0,0,3}};
inline bool ComponentText(const wchar_t* a,const wchar_t* b){for(UINT i=0;i<128;++i){auto x=a[i],y=b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;if(!x)return true;}return false;}
inline volatile LONG componentObjects=0,componentLocks=0;
inline HANDLE componentStop=nullptr;
class ComponentObject final:public IDispatch {
    LONG refs_=1;
public:
    ComponentObject(){InterlockedIncrement(&componentObjects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(!IsEqualGUID(iid,__uuidof(IUnknown))&&!IsEqualGUID(iid,__uuidof(IDispatch)))return E_NOINTERFACE;*out=static_cast<IDispatch*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(InterlockedIncrement(&refs_));}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=InterlockedDecrement(&refs_);if(!n){InterlockedDecrement(&componentObjects);this->~ComponentObject();HeapFree(GetProcessHeap(),0,this);}return ULONG(n);}
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out) override {if(!out)return E_POINTER;*out=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out) override {if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid,LPOLESTR* names,UINT count,LCID,DISPID* out) override {if(!IsEqualGUID(iid,GUID_NULL))return DISP_E_UNKNOWNINTERFACE;if(!names||!out||count!=1)return E_INVALIDARG;const wchar_t* methods[]{L"Add",L"Echo",L"Pid",L"Tid",L"Quit",L"Fail",L"Crash",L"Slow"};for(UINT i=0;i<8;++i)if(ComponentText(names[0],methods[i])){*out=LONG(i+1);return S_OK;}*out=DISPID_UNKNOWN;return DISP_E_UNKNOWNNAME;}
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID iid,LCID,WORD flags,DISPPARAMS* p,VARIANT* out,EXCEPINFO*,UINT* bad) override {if(out)VariantInit(out);if(bad)*bad=0;if(!IsEqualGUID(iid,GUID_NULL))return DISP_E_UNKNOWNINTERFACE;if(!p||flags!=DISPATCH_METHOD)return E_INVALIDARG;if(id==1){if(p->cArgs!=2)return DISP_E_BADPARAMCOUNT;if(p->rgvarg[0].vt!=VT_I4||p->rgvarg[1].vt!=VT_I4)return DISP_E_TYPEMISMATCH;const int64_t sum=int64_t(p->rgvarg[0].lVal)+p->rgvarg[1].lVal;if(sum<LONG_MIN||sum>LONG_MAX)return DISP_E_OVERFLOW;if(out){out->vt=VT_I4;out->lVal=LONG(sum);}return S_OK;}if(id==2)return p->cArgs==1?(out?VariantCopy(out,p->rgvarg):S_OK):DISP_E_BADPARAMCOUNT;if(p->cArgs)return DISP_E_BADPARAMCOUNT;if(id==6)return DISP_E_TYPEMISMATCH;if(id==7)ExitProcess(23);if(id==8)Sleep(4200);if(id==5&&componentStop)SetEvent(componentStop);if(id<3||id>8)return DISP_E_MEMBERNOTFOUND;if(out){out->vt=VT_I4;out->lVal=LONG(id==3?GetCurrentProcessId():id==4?GetCurrentThreadId():1);}return S_OK;}
};
class ComponentFactory final:public IClassFactory {
    LONG refs_=1;
public:
    ComponentFactory(){InterlockedIncrement(&componentObjects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(!IsEqualGUID(iid,__uuidof(IUnknown))&&!IsEqualGUID(iid,__uuidof(IClassFactory)))return E_NOINTERFACE;*out=static_cast<IClassFactory*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(InterlockedIncrement(&refs_));}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=InterlockedDecrement(&refs_);if(!n){InterlockedDecrement(&componentObjects);this->~ComponentFactory();HeapFree(GetProcessHeap(),0,this);}return ULONG(n);}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;auto* bytes=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(ComponentObject));if(!bytes)return E_OUTOFMEMORY;auto* value=new(bytes)ComponentObject();const auto hr=value->QueryInterface(iid,out);value->Release();return hr;}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {if(lock)InterlockedIncrement(&componentLocks);else if(componentLocks)InterlockedDecrement(&componentLocks);else return E_UNEXPECTED;return S_OK;}
};
inline ComponentFactory* ComponentMakeFactory(){auto* bytes=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(ComponentFactory));return bytes?new(bytes)ComponentFactory():nullptr;}
