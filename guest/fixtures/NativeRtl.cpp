#define GUARDIAN_NTDLL_BUILD
#include "NativeApi.h"
#include "PeRuntimeApi.h"
#include "PeTls.h"
#include "../Heap.h"
#include <intrin.h>
namespace guardian::native {
namespace {
struct Heap {void* base=nullptr;memory::Heap memory;};Heap heaps[4]{};
win::Handle lock=nullptr;void (*callbacks[32])(void*){};bool allocated[32]{};
__declspec(thread) void* fls[32]{};
void** threadValues[4]{};
bool Lock() noexcept {if(!lock)return false;const auto result=win::WaitForSingleObject(lock,UINT32_MAX);return result==0||result==0x80;}
void Unlock() noexcept {win::ReleaseMutex(lock);}
void Cleanup() noexcept {void (*pending[32])(void*){};void* values[32]{};if(!Lock())return;
    for(uint32_t i=1;i<32;++i){pending[i]=allocated[i]?callbacks[i]:nullptr;values[i]=fls[i];fls[i]=nullptr;}
    for(auto& state:threadValues)if(state==fls)state=nullptr;Unlock();for(uint32_t i=1;i<32;++i)if(values[i]&&pending[i])pending[i](values[i]);}
Heap* Find(void* h) noexcept {for(auto& heap:heaps)if(heap.base&&h==&heap)return &heap;return nullptr;}
bool Narrow(const Unicode* s,char* out) noexcept {if(!s||(s->length&1)||s->length>s->maximum||s->length>=256||(!s->buffer&&s->length))return false;
    const auto* text=reinterpret_cast<const uint16_t*>(s->buffer);for(uint32_t i=0;i<uint32_t(s->length)/2;++i){if(text[i]<32||text[i]>126)return false;out[i]=char(text[i]);}out[s->length/2]=0;return true;}
}
#ifdef GUARDIAN_CRT_RUNTIME
extern "C" __declspec(dllexport) void GuardianCrtFlsCleanup(){Cleanup();}
#endif
extern "C" void* RtlGetCurrentPeb(){return reinterpret_cast<void*>(__readgsqword(0x60));}
extern "C" Status RtlGetVersion(VersionInfo* out){if(!out||(out->size!=276&&out->size!=sizeof(VersionInfo)))return Invalid;
    const auto* peb=static_cast<const uint8_t*>(RtlGetCurrentPeb());out->major=*reinterpret_cast<const uint32_t*>(peb+0x118);out->minor=*reinterpret_cast<const uint32_t*>(peb+0x11C);out->build=*reinterpret_cast<const uint16_t*>(peb+0x120);out->platform=2;
    for(auto& ch:out->servicePack)ch=0;const auto pack=*reinterpret_cast<const uint16_t*>(peb+0x122);
    if(pack==0x100){const uint16_t text[]{'S','e','r','v','i','c','e',' ','P','a','c','k',' ','1',0};for(uint32_t i=0;i<15;++i)out->servicePack[i]=text[i];}
    if(out->size==sizeof(VersionInfo)){out->spMajor=pack>>8;out->spMinor=pack&255;out->suite=0;out->product=1;out->reserved=0;}return 0;}
extern "C" void RtlInitUnicodeString(Unicode* out,const uint16_t* source){if(!out)return;uint32_t n=0;if(source)while(n<32766&&source[n])++n;*out={uint16_t(n*2),uint16_t(source?n*2+2:0),0,reinterpret_cast<uint64_t>(source)};}
extern "C" void RtlInitAnsiString(Ansi* out,const char* source){if(!out)return;uint32_t n=0;if(source)while(n<65534&&source[n])++n;*out={uint16_t(n),uint16_t(source?n+1:0),0,reinterpret_cast<uint64_t>(source)};}
extern "C" int32_t RtlCompareUnicodeString(const Unicode* a,const Unicode* b,uint8_t fold){const auto* x=reinterpret_cast<const uint16_t*>(a->buffer);const auto* y=reinterpret_cast<const uint16_t*>(b->buffer);const auto n=a->length<b->length?a->length:b->length;
    for(uint32_t i=0;i<uint32_t(n)/2;++i){uint16_t left=x[i],right=y[i];if(fold){if(left>='a'&&left<='z')left-=32;if(right>='a'&&right<='z')right-=32;}if(left!=right)return int32_t(left)-int32_t(right);}return int32_t(a->length)-int32_t(b->length);}
extern "C" uint8_t RtlEqualUnicodeString(const Unicode* a,const Unicode* b,uint8_t fold){return a&&b&&a->length==b->length&&!RtlCompareUnicodeString(a,b,fold);}
extern "C" void* RtlCreateHeap(uint32_t flags,void* requested,uint64_t reserve,uint64_t commit,void* lockPointer,void* parameters){
    if((flags&~2u)||requested||lockPointer||parameters||reserve>(1u<<20)||commit>reserve)return nullptr;if(!Lock())return nullptr;
    Heap* heap=nullptr;for(auto& h:heaps)if(!h.base){heap=&h;break;}if(!heap){Unlock();return nullptr;}
    uint64_t size=reserve?reserve:65536;void* base=nullptr;const auto status=NtAllocateVirtualMemory(reinterpret_cast<void*>(CurrentProcess),&base,0,&size,0x3000,4);
    if(status||!heap->memory.Initialize(base,uint32_t(size))){Unlock();return nullptr;}heap->base=base;Unlock();return heap;
}
extern "C" void* RtlDestroyHeap(void* handle){if(!Lock())return handle;auto* h=Find(handle);if(!h){Unlock();return handle;}uint64_t size=0;void* base=h->base;
    if(NtFreeVirtualMemory(reinterpret_cast<void*>(CurrentProcess),&base,&size,0x8000)){Unlock();return handle;}*h={};Unlock();return nullptr;}
extern "C" void* RtlAllocateHeap(void* handle,uint32_t flags,uint64_t size){if((flags&~8u)||size>UINT32_MAX||!Lock())return nullptr;auto* h=Find(handle);auto* out=h?h->memory.Allocate(uint32_t(size?size:1),(flags&8)!=0):nullptr;Unlock();return out;}
extern "C" uint8_t RtlFreeHeap(void* handle,uint32_t flags,void* memory){if(flags||!Lock())return 0;auto* h=Find(handle);const bool result=h&&(!memory||h->memory.Release(memory));Unlock();return result;}
extern "C" void* RtlReAllocateHeap(void* handle,uint32_t flags,void* previous,uint64_t size){if((flags&~8u)||!size||size>UINT32_MAX||!Lock())return nullptr;auto* h=Find(handle);const auto old=h?h->memory.Size(previous):0;
    void* out=h&&old?h->memory.Reallocate(previous,uint32_t(size)):nullptr;if(out&&(flags&8)&&size>old)memory::HeapZero(static_cast<uint8_t*>(out)+old,uint32_t(size)-old);Unlock();return out;}
extern "C" uint64_t RtlSizeHeap(void* handle,uint32_t flags,void* pointer){if(flags||!Lock())return UINT64_MAX;auto* h=Find(handle);const auto n=h?h->memory.Size(pointer):0;Unlock();return n?n:UINT64_MAX;}
extern "C" Status RtlFlsAlloc(void (*callback)(void*),uint32_t* index){if(!index)return Invalid;if(!Lock())return NoMemory;for(uint32_t i=1;i<32;++i)if(!allocated[i]){allocated[i]=true;callbacks[i]=callback;*index=i;Unlock();return 0;}Unlock();return NoMemory;}
extern "C" Status RtlFlsSetValue(uint32_t index,void* value){if(index>=32||!index||!Lock())return Invalid;if(!allocated[index]){Unlock();return Invalid;}fls[index]=value;Unlock();return 0;}
extern "C" Status RtlFlsGetValue(uint32_t index,void** value){if(index>=32||!index||!value||!Lock())return Invalid;if(!allocated[index]){Unlock();return Invalid;}*value=fls[index];Unlock();return 0;}
extern "C" Status RtlFlsFree(uint32_t index){if(!index||index>=32||!Lock())return Invalid;if(!allocated[index]){Unlock();return Invalid;}
    const auto callback=callbacks[index];void* values[4]{};uint32_t used=0;
    for(auto& state:threadValues)if(state){guardian::memory::Information info{};uint64_t bytes=0;
        if(NtQueryVirtualMemory(reinterpret_cast<void*>(CurrentProcess),state,0,&info,sizeof(info),&bytes)||info.state!=0x1000){state=nullptr;continue;}
        if(state[index])values[used++]=state[index];state[index]=nullptr;}
    allocated[index]=false;callbacks[index]=nullptr;Unlock();if(callback)for(uint32_t i=0;i<used;++i)callback(values[i]);return 0;}
extern "C" uint32_t RtlNtStatusToDosError(Status status){switch(status){case 0:return 0;case InvalidHandle:return 6;case Invalid:return 87;case Denied:return 5;case NoMemory:return 8;case 0xC0000034:return 2;case Small:return 122;case AccessViolation:return 998;case LengthMismatch:return 24;case Timeout:return 1460;case NotImplemented:return 1;case Unsupported:return 50;default:return 317;}}
extern "C" void RtlRaiseException(guardian::exceptions::Record* record){if(!record)return;win::RaiseException(record->code,record->flags,record->count,record->information);}
extern "C" Status LdrLoadDll(const uint16_t* path,uint32_t* flags,Unicode* name,Handle* out){if(!out||path||(flags&&*flags))return Unsupported;char text[128]{};if(!Narrow(name,text))return Invalid;
    *out=win::LoadLibraryA(text);return *out?0:0xC0000135u;}
extern "C" Status LdrUnloadDll(Handle module){return win::FreeLibrary(module)?0:InvalidHandle;}
extern "C" Status LdrGetDllHandle(const uint16_t* path,void* flags,Unicode* name,Handle* out){if(!out||path||flags)return Unsupported;char text[128]{};if(!Narrow(name,text))return Invalid;*out=win::GetModuleHandleA(text);return *out?0:0xC0000135u;}
extern "C" Status LdrGetProcedureAddress(Handle module,Ansi* name,uint32_t ordinal,void** out){if(!out||ordinal>65535)return Invalid;char text[128]{};const char* symbol=reinterpret_cast<const char*>(uint64_t(ordinal));
    if(name){if(name->length>=128||name->length>name->maximum||!name->buffer)return Invalid;for(uint32_t i=0;i<name->length;++i)text[i]=reinterpret_cast<const char*>(name->buffer)[i];symbol=text;}
    *out=reinterpret_cast<void*>(win::GetProcAddress(module,symbol));return *out?0:0xC000007Au;}
extern "C" int NativeDllEntry(void*,uint32_t reason,void*){if(reason==1){lock=win::CreateMutexA(nullptr,0,nullptr);return lock!=nullptr;}
    if(!reason){Cleanup();for(auto& h:heaps)if(h.base)RtlDestroyHeap(&h);if(lock)win::CloseHandle(lock);lock=nullptr;}return 1;}
}
extern "C" void TlsCallback(void*,uint32_t reason,void*){using namespace guardian::native;
    if(reason==1||reason==2){const bool locked=lock&&Lock();bool found=false;for(auto state:threadValues)if(state==fls)found=true;
        if(!found)for(auto& state:threadValues)if(!state){state=fls;break;}if(locked)Unlock();}
    else if(reason==3)Cleanup();}
