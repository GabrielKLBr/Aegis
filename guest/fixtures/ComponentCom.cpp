#include "ComponentBase.h"
#include <limits.h>
#include <initializer_list>
extern "C" const int GuardianComponentAnchor=148;
extern "C" int _fltused=0;
const void* volatile GuardianComponentRelocation=&GuardianComponentAnchor;
#include "ComponentAutomation.inl"
namespace {
struct Apartment {DWORD tid=0,model=0,count=0;bool pumping=false;};Apartment apartments[32]{};
Apartment* Apt(){Guard lock;const auto tid=GetCurrentThreadId();for(auto& a:apartments)if(a.tid==tid&&a.count)return &a;return nullptr;}
struct RemoteObject {uint64_t id=0;DWORD client=0;IDispatch* value=nullptr;};
struct Server {uint64_t endpoint=0;DWORD tid=0;IUnknown* factory=nullptr;IDispatch* marshaled=nullptr;bool claimed=false;RemoteObject objects[16]{};};Server servers[cmp::Endpoints]{};uint64_t nextObject=1;
Server* ReserveServer(){Guard lock;for(auto& s:servers)if(!s.endpoint){s={};s.endpoint=UINT64_MAX;s.tid=GetCurrentThreadId();return &s;}return nullptr;}
UINT OwnedServers(Server* (&out)[cmp::Endpoints]){Guard lock;UINT count=0;for(auto& s:servers)if(s.endpoint&&s.endpoint!=UINT64_MAX&&s.tid==GetCurrentThreadId())out[count++]=&s;return count;}
struct Module {HMODULE h=nullptr;bool busy=false;};Module modules[12]{};
void Pump();void PumpRpc();
bool Thread(DWORD tid,DWORD model){const auto* a=Apt();return a&&(a->tid==tid||(!a->model&&!model));}
HRESULT Error(DWORD e){return e==1722||e==1726?RPC_E_DISCONNECTED:HRESULT_FROM_WIN32(e);}
HRESULT Transport(cmp::Request& r,DWORD timeout=3000){
    r.op=cmp::Op::Call;const auto e=GuardianComponentCall(&r);if(e)return Error(e);const auto id=r.call;const auto start=GetTickCount64();
    for(;;){cmp::Request result;result.op=cmp::Op::Result;result.call=id;const auto status=GuardianComponentCall(&result);if(!status){r=result;return r.status==1722||r.status==1726?RPC_E_DISCONNECTED:HRESULT(r.status);}if(status!=ERROR_IO_PENDING)return Error(status);if(GetTickCount64()-start>=timeout){cmp::Request cancel;cancel.op=cmp::Op::Cancel;cancel.call=id;GuardianComponentCall(&cancel);return RPC_E_TIMEOUT;}Pump();Sleep(1);}
}
struct Wire {
    BYTE* p;UINT at=0,cap;bool valid=true;
    void Put(const void* in,UINT n){if(n>cap-at){valid=false;return;}Copy(p+at,in,n);at+=n;}
    void Get(void* out,UINT n){if(n>cap-at){valid=false;return;}Copy(out,p+at,n);at+=n;}
    template<class T>void Put(T value){Put(&value,sizeof(value));}
    template<class T>T Get(){T v{};Get(&v,sizeof(v));return v;}
};
bool Encode(Wire& w,const VARIANT& v){w.Put<USHORT>(v.vt);switch(v.vt){case VT_EMPTY:case VT_NULL:break;case VT_I2:case VT_BOOL:w.Put<SHORT>(v.iVal);break;case VT_I4:case VT_UI4:case VT_ERROR:w.Put<LONG>(v.lVal);break;case VT_R8:w.Put<double>(v.dblVal);break;case VT_BSTR:{const auto n=CmpSysStringLen(v.bstrVal);if(n>128)return false;w.Put<UINT>(n);w.Put(v.bstrVal,n*2);break;}default:return false;}return w.valid;}
bool Decode(Wire& w,VARIANT& v){CmpVariantInit(&v);v.vt=w.Get<USHORT>();switch(v.vt){case VT_EMPTY:case VT_NULL:break;case VT_I2:case VT_BOOL:v.iVal=w.Get<SHORT>();break;case VT_I4:case VT_UI4:case VT_ERROR:v.lVal=w.Get<LONG>();break;case VT_R8:v.dblVal=w.Get<double>();break;case VT_BSTR:{const auto n=w.Get<UINT>();if(n>128||n*2>w.cap-w.at)return false;v.bstrVal=CmpSysAllocStringLen(reinterpret_cast<const wchar_t*>(w.p+w.at),n);w.at+=n*2;if(!v.bstrVal)return false;break;}default:CmpVariantInit(&v);return false;}return w.valid;}
void CloseServer(Server& s){Server old;{Guard lock;old=s;s={};}cmp::Request r;r.op=cmp::Op::Unregister;r.endpoint=old.endpoint;GuardianComponentCall(&r);for(auto& o:old.objects)if(o.value)o.value->Release();if(old.factory)old.factory->Release();if(old.marshaled)old.marshaled->Release();}

void Pump(){
    PumpRpc();
    auto* apt=Apt();if(!apt||apt->pumping)return;apt->pumping=true;
    Server* owned[cmp::Endpoints]{};const auto ownedCount=OwnedServers(owned);for(UINT serverIndex=0;serverIndex<ownedCount;++serverIndex){auto& s=*owned[serverIndex];if(!s.endpoint)continue;cmp::Request r;r.op=cmp::Op::Receive;r.endpoint=s.endpoint;if(GuardianComponentCall(&r))continue;
        Wire in{r.data,0,r.length};HRESULT hr=E_INVALIDARG;const auto object=in.Get<uint64_t>();
        if(r.method==5&&r.length==12&&s.factory){const auto locked=in.Get<DWORD>();IClassFactory* factory=nullptr;hr=locked>1?E_INVALIDARG:s.factory->QueryInterface(__uuidof(IClassFactory),reinterpret_cast<void**>(&factory));if(SUCCEEDED(hr)){hr=factory->LockServer(BOOL(locked));factory->Release();}r.length=0;}
        else if(r.method==1&&r.length==8){RemoteObject* target=nullptr;for(auto& o:s.objects)if(!o.id){target=&o;break;}if(!target)hr=E_OUTOFMEMORY;else {IDispatch* value=nullptr;if(s.marshaled){if(s.claimed)hr=RPC_E_DISCONNECTED;else {value=s.marshaled;value->AddRef();s.claimed=true;hr=S_OK;}}else {IClassFactory* factory=nullptr;hr=s.factory->QueryInterface(__uuidof(IClassFactory),reinterpret_cast<void**>(&factory));if(SUCCEEDED(hr)){hr=factory->CreateInstance(nullptr,__uuidof(IDispatch),reinterpret_cast<void**>(&value));factory->Release();}}if(SUCCEEDED(hr)&&value){target->id=nextObject++;target->client=r.pid;target->value=value;Wire out{r.data,0,cmp::Payload};out.Put(target->id);r.length=out.at;}else r.length=0;}}
        else {RemoteObject* target=nullptr;for(auto& o:s.objects)if(o.id==object&&o.client==r.pid&&o.value){target=&o;break;}r.length=0;
            if(!target)hr=RPC_E_DISCONNECTED;
            else if(r.method==3&&in.cap==8){target->value->Release();*target={};hr=S_OK;}
            else if(r.method==4){wchar_t name[129]{};const auto n=in.Get<UINT>();if(n&&n<=128&&n*2==in.cap-in.at){in.Get(name,n*2);LPOLESTR names[]{name};DISPID id=DISPID_UNKNOWN;auto* active=target->value;active->AddRef();hr=active->GetIDsOfNames(GUID_NULL,names,1,LOCALE_USER_DEFAULT,&id);active->Release();Wire out{r.data,0,cmp::Payload};out.Put(id);r.length=out.at;}}
            else if(r.method==2){const auto id=in.Get<LONG>();const auto flags=in.Get<WORD>();const auto count=in.Get<UINT>();const auto named=in.Get<UINT>();DISPID namedId=DISPID_PROPERTYPUT;if(count<=4&&named<=1&&named<=count){if(named)namedId=in.Get<DISPID>();VARIANT args[4]{},value{};bool decoded=true;for(UINT i=0;i<count;++i)if(!Decode(in,args[i])){decoded=false;break;}if(decoded&&in.valid&&in.at==in.cap){DISPPARAMS params{args,named?&namedId:nullptr,count,named};EXCEPINFO exception{};UINT bad=0;auto* active=target->value;active->AddRef();hr=active->Invoke(id,GUID_NULL,LOCALE_USER_DEFAULT,flags,&params,&value,&exception,&bad);active->Release();Wire out{r.data,0,cmp::Payload};out.Put(bad);if(SUCCEEDED(hr)&&!Encode(out,value))hr=DISP_E_BADVARTYPE;r.length=SUCCEEDED(hr)?out.at:0;CmpSysFreeString(exception.bstrSource);CmpSysFreeString(exception.bstrDescription);CmpSysFreeString(exception.bstrHelpFile);}CmpVariantClear(&value);for(auto& arg:args)CmpVariantClear(&arg);}}
        }
        r.op=cmp::Op::Reply;r.pid=r.tid=0;r.status=DWORD(hr);GuardianComponentCall(&r);if(r.method==3&&s.endpoint==r.endpoint&&s.marshaled&&s.claimed){bool live=false;for(const auto& o:s.objects)if(o.value)live=true;if(!live)CloseServer(s);}
    }
    apt->pumping=false;
}
class Proxy final:public IDispatch {
public:
    LONG refs=1;uint64_t endpoint=0,object=0;DWORD tid=0,model=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(!Thread(tid,model))return RPC_E_WRONG_THREAD;if(!Guid(iid,__uuidof(IUnknown))&&!Guid(iid,__uuidof(IDispatch)))return E_NOINTERFACE;*out=static_cast<IDispatch*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(_InterlockedIncrement(&refs));}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=_InterlockedDecrement(&refs);if(!n){cmp::Request r;r.endpoint=endpoint;r.method=3;Wire w{r.data,0,cmp::Payload};w.Put(object);r.length=w.at;if(Apt())Transport(r,500);Free(this);}return ULONG(n);}
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out) override {if(!out)return E_POINTER;*out=0;return Thread(tid,model)?S_OK:RPC_E_WRONG_THREAD;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out) override {if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid,LPOLESTR* names,UINT count,LCID,DISPID* ids) override {if(!Thread(tid,model))return RPC_E_WRONG_THREAD;if(!Guid(iid,GUID_NULL))return DISP_E_UNKNOWNINTERFACE;if(!names||!ids||count!=1)return E_INVALIDARG;const auto n=Length(names[0],129);if(!n||n>128)return DISP_E_UNKNOWNNAME;cmp::Request r;r.endpoint=endpoint;r.method=4;Wire w{r.data,0,cmp::Payload};w.Put(object);w.Put(n);w.Put(names[0],n*2);r.length=w.at;const auto hr=Transport(r);if(SUCCEEDED(hr)&&r.length==sizeof(DISPID))Copy(ids,r.data,sizeof(DISPID));return hr;}
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID iid,LCID,WORD flags,DISPPARAMS* params,VARIANT* value,EXCEPINFO* exception,UINT* bad) override {if(value)CmpVariantInit(value);if(exception)Zero(exception,sizeof(*exception));if(bad)*bad=0;if(!Thread(tid,model))return RPC_E_WRONG_THREAD;if(!Guid(iid,GUID_NULL))return DISP_E_UNKNOWNINTERFACE;if(!params||params->cArgs>4||params->cNamedArgs>1||params->cNamedArgs>params->cArgs||(params->cArgs&&!params->rgvarg)||(params->cNamedArgs&&!params->rgdispidNamedArgs))return E_INVALIDARG;cmp::Request r;r.endpoint=endpoint;r.method=2;Wire w{r.data,0,cmp::Payload};w.Put(object);w.Put<LONG>(id);w.Put(flags);w.Put(params->cArgs);w.Put(params->cNamedArgs);if(params->cNamedArgs)w.Put(params->rgdispidNamedArgs[0]);for(UINT i=0;i<params->cArgs;++i)if(!Encode(w,params->rgvarg[i]))return DISP_E_BADVARTYPE;r.length=w.at;const auto hr=Transport(r);if(FAILED(hr))return hr;Wire in{r.data,0,r.length};const auto argument=in.Get<UINT>();VARIANT result{};if(!Decode(in,result)||!in.valid||in.at!=in.cap){CmpVariantClear(&result);return RPC_E_INVALID_DATA;}if(bad)*bad=argument;if(value)*value=result;else CmpVariantClear(&result);return hr;}
};
HRESULT CreateProxy(uint64_t endpoint,REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(!Guid(iid,__uuidof(IDispatch))&&!Guid(iid,__uuidof(IUnknown)))return E_NOINTERFACE;cmp::Request r;r.endpoint=endpoint;r.method=1;r.length=8;const auto hr=Transport(r);if(FAILED(hr))return hr;if(r.length!=8)return RPC_E_INVALID_DATA;auto* p=Make<Proxy>();if(!p){r.endpoint=endpoint;r.method=3;r.length=8;Transport(r,500);return E_OUTOFMEMORY;}p->endpoint=endpoint;Copy(&p->object,r.data,8);p->tid=GetCurrentThreadId();p->model=Apt()->model;*out=static_cast<IDispatch*>(p);return S_OK;}
bool ClassPath(REFCLSID cls,const wchar_t* kind,wchar_t (&path)[128],wchar_t (&model)[32]){
    constexpr wchar_t digits[]=L"0123456789ABCDEF";wchar_t key[128]=L"Software\\Classes\\CLSID\\{";UINT at=24;const auto byte=[&](BYTE b){key[at++]=digits[b>>4];key[at++]=digits[b&15];};
    const auto* bytes=reinterpret_cast<const BYTE*>(&cls);for(int i=3;i>=0;--i)byte(bytes[i]);key[at++]=L'-';byte(bytes[5]);byte(bytes[4]);key[at++]=L'-';byte(bytes[7]);byte(bytes[6]);key[at++]=L'-';byte(bytes[8]);byte(bytes[9]);key[at++]=L'-';for(int i=10;i<16;++i)byte(bytes[i]);key[at++]=L'}';key[at++]=L'\\';for(UINT i=0;kind[i];++i)key[at++]=kind[i];key[at]=0;
    for(auto root:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE}){HKEY h=nullptr;if(RegOpenKeyExW(root,key,0,KEY_READ,&h))continue;DWORD type=0,size=sizeof(path);const auto status=RegQueryValueExW(h,nullptr,nullptr,&type,reinterpret_cast<BYTE*>(path),&size);DWORD m=sizeof(model);RegQueryValueExW(h,L"ThreadingModel",nullptr,nullptr,reinterpret_cast<BYTE*>(model),&m);RegCloseKey(h);if(!status&&type==REG_SZ&&size>=2&&size<=sizeof(path)&&!path[size/2-1])return true;}return false;
}
bool GuestImage(const wchar_t* path){if(!path||Length(path,128)>=128)return false;wchar_t full[128]{};if(!GetFullPathNameW(path,128,full,nullptr)||!Text(full,path))return false;return (path[0]==L'C'||path[0]==L'c')&&path[1]==L':'&&path[2]==L'\\'&&path[3]&&!(path[3]==L'\\');}
}
extern "C" BOOL ComponentEntry(void*,DWORD,void*){return TRUE;}
extern "C" HRESULT WINAPI CmpCoInitializeEx(void* reserved,DWORD model){if(reserved||model&~(COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE|COINIT_SPEED_OVER_MEMORY))return E_INVALIDARG;const auto selected=model&COINIT_APARTMENTTHREADED;Guard g;auto* a=Apt();if(a){if(a->model!=selected)return RPC_E_CHANGED_MODE;++a->count;return S_FALSE;}for(auto& slot:apartments)if(!slot.tid){slot={GetCurrentThreadId(),selected,1,false};if(const auto user=GetModuleHandleW(L"user32.dll")){using Hook=void(WINAPI*)(void(*)());if(const auto hook=reinterpret_cast<Hook>(GetProcAddress(user,"GuardianComponentPump")))hook(Pump);}return S_OK;}return E_OUTOFMEMORY;}
extern "C" HRESULT WINAPI CmpCoInitialize(void* reserved){return CmpCoInitializeEx(reserved,COINIT_APARTMENTTHREADED);}
extern "C" HRESULT WINAPI CmpOleInitialize(void* reserved){return CmpCoInitialize(reserved);}
extern "C" void WINAPI CmpCoUninitialize(){Apartment* a=nullptr;{Guard lock;a=Apt();if(!a||--a->count)return;}Server* owned[cmp::Endpoints]{};const auto count=OwnedServers(owned);for(UINT i=0;i<count;++i)CloseServer(*owned[i]);bool last=true;{Guard lock;*a={};for(const auto& other:apartments)if(other.count)last=false;}if(last){if(const auto user=GetModuleHandleW(L"user32.dll")){using Hook=void(WINAPI*)(void(*)());if(const auto hook=reinterpret_cast<Hook>(GetProcAddress(user,"GuardianComponentPump")))hook(nullptr);}}}
extern "C" void WINAPI CmpOleUninitialize(){CmpCoUninitialize();}
extern "C" HRESULT WINAPI CmpCoGetApartmentType(APTTYPE* type,APTTYPEQUALIFIER* qualifier){if(!type||!qualifier)return E_POINTER;const auto* a=Apt();if(!a)return CO_E_NOTINITIALIZED;*type=a->model?APTTYPE_STA:APTTYPE_MTA;*qualifier=APTTYPEQUALIFIER_NONE;return S_OK;}
extern "C" HRESULT WINAPI CmpCoRegisterClassObject(REFCLSID cls,IUnknown* factory,DWORD context,DWORD flags,DWORD* cookie){if(!cookie)return E_POINTER;*cookie=0;if(!Apt())return CO_E_NOTINITIALIZED;if(!factory||context!=CLSCTX_LOCAL_SERVER||flags!=REGCLS_MULTIPLEUSE)return E_INVALIDARG;IClassFactory* valid=nullptr;auto hr=factory->QueryInterface(__uuidof(IClassFactory),reinterpret_cast<void**>(&valid));if(FAILED(hr))return hr;valid->Release();if(auto* reserved=ReserveServer()){auto& s=*reserved;cmp::Request r;r.op=cmp::Op::Register;Copy(r.iid,&cls,16);const auto e=GuardianComponentCall(&r);if(e){{Guard lock;s={};}return Error(e);}s.endpoint=r.endpoint;s.tid=GetCurrentThreadId();s.factory=factory;factory->AddRef();*cookie=DWORD(r.endpoint);return S_OK;}return E_OUTOFMEMORY;}
extern "C" HRESULT WINAPI CmpCoRevokeClassObject(DWORD cookie){if(!Apt())return CO_E_NOTINITIALIZED;Server* found=nullptr;{Guard lock;for(auto& s:servers)if(s.endpoint==cookie){if(s.tid!=GetCurrentThreadId())return RPC_E_WRONG_THREAD;found=&s;break;}}if(!found)return E_INVALIDARG;CloseServer(*found);return S_OK;}
namespace {
class FactoryProxy final:public IClassFactory {
public:
    LONG refs=1;uint64_t endpoint=0;DWORD tid=0,model=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(!Thread(tid,model))return RPC_E_WRONG_THREAD;if(!Guid(iid,__uuidof(IUnknown))&&!Guid(iid,__uuidof(IClassFactory)))return E_NOINTERFACE;*out=static_cast<IClassFactory*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(_InterlockedIncrement(&refs));}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=_InterlockedDecrement(&refs);if(!n)Free(this);return ULONG(n);}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override {if(out)*out=nullptr;if(!Thread(tid,model))return RPC_E_WRONG_THREAD;if(outer)return CLASS_E_NOAGGREGATION;return CreateProxy(endpoint,iid,out);}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL locked) override {if(!Thread(tid,model))return RPC_E_WRONG_THREAD;cmp::Request r;r.endpoint=endpoint;r.method=5;Wire w{r.data,0,cmp::Payload};w.Put<uint64_t>(0);w.Put<DWORD>(locked?1:0);r.length=w.at;return Transport(r);}
};
HRESULT LocalClass(REFCLSID cls,uint64_t& endpoint){
    cmp::Request r;r.op=cmp::Op::Lookup;Copy(r.iid,&cls,16);auto e=GuardianComponentCall(&r);
    if(e==1168){wchar_t path[128]{},model[32]{};if(!ClassPath(cls,L"LocalServer32",path,model)||!GuestImage(path))return REGDB_E_CLASSNOTREG;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        if(!CreateProcessW(path,nullptr,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))return HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(child.hThread);const auto start=GetTickCount64();do{r={};r.op=cmp::Op::Lookup;Copy(r.iid,&cls,16);e=GuardianComponentCall(&r);if(e!=1168)break;DWORD code=0;if(GetExitCodeProcess(child.hProcess,&code)&&code!=STILL_ACTIVE)break;Pump();Sleep(1);}while(GetTickCount64()-start<3000);CloseHandle(child.hProcess);
    }
    if(e)return e==1168?CO_E_SERVER_EXEC_FAILURE:Error(e);endpoint=r.endpoint;return S_OK;
}
}
extern "C" HRESULT WINAPI CmpCoGetClassObject(REFCLSID cls,DWORD context,COSERVERINFO* remote,REFIID iid,void** out){
    if(!out)return E_INVALIDARG;*out=nullptr;if(!Apt())return CO_E_NOTINITIALIZED;
    if(remote||context&~(CLSCTX_INPROC_SERVER|CLSCTX_LOCAL_SERVER)||!context)return E_INVALIDARG;
    if(context&CLSCTX_INPROC_SERVER){wchar_t path[128]{},model[32]{};if(ClassPath(cls,L"InprocServer32",path,model)){
        if(!GuestImage(path))return REGDB_E_CLASSNOTREG;
        if(!Text(model,L"Both")&&!Text(model,L"Free")&&!Text(model,L"Apartment"))return CO_E_NOT_SUPPORTED;
        if((Text(model,L"Apartment")&&!Apt()->model)||(Text(model,L"Free")&&Apt()->model))return CO_E_NOT_SUPPORTED;
        const auto h=LoadLibraryW(path);if(!h)return HRESULT_FROM_WIN32(GetLastError());using Get=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
        const auto get=reinterpret_cast<Get>(GetProcAddress(h,"DllGetClassObject"));const auto hr=get?get(cls,iid,out):CLASS_E_CLASSNOTAVAILABLE;
        bool stored=false;{Guard lock;for(auto& m:modules)if(!m.h&&!m.busy){m.h=h;stored=true;break;}}
        if(!stored){if(*out){static_cast<IUnknown*>(*out)->Release();*out=nullptr;}FreeLibrary(h);return E_OUTOFMEMORY;}return hr;
    }}
    if(context&CLSCTX_LOCAL_SERVER){uint64_t endpoint=0;const auto hr=LocalClass(cls,endpoint);if(FAILED(hr))return hr;
        auto* factory=Make<FactoryProxy>();if(!factory)return E_OUTOFMEMORY;factory->endpoint=endpoint;factory->tid=GetCurrentThreadId();factory->model=Apt()->model;
        const auto query=factory->QueryInterface(iid,out);factory->Release();return query;
    }
    return REGDB_E_CLASSNOTREG;
}
extern "C" HRESULT WINAPI CmpCoCreateInstance(REFCLSID cls,IUnknown* outer,DWORD context,REFIID iid,void** out){
    if(!out)return E_INVALIDARG;*out=nullptr;if(!Apt())return CO_E_NOTINITIALIZED;if(outer)return CLASS_E_NOAGGREGATION;
    IClassFactory* factory=nullptr;const auto hr=CmpCoGetClassObject(cls,context,nullptr,__uuidof(IClassFactory),reinterpret_cast<void**>(&factory));
    if(FAILED(hr))return hr;const auto created=factory->CreateInstance(nullptr,iid,out);factory->Release();return created;
}
extern "C" void WINAPI CmpCoFreeUnusedLibraries(){for(auto& m:modules){HMODULE h=nullptr;{Guard lock;if(!m.h||m.busy)continue;h=m.h;m.busy=true;}using Can=HRESULT(WINAPI*)();const auto can=reinterpret_cast<Can>(GetProcAddress(h,"DllCanUnloadNow"));const bool unload=can&&can()==S_OK;if(unload)FreeLibrary(h);{Guard lock;if(unload)m.h=nullptr;m.busy=false;}}}
extern "C" void WINAPI CmpCoFreeUnusedLibrariesEx(DWORD,DWORD){CmpCoFreeUnusedLibraries();}
extern "C" HRESULT WINAPI CmpCoWaitForMultipleHandles(DWORD flags,DWORD timeout,ULONG count,HANDLE* handles,DWORD* index){if(!Apt())return CO_E_NOTINITIALIZED;if(!index||!count||count>8||!handles||flags&~(COWAIT_DISPATCH_CALLS|COWAIT_DISPATCH_WINDOW_MESSAGES))return E_INVALIDARG;const auto start=GetTickCount64();do{Pump();for(ULONG i=0;i<count;++i){const auto wait=WaitForSingleObject(handles[i],0);if(wait==WAIT_OBJECT_0){*index=i;return S_OK;}if(wait==WAIT_FAILED)return HRESULT_FROM_WIN32(GetLastError());}if(flags&COWAIT_DISPATCH_WINDOW_MESSAGES){using Peek=BOOL(WINAPI*)(MSG*,HWND,UINT,UINT,UINT);using Dispatch=LRESULT(WINAPI*)(const MSG*);const auto user=GetModuleHandleW(L"user32.dll");const auto peek=reinterpret_cast<Peek>(GetProcAddress(user,"PeekMessageW"));const auto dispatch=reinterpret_cast<Dispatch>(GetProcAddress(user,"DispatchMessageW"));MSG message{};if(peek&&dispatch&&peek(&message,nullptr,0,0,PM_REMOVE))dispatch(&message);}if(timeout==0)break;Sleep(1);}while(timeout==INFINITE||GetTickCount64()-start<timeout);return RPC_S_CALLPENDING;}
#include "ComponentMarshal.inl"
#include "ComponentGuids.inl"
#include "ComponentRpc.inl"
