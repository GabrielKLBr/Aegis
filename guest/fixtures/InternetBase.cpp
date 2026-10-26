#include "InternetBase.h"
extern "C" size_t strlen(const char*);
#pragma function(strlen)
extern "C" size_t strlen(const char* text){const volatile char* p=text;size_t n=0;while(p[n])++n;return n;}
namespace netapp {
namespace {volatile LONG lock=0;UINT depth=0;uint64_t next=0;struct Entry{uint64_t handle;UINT kind;void* data;};Entry entries[128]{};}
void Copy(void* d,const void* s,SIZE_T n){for(SIZE_T i=0;i<n;++i)static_cast<BYTE*>(d)[i]=static_cast<const BYTE*>(s)[i];}
void Wipe(void* p,SIZE_T n){for(SIZE_T i=0;i<n;++i)static_cast<volatile BYTE*>(p)[i]=0;}
bool Equal(const void* a,const void* b,SIZE_T n){BYTE difference=0;for(SIZE_T i=0;i<n;++i)difference|=static_cast<const BYTE*>(a)[i]^static_cast<const BYTE*>(b)[i];return !difference;}
UINT Length(const wchar_t* s,UINT cap){UINT n=0;if(s)while(n<cap&&s[n])++n;return n;}
UINT Length(const char* s,UINT cap){UINT n=0;if(s)while(n<cap&&s[n])++n;return n;}
bool Text(const wchar_t* a,const wchar_t* b){if(!a||!b)return a==b;for(UINT i=0;i<32768;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
bool Narrow(const wchar_t* s,char* out,UINT cap){if(!s||!out||!cap)return false;UINT n=0;for(;n+1<cap&&s[n];++n){if(s[n]<32||s[n]>126)return false;out[n]=char(s[n]);}out[n]=0;return !s[n];}
void* Allocate(SIZE_T n){return n&&n<=2u<<20?HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n):nullptr;}
void Release(void* p){if(p)HeapFree(GetProcessHeap(),0,p);}
Guard::Guard(){const auto tid=LONG(GetCurrentThreadId());while(_InterlockedCompareExchange(&lock,tid,0)!=0&&lock!=tid)SwitchToThread();++depth;}
Guard::~Guard(){if(!--depth)_InterlockedExchange(&lock,0);}
uint64_t Handle(UINT kind,void* data){Guard guard;for(auto& e:entries)if(!e.handle&&next<0xFFFFFF){e={0xA900000000ull|(++next<<8)|kind,kind,data};return e.handle;}return 0;}
void* Object(uint64_t handle,UINT kind){for(const auto& e:entries)if(e.handle==handle&&e.kind==kind)return e.data;return nullptr;}
bool Drop(uint64_t handle,UINT kind){for(auto& e:entries)if(e.handle==handle&&e.kind==kind){e={};return true;}return false;}
UINT Handles(UINT kind,uint64_t* out,UINT capacity){Guard guard;UINT n=0;for(const auto& e:entries)if(e.handle&&e.kind==kind&&n<capacity)out[n++]=e.handle;return n;}
DWORD Exchange(inet::Request& r){return GuardianInternetCall(&r);}
bool Random(void* p,UINT bytes){if(!p||bytes>65536)return false;auto* out=static_cast<BYTE*>(p);while(bytes){inet::Request r;r.op=inet::Op::Random;r.length=bytes>inet::Payload?inet::Payload:bytes;if(Exchange(r))return false;Copy(out,r.data,r.length);out+=r.length;bytes-=r.length;}return true;}
DWORD Resolve(const char* name,BYTE address[16]){inet::Request r;r.op=inet::Op::Resolve;const auto n=Length(name,64);if(!n||n>=64)return WSAEINVAL;Copy(r.name,name,n+1);for(UINT i=0;i<n;++i)if(r.name[i]>='A'&&r.name[i]<='Z')r.name[i]=char(r.name[i]+32);const auto e=Exchange(r);if(!e)Copy(address,r.address,16);return e;}
DWORD Open(UINT kind,UINT& id,UINT* localPort){inet::Request r;r.op=inet::Op::Open;r.kind=kind;const auto e=Exchange(r);if(!e){id=r.id;if(localPort)*localPort=r.port;}return e;}
DWORD Connect(UINT id,const BYTE address[16],UINT port){inet::Request r;r.op=inet::Op::Connect;r.id=id;r.port=port;Copy(r.address,address,16);return Exchange(r);}
DWORD Poll(UINT id,DWORD& flags){inet::Request r;r.op=inet::Op::Poll;r.id=id;const auto e=Exchange(r);flags=r.flags;return e;}
DWORD Send(UINT id,const void* data,UINT bytes,UINT& count){count=0;if(bytes>inet::Payload||(!data&&bytes))return WSAEINVAL;inet::Request r;r.op=inet::Op::Send;r.id=id;r.length=bytes;if(bytes)Copy(r.data,data,bytes);const auto e=Exchange(r);if(!e)count=r.length;return e;}
DWORD Receive(UINT id,void* data,UINT bytes,UINT& count){count=0;if(bytes>inet::Payload||(!data&&bytes))return WSAEINVAL;inet::Request r;r.op=inet::Op::Receive;r.id=id;r.length=bytes;const auto e=Exchange(r);if(!e||e==WSAEMSGSIZE){count=r.length;if(count)Copy(data,r.data,count);}return e;}
void Close(UINT id){inet::Request r;r.op=inet::Op::Close;r.id=id;Exchange(r);}
void Cleanup(){Guard guard;for(auto& e:entries)if(e.handle){Release(e.data);e={};}}
}
extern "C" BOOL WINAPI InternetEntry(HINSTANCE,DWORD reason,void*){if(reason==DLL_PROCESS_DETACH)netapp::Cleanup();return TRUE;}
