#define GUARDIAN_RUNTIME_BUILD
#include "PeRuntimeApi.h"
#include "guardian/StorageQuota.h"
#include "../Heap.h"
#include "guardian/Runtime.h"
#include "guardian/Storage.h"
#include <intrin.h>
extern "C" uint32_t GuardianRuntimeCall(guardian::runtime::Command*) noexcept;
namespace guardian::win {
namespace {
using runtime::Command;using runtime::Op;
memory::Heap heap;alignas(16) uint8_t arena[8192]{};Handle heapMutex=nullptr;bool ready=false;
uint32_t Error(uint32_t s) noexcept {switch(s){case 0:return 0;case storage::NotFound:return 2;case storage::Denied:return 5;case storage::InvalidHandle:case 0xC0000024:return 6;
case storage::Quota:case 0xC0000017:return 8;case storage::Sharing:return 32;case storage::Unsupported:return 50;case storage::Invalid:return 87;
case 0xC00000A0:case 0xC0000018:return 487;case 0xC0000045:return 87;
case storage::NoSpace:return 112;case runtime::NegativeSeek:return 131;case storage::Collision:return 183;case storage::Small:return 234;case 0xC0000046:return 288;default:return 1;}}
uint32_t Call(Command& c) noexcept {const auto s=GuardianRuntimeCall(&c);c.status=s;return s;}
bool Text(char* out,const char* in,uint32_t n) noexcept {if(!in)return false;for(uint32_t i=0;i<n;++i){const auto c=static_cast<uint8_t>(in[i]);if(c>126 || (c && c<32))return false;out[i]=in[i];if(!c)return true;}return false;}
void Copy(void* to,const void* from,uint32_t n) noexcept {for(uint32_t i=0;i<n;++i)static_cast<uint8_t*>(to)[i]=static_cast<const uint8_t*>(from)[i];}
Bool Result(uint32_t status) noexcept {if(!status)return 1;SetLastError(Error(status));return 0;}
bool Lock() noexcept {if(!ready)return false;const auto s=WaitForSingleObject(heapMutex,UINT32_MAX);return s==0 || s==0x80;}
}
extern "C" __declspec(noinline) uint16_t RtlCaptureStackBackTrace(Dword skip,Dword count,void** frames,Dword* hash){
    if(hash)*hash=0;if(!frames || !count || count>16 || skip>16){SetLastError(87);return 0;}
    Command c{};c.op=Op::StackCapture;c.flags=skip;c.length=count;
    const auto status=GuardianRuntimeCall(&c);if(status){SetLastError(Error(status));return 0;}
    for(uint32_t i=0;i<c.transferred;++i)Copy(frames+i,c.data+i*8,8);if(hash)*hash=static_cast<Dword>(c.argument);return static_cast<uint16_t>(c.transferred);
}
extern "C" __declspec(noinline) uint16_t GuardianCaptureProbe(Dword skip,void** frames,Dword* hash){
    volatile uint32_t guard=0xA55A;const auto n=RtlCaptureStackBackTrace(skip,16,frames,hash);guard=guard+1;return guard==0xA55B?n:0;
}
extern "C" Bool GetDiskFreeSpaceExA(const char* root,uint64_t* available,uint64_t* total,uint64_t* free){
    if(root && !((root[0]=='C'||root[0]=='c')&&root[1]==':'&&root[2]=='\\'&&!root[3])){SetLastError(87);return 0;}
    Command c{};c.op=Op::DiskSpace;c.path[0]='C';c.path[1]=':';c.path[2]='\\';
    const auto status=Call(c);if(status)return Result(status);storage::Space space{};Copy(&space,c.data,sizeof(space));
    if(available)*available=space.available;if(total)*total=space.capacity;if(free)*free=space.free;return 1;
}
extern "C" Bool GetDiskFreeSpaceExW(const uint16_t* root,uint64_t* available,uint64_t* total,uint64_t* free){
    if(root && !((root[0]=='C'||root[0]=='c')&&root[1]==':'&&root[2]=='\\'&&!root[3])){SetLastError(87);return 0;}
    return GetDiskFreeSpaceExA(nullptr,available,total,free);
}
extern "C" Dword GetLastError(){return __readgsdword(0x68);}
extern "C" void SetLastError(Dword e){__writegsdword(0x68,e);}
extern "C" Dword GetCurrentProcessId(){return static_cast<Dword>(__readgsqword(0x40));}
extern "C" Dword GetCurrentThreadId(){return static_cast<Dword>(__readgsqword(0x48));}
extern "C" Handle CreateThread(void* security,uint64_t stack,ThreadStart start,void* argument,Dword flags,Dword* id){
    if(security || stack || !start || (flags&~4u)){SetLastError(87);return nullptr;}
    Command c{};c.op=Op::ThreadCreate;c.flags=flags;c.argument=reinterpret_cast<uint64_t>(argument);const auto entry=reinterpret_cast<uint64_t>(start);Copy(c.data,&entry,8);
    const auto status=Call(c);if(status){SetLastError(Error(status));return nullptr;}if(id)*id=c.transferred;return reinterpret_cast<Handle>(c.handle);
}
extern "C" Dword ResumeThread(Handle h){Command c{};c.op=Op::ThreadResume;c.handle=reinterpret_cast<uint64_t>(h);const auto s=Call(c);if(s){SetLastError(Error(s));return UINT32_MAX;}return c.transferred;}
extern "C" Bool GetExitCodeThread(Handle h,Dword* code){if(!code){SetLastError(87);return 0;}Command c{};c.op=Op::ThreadQuery;c.handle=reinterpret_cast<uint64_t>(h);const auto s=Call(c);if(s)return Result(s);*code=c.transferred;return 1;}
extern "C" Bool SwitchToThread(){Command c{};c.op=Op::Yield;const auto s=Call(c);return s?Result(s):static_cast<Bool>(c.transferred);}
extern "C" Bool CreateProcessA(const char* application,char* command,void* processSecurity,void* threadSecurity,Bool inherit,Dword flags,void* environment,const char* directory,StartupInfoA* startup,ProcessInformation* information){
    if(information)*information={};
    if(!information || !startup || startup->cb!=sizeof(StartupInfoA) || processSecurity || threadSecurity || inherit || (flags&~4u) || environment || directory){SetLastError(87);return 0;}
    const auto* options=reinterpret_cast<const uint8_t*>(startup);for(uint32_t i=8;i<sizeof(StartupInfoA);++i)if(options[i]){SetLastError(87);return 0;}
    Command c{};c.op=Op::ProcessCreate;c.flags=flags;
    if(!Text(c.path,application,128) || !Text(reinterpret_cast<char*>(c.data),command?command:application,128)){SetLastError(87);return 0;}
    const auto status=Call(c);if(status)return Result(status);Copy(information,c.data,sizeof(*information));return 1;
}
extern "C" Bool GetExitCodeProcess(Handle h,Dword* code){if(!code){SetLastError(87);return 0;}Command c{};c.op=Op::ProcessQuery;c.handle=reinterpret_cast<uint64_t>(h);const auto s=Call(c);if(s)return Result(s);*code=c.transferred;return 1;}
extern "C" char* GetCommandLineA(){return reinterpret_cast<char*>(*reinterpret_cast<const uint64_t*>(__readgsqword(0x60)+0x20));}
extern "C" __declspec(noreturn) void ExitThread(Dword code){Command c{};c.op=Op::ThreadExit;c.argument=code;Call(c);__ud2();}
extern "C" __declspec(noreturn) void ExitProcess(Dword code){Command c{};c.op=Op::ProcessExit;c.argument=code;Call(c);__ud2();}
extern "C" Handle GetProcessHeap(){return ready?&heap:nullptr;}
extern "C" void* HeapAlloc(Handle h,Dword flags,uint64_t n){
    if(h!=GetProcessHeap() || (flags&~8u) || n>UINT32_MAX){SetLastError(87);return nullptr;}if(!Lock())return nullptr;
    auto* p=heap.Allocate(n?static_cast<uint32_t>(n):1u,(flags&8)!=0);ReleaseMutex(heapMutex);return p;
}
extern "C" void* HeapReAlloc(Handle h,Dword flags,void* old,uint64_t n){
    if(h!=GetProcessHeap() || !old || (flags&~8u) || n>UINT32_MAX){SetLastError(87);return nullptr;}if(!Lock())return nullptr;
    const auto size=heap.Size(old);void* p=size?heap.Reallocate(old,n?static_cast<uint32_t>(n):1u):nullptr;
    if(p && (flags&8) && n>size)for(uint32_t i=size;i<n;++i)static_cast<uint8_t*>(p)[i]=0;ReleaseMutex(heapMutex);return p;
}
extern "C" Bool HeapFree(Handle h,Dword flags,void* p){
    if(h!=GetProcessHeap() || flags){SetLastError(87);return 0;}if(!Lock())return 0;const bool ok=!p || heap.Release(p);ReleaseMutex(heapMutex);if(!ok)SetLastError(87);return ok?1:0;
}
extern "C" Handle GetStdHandle(Dword h){if(h==Dword(-11) || h==Dword(-12))return reinterpret_cast<Handle>(runtime::Console);SetLastError(87);return reinterpret_cast<Handle>(UINT64_MAX);}
extern "C" Handle CreateFileA(const char* path,Dword access,Dword share,void* security,Dword disposition,Dword attributes,Handle templ){
    Command c{};c.op=Op::Open;c.access=access;c.share=share;c.disposition=disposition;
    if(security || templ || (attributes && attributes!=0x80) || !Text(c.path,path,128)){SetLastError(87);return reinterpret_cast<Handle>(UINT64_MAX);}
    const auto s=Call(c);if(s){SetLastError(s==storage::Collision && disposition==1?80:Error(s));return reinterpret_cast<Handle>(UINT64_MAX);}
    if(disposition==2 || disposition==4)SetLastError(c.flags?183:0);return reinterpret_cast<Handle>(c.handle);
}
extern "C" Bool ReadFile(Handle h,void* out,Dword n,Dword* transferred,void* overlapped){
    if(transferred)*transferred=0;if(!transferred || overlapped || (n && !out) || n>512){SetLastError(87);return 0;}
    Command c{};c.op=Op::Read;c.handle=reinterpret_cast<uint64_t>(h);c.length=n;const auto s=Call(c);if(s)return Result(s);Copy(out,c.data,c.transferred);*transferred=c.transferred;return 1;
}
extern "C" Bool WriteFile(Handle h,const void* in,Dword n,Dword* transferred,void* overlapped){
    if(transferred)*transferred=0;if(!transferred || overlapped || (n && !in) || n>512){SetLastError(87);return 0;}
    Command c{};c.op=reinterpret_cast<uint64_t>(h)==runtime::Console?Op::ConsoleWrite:Op::Write;c.handle=reinterpret_cast<uint64_t>(h);c.length=n;Copy(c.data,in,n);
    const auto s=Call(c);if(s)return Result(s);*transferred=c.transferred;return 1;
}
extern "C" Dword GetSystemFirmwareTable(Dword provider,Dword table,void* buffer,Dword capacity){
    Command c{};c.op=Op::FirmwareRead;c.handle=provider;c.type=table;c.flags=1;
    auto status=Call(c);if(status){SetLastError(Error(status));return 0;}const auto required=c.access;
    if(!buffer||capacity<required)return required;
    for(uint32_t offset=0;offset<required;){c={};c.op=Op::FirmwareRead;c.handle=provider;c.type=table;c.argument=offset;c.length=required-offset<512?required-offset:512;
        status=Call(c);if(status||!c.transferred||c.transferred>c.length){SetLastError(status?Error(status):87);return 0;}
        Copy(static_cast<uint8_t*>(buffer)+offset,c.data,c.transferred);offset+=c.transferred;}
    return required;
}
extern "C" Bool SetFilePointerEx(Handle h,int64_t distance,int64_t* result,Dword method){Command c{};c.op=Op::Seek;c.handle=reinterpret_cast<uint64_t>(h);c.argument=static_cast<uint64_t>(distance);c.share=method;
    const auto s=Call(c);if(s)return Result(s);if(result)*result=static_cast<int64_t>(c.argument);return 1;}
extern "C" Bool GetFileSizeEx(Handle h,int64_t* size){if(!size){SetLastError(87);return 0;}Command c{};c.op=Op::Size;c.handle=reinterpret_cast<uint64_t>(h);const auto s=Call(c);if(s)return Result(s);*size=static_cast<int64_t>(c.argument);return 1;}
extern "C" Bool FlushFileBuffers(Handle h){Command c{};c.op=Op::Flush;c.handle=reinterpret_cast<uint64_t>(h);return Result(Call(c));}
extern "C" Bool CloseHandle(Handle h){Command c{};c.handle=reinterpret_cast<uint64_t>(h);c.op=c.handle>=runtime::FirstHandle?Op::Close:Op::CloseSync;return Result(Call(c));}
extern "C" Dword RegOpenKeyExA(Handle root,const char* path,Dword options,Dword access,Handle* key){
    if(!key || options)return 87;*key=nullptr;Command c{};c.op=Op::RegOpen;c.handle=reinterpret_cast<uint64_t>(root);c.access=access;if(!Text(c.path,path,128))return 87;
    const auto s=Call(c);if(s)return Error(s);*key=reinterpret_cast<Handle>(c.handle);return 0;
}
extern "C" Dword RegCreateKeyExA(Handle root,const char* path,Dword reserved,char* cls,Dword options,Dword access,void* security,Handle* key,Dword* disposition){
    if(!key || reserved || cls || options || security)return 87;*key=nullptr;Command c{};c.op=Op::RegOpen;c.flags=1;c.handle=reinterpret_cast<uint64_t>(root);c.access=access;if(!Text(c.path,path,128))return 87;
    const auto s=Call(c);if(s)return Error(s);*key=reinterpret_cast<Handle>(c.handle);if(disposition)*disposition=c.disposition;return 0;
}
extern "C" Dword RegSetValueExA(Handle h,const char* name,Dword reserved,Dword type,const uint8_t* data,Dword n){
    if(reserved || n>512 || (n && !data))return 87;Command c{};c.op=Op::RegSet;c.handle=reinterpret_cast<uint64_t>(h);c.length=n;c.type=type;if(!Text(c.name,name?name:"",32))return 87;Copy(c.data,data,n);return Error(Call(c));
}
extern "C" Dword RegQueryValueExA(Handle h,const char* name,Dword* reserved,Dword* type,uint8_t* data,Dword* n){
    if(reserved || !n)return 87;Command c{};c.op=Op::RegGet;c.handle=reinterpret_cast<uint64_t>(h);c.length=*n>512?512u:*n;c.flags=data?0u:1u;if(!Text(c.name,name?name:"",32))return 87;
    const auto s=Call(c);if(!s || s==storage::Small){*n=c.length;if(type)*type=c.type;if(!s && data)Copy(data,c.data,c.transferred);}return Error(s);
}
extern "C" Dword RegCloseKey(Handle h){Command c{};c.op=Op::RegClose;c.handle=reinterpret_cast<uint64_t>(h);return Error(Call(c));}
extern "C" Handle CreateMutexA(void* security,Bool initial,const char* name){if(security || name){SetLastError(87);return nullptr;}Command c{};c.op=Op::MutexCreate;c.flags=initial?1u:0u;const auto s=Call(c);if(s){SetLastError(Error(s));return nullptr;}return reinterpret_cast<Handle>(c.handle);}
extern "C" Dword WaitForSingleObject(Handle h,Dword ms){Command c{};c.op=Op::Wait;c.handle=reinterpret_cast<uint64_t>(h);c.argument=ms;const auto s=Call(c);if(s==0 || s==0x80 || s==0x102)return s;SetLastError(Error(s));return UINT32_MAX;}
extern "C" Bool ReleaseMutex(Handle h){Command c{};c.op=Op::Unlock;c.handle=reinterpret_cast<uint64_t>(h);return Result(Call(c));}
extern "C" Bool InitializeCriticalSectionEx(CriticalSection* c,Dword spin,Dword flags){if(!c || flags){SetLastError(87);return 0;}const auto h=CreateMutexA(nullptr,0,nullptr);if(!h)return 0;*c={};c->lockCount=-1;c->semaphore=h;c->spin=spin;return 1;}
extern "C" Bool TryEnterCriticalSection(CriticalSection* c){if(!c || !c->semaphore){SetLastError(87);return 0;}const auto s=WaitForSingleObject(c->semaphore,0);if(s!=0 && s!=0x80)return 0;++c->recursion;++c->lockCount;c->owner=reinterpret_cast<Handle>(uint64_t(GetCurrentThreadId()));return 1;}
extern "C" void EnterCriticalSection(CriticalSection* c){if(!c || !c->semaphore){SetLastError(87);return;}const auto s=WaitForSingleObject(c->semaphore,UINT32_MAX);if(s!=0 && s!=0x80)return;++c->recursion;++c->lockCount;c->owner=reinterpret_cast<Handle>(uint64_t(GetCurrentThreadId()));}
extern "C" void LeaveCriticalSection(CriticalSection* c){if(!c || !c->recursion || reinterpret_cast<uint64_t>(c->owner)!=GetCurrentThreadId()){SetLastError(288);return;}const auto owner=c->owner;--c->lockCount;if(!--c->recursion)c->owner=nullptr;if(!ReleaseMutex(c->semaphore)){++c->lockCount;++c->recursion;c->owner=owner;}}
extern "C" void DeleteCriticalSection(CriticalSection* c){if(!c || c->recursion){SetLastError(87);return;}if(CloseHandle(c->semaphore))*c={};}
}
extern "C" uint32_t GuardianRuntimeError(uint32_t status) noexcept {return guardian::win::Error(status);}
extern "C" bool RuntimeInitialize() noexcept {
    using namespace guardian::win;if(ready || !heap.Initialize(arena,sizeof(arena)))return false;heapMutex=CreateMutexA(nullptr,0,nullptr);if(!heapMutex)return false;ready=true;
    *reinterpret_cast<uint64_t*>(__readgsqword(0x60)+0x30)=reinterpret_cast<uint64_t>(GetProcessHeap());return true;
}
extern "C" bool RuntimeDrainLibraries() noexcept;
extern "C" bool RuntimeShutdown(bool terminating) noexcept {if(!RuntimeDrainLibraries())return false;using namespace guardian::win;const bool ok=ready && (terminating || !heap.Used()) && CloseHandle(heapMutex);ready=false;heapMutex=nullptr;return ok;}
