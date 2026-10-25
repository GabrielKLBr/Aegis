#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <new>
#include <intrin.h>
#include "guardian/Component.h"
namespace cmp=guardian::component;
extern "C" DWORD GuardianComponentCall(cmp::Request*) noexcept;
extern "C" void* WINAPI CmpCoTaskMemAlloc(SIZE_T);
extern "C" void WINAPI CmpCoTaskMemFree(void*);
extern "C" BSTR WINAPI CmpSysAllocStringLen(const OLECHAR*,UINT);
extern "C" void WINAPI CmpSysFreeString(BSTR);
extern "C" UINT WINAPI CmpSysStringLen(BSTR);
extern "C" void WINAPI CmpVariantInit(VARIANTARG*);
extern "C" HRESULT WINAPI CmpVariantClear(VARIANTARG*);
extern "C" HRESULT WINAPI CmpVariantCopy(VARIANTARG*,const VARIANTARG*);
extern "C" HRESULT WINAPI CmpSafeArrayDestroy(SAFEARRAY*);
extern "C" HRESULT WINAPI CmpSafeArrayCopy(SAFEARRAY*,SAFEARRAY**);
namespace {
void Copy(void* d,const void* s,SIZE_T n){for(SIZE_T i=0;i<n;++i)static_cast<BYTE*>(d)[i]=static_cast<const BYTE*>(s)[i];}
void Zero(void* p,SIZE_T n){for(SIZE_T i=0;i<n;++i)static_cast<BYTE*>(p)[i]=0;}
bool Equal(const void* a,const void* b,SIZE_T n){for(SIZE_T i=0;i<n;++i)if(static_cast<const BYTE*>(a)[i]!=static_cast<const BYTE*>(b)[i])return false;return true;}
bool Guid(REFGUID a,REFGUID b){return Equal(&a,&b,sizeof(GUID));}
UINT Length(const wchar_t* s,UINT cap=32768){if(!s)return 0;UINT n=0;while(n<cap&&s[n])++n;return n;}
bool Text(const wchar_t* a,const wchar_t* b){if(!a||!b)return a==b;for(UINT i=0;i<32768;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
template<class T> T* Make(){auto* p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(T));return p?new(p)T():nullptr;}
template<class T> void Free(T* p){if(p){p->~T();HeapFree(GetProcessHeap(),0,p);}}
volatile LONG mutex=0;UINT mutexDepth=0;
struct Guard {
    Guard(){const auto tid=LONG(GetCurrentThreadId());while(_InterlockedCompareExchange(&mutex,tid,0)!=0&&mutex!=tid)SwitchToThread();++mutexDepth;}
    ~Guard(){if(!--mutexDepth)_InterlockedExchange(&mutex,0);}
};
}
