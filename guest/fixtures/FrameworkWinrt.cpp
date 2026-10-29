#include "ComponentBase.h"
#include <roapi.h>
#include <winstring.h>
#include <windows.foundation.h>
#include <limits.h>
#include "guardian/FrameworkWinrt.h"
namespace wf=ABI::Windows::Foundation;
namespace fw=guardian::frameworks;
void operator delete(void* pointer,SIZE_T) noexcept {if(pointer)HeapFree(GetProcessHeap(),0,pointer);}
extern "C" HRESULT WINAPI CmpCoInitializeEx(void*,DWORD);
extern "C" void WINAPI CmpCoUninitialize();
extern "C" LONG NTAPI RtlGetVersion(OSVERSIONINFOW*);
extern "C" HRESULT WINAPI FwWindowsCreateString(PCNZWCH,UINT32,HSTRING*);
extern "C" HRESULT WINAPI FwWindowsDuplicateString(HSTRING,HSTRING*);
extern "C" HRESULT WINAPI FwWindowsDeleteString(HSTRING);
namespace {
struct String {HSTRING id=nullptr;const wchar_t* text=nullptr;UINT32 count=0,refs=0;};
struct StringReference {uint64_t magic;const wchar_t* text;UINT32 count,reserved;};
constexpr uint64_t ReferenceMagic=0x314652535447ull;
static_assert(sizeof(StringReference)<=sizeof(HSTRING_HEADER));
String strings[128]{};uint64_t nextString=1;
struct RoThread {DWORD tid=0,count=0;};RoThread roThreads[32]{};
bool Available(){OSVERSIONINFOW v{};v.dwOSVersionInfoSize=sizeof(v);return RtlGetVersion(&v)==0&&(v.dwMajorVersion>6||(v.dwMajorVersion==6&&v.dwMinorVersion>=2));}
RoThread* Thread(){for(auto& t:roThreads)if(t.tid==GetCurrentThreadId()&&t.count)return &t;return nullptr;}
String* Find(HSTRING id){for(auto& s:strings)if(s.id==id)return &s;return nullptr;}
bool View(HSTRING id,String& value){if(!id){value={};value.text=L"";return true;}if(const auto* s=Find(id)){value=*s;return true;}
    if(reinterpret_cast<uintptr_t>(id)&7)return false;StringReference reference{};Copy(&reference,id,sizeof(reference));
    if(reference.magic!=ReferenceMagic||reference.reserved||!reference.text||reference.count>4096)return false;
    value={id,reference.text,reference.count,1};return true;}
HRESULT NewString(const wchar_t* text,UINT32 n,HSTRING* out){
    if(!out)return E_POINTER;*out=nullptr;if(!Available())return E_NOTIMPL;
    if(n>4096||(!text&&n))return E_INVALIDARG;if(!n)return S_OK;
    Guard g;String* slot=nullptr;for(auto& s:strings)if(!s.id){slot=&s;break;}
    if(!slot||nextString>UINT64_MAX/256)return E_OUTOFMEMORY;
    auto* copy=static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(),0,(SIZE_T(n)+1)*2));if(!copy)return E_OUTOFMEMORY;Copy(copy,text,n*2);copy[n]=0;
    slot->id=reinterpret_cast<HSTRING>((nextString++<<8)|1);slot->text=copy;slot->count=n;slot->refs=1;*out=slot->id;return S_OK;
}
bool Class(HSTRING name){String s;constexpr wchar_t value[]=L"Windows.Foundation.PropertyValue";return View(name,s)&&s.count==_countof(value)-1&&Equal(s.text,value,(_countof(value)-1)*2);}
template<class T>HRESULT Empty(T* out){if(!out)return E_POINTER;*out={};return TYPE_E_TYPEMISMATCH;}
template<class T>HRESULT Array(UINT32* n,T** out){if(n)*n=0;if(out)*out=nullptr;return !n||!out?E_POINTER:TYPE_E_TYPEMISMATCH;}
class Property final:public wf::IPropertyValue,public fw::ReferenceInt32,public fw::ReferenceBoolean,public fw::ReferenceString {
    volatile LONG refs_=1;
public:
    wf::PropertyType type=wf::PropertyType_Empty;INT32 number=0;boolean booleanValue=0;HSTRING stringValue=nullptr;
    ~Property(){FwWindowsDeleteString(stringValue);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(Guid(iid,__uuidof(IUnknown))||Guid(iid,__uuidof(IInspectable))||Guid(iid,__uuidof(wf::IPropertyValue))||Guid(iid,__uuidof(IAgileObject)))*out=static_cast<wf::IPropertyValue*>(this);
        else if(type==wf::PropertyType_Int32&&Guid(iid,__uuidof(fw::ReferenceInt32)))*out=static_cast<fw::ReferenceInt32*>(this);
        else if(type==wf::PropertyType_Boolean&&Guid(iid,__uuidof(fw::ReferenceBoolean)))*out=static_cast<fw::ReferenceBoolean*>(this);
        else if(type==wf::PropertyType_String&&Guid(iid,__uuidof(fw::ReferenceString)))*out=static_cast<fw::ReferenceString*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(_InterlockedIncrement(&refs_));}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=_InterlockedDecrement(&refs_);if(!n)Free(this);return ULONG(n);}
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count,IID** out) override {if(count)*count=0;if(out)*out=nullptr;if(!count||!out)return E_POINTER;auto* ids=static_cast<IID*>(CmpCoTaskMemAlloc(2*sizeof(IID)));if(!ids)return E_OUTOFMEMORY;ids[0]=__uuidof(wf::IPropertyValue);ids[1]=type==wf::PropertyType_Int32?__uuidof(fw::ReferenceInt32):type==wf::PropertyType_Boolean?__uuidof(fw::ReferenceBoolean):__uuidof(fw::ReferenceString);*out=ids;*count=2;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* out) override {const auto* name=type==wf::PropertyType_Int32?L"Windows.Foundation.IReference`1<Int32>":type==wf::PropertyType_Boolean?L"Windows.Foundation.IReference`1<Boolean>":L"Windows.Foundation.IReference`1<String>";return FwWindowsCreateString(name,Length(name),out);}
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* out) override {if(!out)return E_POINTER;*out=BaseTrust;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Type(wf::PropertyType* out) override {if(!out)return E_POINTER;*out=type;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsNumericScalar(boolean* out) override {if(!out)return E_POINTER;*out=type==wf::PropertyType_Int32;return S_OK;}
    template<class T>HRESULT Numeric(T* out,int64_t low,uint64_t high){if(!out)return E_POINTER;*out={};if(type!=wf::PropertyType_Int32)return TYPE_E_TYPEMISMATCH;if(int64_t(number)<low||(number>=0&&uint64_t(number)>high))return DISP_E_OVERFLOW;*out=static_cast<T>(number);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetUInt8(BYTE* v) override {return Numeric(v,0,255);}
    HRESULT STDMETHODCALLTYPE GetInt16(INT16* v) override {return Numeric(v,-32768,32767);}
    HRESULT STDMETHODCALLTYPE GetUInt16(UINT16* v) override {return Numeric(v,0,65535);}
    HRESULT STDMETHODCALLTYPE GetInt32(INT32* v) override {return Numeric(v,INT32_MIN,INT32_MAX);}
    HRESULT STDMETHODCALLTYPE GetUInt32(UINT32* v) override {return Numeric(v,0,UINT32_MAX);}
    HRESULT STDMETHODCALLTYPE GetInt64(INT64* v) override {return Numeric(v,INT32_MIN,INT32_MAX);}
    HRESULT STDMETHODCALLTYPE GetUInt64(UINT64* v) override {return Numeric(v,0,UINT32_MAX);}
    HRESULT STDMETHODCALLTYPE GetSingle(FLOAT* v) override {return Numeric(v,INT32_MIN,INT32_MAX);}
    HRESULT STDMETHODCALLTYPE GetDouble(DOUBLE* v) override {return Numeric(v,INT32_MIN,INT32_MAX);}
    HRESULT STDMETHODCALLTYPE GetBoolean(boolean* v) override {if(!v)return E_POINTER;*v=0;if(type!=wf::PropertyType_Boolean)return TYPE_E_TYPEMISMATCH;*v=booleanValue;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetString(HSTRING* v) override {if(!v)return E_POINTER;*v=nullptr;return type==wf::PropertyType_String?FwWindowsDuplicateString(stringValue,v):TYPE_E_TYPEMISMATCH;}
    HRESULT STDMETHODCALLTYPE get_Value(INT32* value) override {return GetInt32(value);}
    HRESULT STDMETHODCALLTYPE get_Value(boolean* value) override {return GetBoolean(value);}
    HRESULT STDMETHODCALLTYPE get_Value(HSTRING* value) override {return GetString(value);}
#define FW_SCALAR(Name,T) HRESULT STDMETHODCALLTYPE Get##Name(T* v) override {return Empty(v);}
    FW_SCALAR(Char16,WCHAR) FW_SCALAR(Guid,GUID) FW_SCALAR(DateTime,wf::DateTime) FW_SCALAR(TimeSpan,wf::TimeSpan) FW_SCALAR(Point,wf::Point) FW_SCALAR(Size,wf::Size) FW_SCALAR(Rect,wf::Rect)
#undef FW_SCALAR
#define FW_ARRAY(Name,T) HRESULT STDMETHODCALLTYPE Get##Name##Array(UINT32* n,T** v) override {return Array(n,v);}
    FW_ARRAY(UInt8,BYTE) FW_ARRAY(Int16,INT16) FW_ARRAY(UInt16,UINT16) FW_ARRAY(Int32,INT32) FW_ARRAY(UInt32,UINT32) FW_ARRAY(Int64,INT64) FW_ARRAY(UInt64,UINT64) FW_ARRAY(Single,FLOAT) FW_ARRAY(Double,DOUBLE) FW_ARRAY(Char16,WCHAR) FW_ARRAY(Boolean,boolean) FW_ARRAY(String,HSTRING) FW_ARRAY(Inspectable,IInspectable*) FW_ARRAY(Guid,GUID) FW_ARRAY(DateTime,wf::DateTime) FW_ARRAY(TimeSpan,wf::TimeSpan) FW_ARRAY(Point,wf::Point) FW_ARRAY(Size,wf::Size) FW_ARRAY(Rect,wf::Rect)
#undef FW_ARRAY
};
class Factory final:public wf::IPropertyValueStatics,public IActivationFactory {
    volatile LONG refs_=1;
    HRESULT MakeProperty(wf::PropertyType type,INT32 number,boolean flag,HSTRING text,IInspectable** out){if(!out)return E_POINTER;*out=nullptr;auto* p=Make<Property>();if(!p)return E_OUTOFMEMORY;p->type=type;p->number=number;p->booleanValue=flag;if(type==wf::PropertyType_String){const auto hr=FwWindowsDuplicateString(text,&p->stringValue);if(FAILED(hr)){p->Release();return hr;}}*out=static_cast<wf::IPropertyValue*>(p);return S_OK;}
    HRESULT Unsupported(IInspectable** out){if(!out)return E_POINTER;*out=nullptr;return E_NOTIMPL;}
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(Guid(iid,__uuidof(IUnknown))||Guid(iid,__uuidof(IInspectable))||Guid(iid,__uuidof(wf::IPropertyValueStatics)))*out=static_cast<wf::IPropertyValueStatics*>(this);else if(Guid(iid,__uuidof(IActivationFactory)))*out=static_cast<IActivationFactory*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(_InterlockedIncrement(&refs_));}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=_InterlockedDecrement(&refs_);if(!n)Free(this);return ULONG(n);}
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count,IID** out) override {if(count)*count=0;if(out)*out=nullptr;if(!count||!out)return E_POINTER;auto* ids=static_cast<IID*>(CmpCoTaskMemAlloc(2*sizeof(IID)));if(!ids)return E_OUTOFMEMORY;ids[0]=__uuidof(wf::IPropertyValueStatics);ids[1]=__uuidof(IActivationFactory);*out=ids;*count=2;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* out) override {return FwWindowsCreateString(L"Windows.Foundation.PropertyValue",32,out);}
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* out) override {if(!out)return E_POINTER;*out=BaseTrust;return S_OK;}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override {return Unsupported(out);}
    HRESULT STDMETHODCALLTYPE CreateEmpty(IInspectable** out) override {if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateInt32(INT32 value,IInspectable** out) override {return MakeProperty(wf::PropertyType_Int32,value,0,nullptr,out);}
    HRESULT STDMETHODCALLTYPE CreateBoolean(boolean value,IInspectable** out) override {return MakeProperty(wf::PropertyType_Boolean,0,value!=0,nullptr,out);}
    HRESULT STDMETHODCALLTYPE CreateString(HSTRING value,IInspectable** out) override {return MakeProperty(wf::PropertyType_String,0,0,value,out);}
#define FW_CREATE(Name,T) HRESULT STDMETHODCALLTYPE Create##Name(T,IInspectable** out) override {return Unsupported(out);}
    FW_CREATE(UInt8,BYTE) FW_CREATE(Int16,INT16) FW_CREATE(UInt16,UINT16) FW_CREATE(UInt32,UINT32) FW_CREATE(Int64,INT64) FW_CREATE(UInt64,UINT64) FW_CREATE(Single,FLOAT) FW_CREATE(Double,DOUBLE) FW_CREATE(Char16,WCHAR) FW_CREATE(Inspectable,IInspectable*) FW_CREATE(Guid,GUID) FW_CREATE(DateTime,wf::DateTime) FW_CREATE(TimeSpan,wf::TimeSpan) FW_CREATE(Point,wf::Point) FW_CREATE(Size,wf::Size) FW_CREATE(Rect,wf::Rect)
#undef FW_CREATE
#define FW_CREATE_ARRAY(Name,T) HRESULT STDMETHODCALLTYPE Create##Name##Array(UINT32,T*,IInspectable** out) override {return Unsupported(out);}
    FW_CREATE_ARRAY(UInt8,BYTE) FW_CREATE_ARRAY(Int16,INT16) FW_CREATE_ARRAY(UInt16,UINT16) FW_CREATE_ARRAY(Int32,INT32) FW_CREATE_ARRAY(UInt32,UINT32) FW_CREATE_ARRAY(Int64,INT64) FW_CREATE_ARRAY(UInt64,UINT64) FW_CREATE_ARRAY(Single,FLOAT) FW_CREATE_ARRAY(Double,DOUBLE) FW_CREATE_ARRAY(Char16,WCHAR) FW_CREATE_ARRAY(Boolean,boolean) FW_CREATE_ARRAY(String,HSTRING) FW_CREATE_ARRAY(Inspectable,IInspectable*) FW_CREATE_ARRAY(Guid,GUID) FW_CREATE_ARRAY(DateTime,wf::DateTime) FW_CREATE_ARRAY(TimeSpan,wf::TimeSpan) FW_CREATE_ARRAY(Point,wf::Point) FW_CREATE_ARRAY(Size,wf::Size) FW_CREATE_ARRAY(Rect,wf::Rect)
#undef FW_CREATE_ARRAY
};
}
extern "C" HRESULT WINAPI FwWindowsCreateString(PCNZWCH text,UINT32 length,HSTRING* out){return NewString(text,length,out);}
extern "C" HRESULT WINAPI FwWindowsCreateStringReference(PCWSTR text,UINT32 length,HSTRING_HEADER* header,HSTRING* out){if(!out)return E_POINTER;*out=nullptr;if(!Available())return E_NOTIMPL;if(!header||(reinterpret_cast<uintptr_t>(header)&7)||length>4096||(!text&&length))return E_INVALIDARG;if(text&&text[length])return E_STRING_NOT_NULL_TERMINATED;Zero(header,sizeof(*header));if(!length)return S_OK;StringReference reference{ReferenceMagic,text,length,0};Copy(header,&reference,sizeof(reference));*out=reinterpret_cast<HSTRING>(header);return S_OK;}
extern "C" HRESULT WINAPI FwWindowsDuplicateString(HSTRING id,HSTRING* out){if(!out)return E_POINTER;*out=nullptr;if(!id)return S_OK;Guard g;auto* s=Find(id);if(!s){String reference;if(!View(id,reference))return E_INVALIDARG;return NewString(reference.text,reference.count,out);}if(s->refs==UINT32_MAX)return E_OUTOFMEMORY;++s->refs;*out=id;return S_OK;}
extern "C" HRESULT WINAPI FwWindowsDeleteString(HSTRING id){if(!id)return S_OK;Guard g;auto* s=Find(id);if(!s){String reference;return View(id,reference)?S_OK:E_INVALIDARG;}if(--s->refs)return S_OK;HeapFree(GetProcessHeap(),0,const_cast<wchar_t*>(s->text));*s={};return S_OK;}
extern "C" PCWSTR WINAPI FwWindowsGetStringRawBuffer(HSTRING id,UINT32* length){if(length)*length=0;Guard g;String s;if(!View(id,s))return L"";if(length)*length=s.count;return s.text;}
extern "C" UINT32 WINAPI FwWindowsGetStringLen(HSTRING id){UINT32 n=0;FwWindowsGetStringRawBuffer(id,&n);return n;}
extern "C" BOOL WINAPI FwWindowsIsStringEmpty(HSTRING id){return !FwWindowsGetStringLen(id);}
extern "C" HRESULT WINAPI FwWindowsStringHasEmbeddedNull(HSTRING id,BOOL* out){if(!out)return E_POINTER;*out=FALSE;Guard g;String s;if(!View(id,s))return E_INVALIDARG;for(UINT32 i=0;i<s.count;++i)if(!s.text[i])*out=TRUE;return S_OK;}
extern "C" HRESULT WINAPI FwWindowsCompareStringOrdinal(HSTRING a,HSTRING b,INT32* out){if(!out)return E_POINTER;*out=0;Guard g;String x,y;if(!View(a,x)||!View(b,y))return E_INVALIDARG;const auto nx=x.count,ny=y.count;for(UINT32 i=0;i<nx&&i<ny;++i)if(x.text[i]!=y.text[i]){*out=x.text[i]<y.text[i]?-1:1;return S_OK;}*out=nx<ny?-1:nx>ny?1:0;return S_OK;}
extern "C" HRESULT WINAPI FwRoInitialize(RO_INIT_TYPE mode){if(!Available())return E_NOTIMPL;if(mode!=RO_INIT_SINGLETHREADED&&mode!=RO_INIT_MULTITHREADED)return E_INVALIDARG;Guard g;auto* t=Thread();if(!t)for(auto& slot:roThreads)if(!slot.tid){t=&slot;break;}if(!t||t->count==UINT32_MAX)return E_OUTOFMEMORY;const auto hr=CmpCoInitializeEx(nullptr,mode==RO_INIT_SINGLETHREADED?COINIT_APARTMENTTHREADED:COINIT_MULTITHREADED);if(SUCCEEDED(hr)){t->tid=GetCurrentThreadId();++t->count;}return hr;}
extern "C" void WINAPI FwRoUninitialize(){Guard g;if(auto* t=Thread()){if(!--t->count)*t={};CmpCoUninitialize();}}
extern "C" HRESULT WINAPI FwRoGetActivationFactory(HSTRING name,REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(!Available())return E_NOTIMPL;Guard g;if(!Thread())return CO_E_NOTINITIALIZED;if(!Class(name))return REGDB_E_CLASSNOTREG;auto* f=Make<Factory>();if(!f)return E_OUTOFMEMORY;const auto hr=f->QueryInterface(iid,out);f->Release();return hr;}
extern "C" HRESULT WINAPI FwRoActivateInstance(HSTRING name,IInspectable** out){if(!out)return E_POINTER;*out=nullptr;IActivationFactory* f=nullptr;const auto hr=FwRoGetActivationFactory(name,__uuidof(IActivationFactory),reinterpret_cast<void**>(&f));if(FAILED(hr))return hr;const auto result=f->ActivateInstance(out);f->Release();return result;}
