#include "Win32Api.h"
#include "../Win32Unicode.h"
#include "guardian/Win32.h"
#include "guardian/Runtime.h"
#include <intrin.h>
extern "C" uint32_t GuardianWin32Call(guardian::win32::Request*) noexcept;
using namespace guardian;using namespace guardian::win;
namespace {
Handle heap=nullptr,mutex=nullptr;char commandA[128]{};bool tlsUsed[64]{};__declspec(thread) void* tlsValues[64]{};
bool Lock(){return mutex&&win::WaitForSingleObject(mutex,UINT32_MAX)==0;}void Unlock(){win::ReleaseMutex(mutex);}
uint32_t Error(uint32_t s){switch(s){case 0:return 0;case 0xC0000035:return 183;case 0xC0000043:return 32;case 0xC000007F:return 112;case 0xC0000018:case 0xC00000A0:return 487;case 0xE1000001:return 131;case 0xC0000120:return 995;case 0xC0000011:return 38;default:return native::RtlNtStatusToDosError(s);}}
Bool Ok(uint32_t s){if(!s)return 1;win::SetLastError(Error(s));return 0;}
template<class T>T Fail(uint32_t error,T value){win::SetLastError(error);return value;}
win32::Parameters* Parameters(){return reinterpret_cast<win32::Parameters*>(*reinterpret_cast<uint64_t*>(__readgsqword(0x60)+0x20));}
void Copy(void* out,const void* in,uint64_t size){for(uint64_t i=0;i<size;++i)static_cast<uint8_t*>(out)[i]=static_cast<const uint8_t*>(in)[i];}
uint32_t Length(const uint16_t* s,uint32_t cap=128){if(!s)return UINT32_MAX;uint32_t i=0;while(i<cap&&s[i])++i;return i==cap?UINT32_MAX:i;}
bool Ascii(const uint16_t* s,char* out,uint32_t cap=128){const auto n=Length(s,cap);if(n==UINT32_MAX)return false;for(uint32_t i=0;i<n;++i){if(s[i]<32||s[i]>126)return false;out[i]=char(s[i]);}out[n]=0;return true;}
bool FromA(const char* in,uint16_t* out,uint32_t cap=128){uint32_t error=0;return nls::Multi(1252,0,in,-1,out,int(cap),error)!=0;}
bool Equal(const uint16_t* a,const uint16_t* b,uint32_t n){for(uint32_t i=0;i<n;++i)if(nls::Upper(a[i])!=nls::Upper(b[i]))return false;return true;}
bool FullPath(const uint16_t* in,uint16_t (&out)[128]){
    const auto n=Length(in);if(n==UINT32_MAX||!n)return false;uint16_t input[256]{};uint32_t used=0;
    if(n>=2&&in[1]==':'){if(n<3||nls::Upper(in[0])!='C'||(in[2]!='\\'&&in[2]!='/'))return false;}
    else if(in[0]=='\\'||in[0]=='/'){input[used++]='C';input[used++]=':';}
    else {const auto* cwd=reinterpret_cast<const uint16_t*>(Parameters()->directory.buffer);const auto count=Length(cwd);if(count==UINT32_MAX)return false;for(uint32_t i=0;i<count;++i)input[used++]=cwd[i];input[used++]='\\';}
    for(uint32_t i=0;i<n;++i)input[used++]=in[i]=='/'?'\\':in[i];if(used<3||input[1]!=':'||input[2]!='\\')return false;
    out[0]='C';out[1]=':';out[2]='\\';uint32_t at=3,depth=0,starts[64]{};
    for(uint32_t i=3;i<used;){if(input[i]=='\\'){++i;continue;}const auto begin=i;while(i<used&&input[i]!='\\')++i;const auto bytes=i-begin;
        if(bytes==1&&input[begin]=='.')continue;if(bytes==2&&input[begin]=='.'&&input[begin+1]=='.'){if(depth){at=starts[--depth];out[at]=0;}continue;}
        if(depth==64)return false;starts[depth++]=at;if(at>3){if(at>=127)return false;out[at++]='\\';}
        for(uint32_t j=begin;j<i;++j){const auto c=input[j];if(c<32||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|'||at>=127)return false;out[at++]=c;}
        if(out[at-1]=='.'||out[at-1]==' ')return false;out[at]=0;}
    out[at]=0;return true;
}
uint32_t PathCall(uint32_t op,const uint16_t* path){uint16_t full[128]{};char narrow[128]{};if(!FullPath(path,full))return native::Invalid;if(!Ascii(full,narrow))return native::Unsupported;
    native::Unicode name;native::RtlInitUnicodeString(&name,full);native::Attributes attributes;attributes.name=reinterpret_cast<uint64_t>(&name);attributes.attributes=0x40;
    win32::Request r;r.op=op;r.args[0]=reinterpret_cast<uint64_t>(&attributes);return GuardianWin32Call(&r);}
struct FileState {Handle handle=nullptr;bool overlapped=false;
#ifdef GUARDIAN_CRT_RUNTIME
bool inherit=false;
#endif
};FileState files[8]{};
FileState* File(Handle h){for(auto& f:files)if(f.handle==h&&h)return &f;return nullptr;}
bool PendingFile(Handle);void DrainFile(Handle);
}
extern "C" int KbMultiByteToWideChar(Dword p,Dword flags,const char* in,int n,uint16_t* out,int cap){
#ifdef GUARDIAN_CRT_RUNTIME
    if(p==3)p=1252;if(!p||p==1252)flags&=~1u;
#endif
    Dword e=0;const auto count=nls::Multi(p,flags,in,n,out,cap,e);if(e)win::SetLastError(e);return count;}
extern "C" int KbWideCharToMultiByte(Dword p,Dword flags,const uint16_t* in,int n,char* out,int cap,const char* replacement,Bool* used){
#ifdef GUARDIAN_CRT_RUNTIME
    if(p==3)p=1252;if(!p||p==1252)flags&=~0x400u;
#endif
    Dword e=0;const auto count=nls::Wide(p,flags,in,n,out,cap,replacement,used,e);if(e)win::SetLastError(e);return count;}
extern "C" Dword KbGetACP(){return 1252;}
extern "C" int KbCompareStringOrdinal(const uint16_t* a,int na,const uint16_t* b,int nb,Bool fold){if(na==-1)na=int(Length(a,1<<20));if(nb==-1)nb=int(Length(b,1<<20));if(!a||!b||na<0||nb<0||na>(1<<20)||nb>(1<<20))return Fail(87,0);
    if(fold){for(int i=0;i<na;++i)if(!nls::CaseSupported(a[i]))return Fail(50,0);for(int i=0;i<nb;++i)if(!nls::CaseSupported(b[i]))return Fail(50,0);}
    for(int i=0;i<na&&i<nb;++i){const auto x=fold?nls::Upper(a[i]):a[i],y=fold?nls::Upper(b[i]):b[i];if(x!=y)return x<y?1:3;}return na==nb?2:(na<nb?1:3);}
extern "C" int KbLCMapStringEx(const uint16_t* locale,Dword flags,const uint16_t* in,int n,uint16_t* out,int cap,void* version,void* reserved,int64_t sort){
    const uint16_t pt[]{'p','t','-','B','R',0};if(version||reserved||sort||(locale&&locale[0]&&(Length(locale,6)!=5||!Equal(locale,pt,5)))||(flags!=0x100&&flags!=0x200)||!in||!n||n< -1||cap<0||(cap&&!out))return Fail(87,0);
    if(n==-1){const auto length=Length(in,1<<20);if(length==UINT32_MAX)return Fail(87,0);n=int(length)+1;}if(n>(1<<20))return Fail(87,0);for(int i=0;i<n;++i)if(!nls::CaseSupported(in[i]))return Fail(50,0);if(!cap)return n;if(cap<n)return Fail(122,0);
    for(int i=0;i<n;++i)out[i]=flags==0x200?nls::Upper(in[i]):nls::Lower(in[i]);return n;}
extern "C" Handle KbGetCurrentProcess(){return reinterpret_cast<Handle>(native::CurrentProcess);}extern "C" Handle KbGetCurrentThread(){return reinterpret_cast<Handle>(native::CurrentThread);}
extern "C" void* KbVirtualAlloc(void* base,uint64_t size,Dword type,Dword protection){const auto s=native::NtAllocateVirtualMemory(KbGetCurrentProcess(),&base,0,&size,type,protection);if(s)return Fail(Error(s),static_cast<void*>(nullptr));return base;}
extern "C" Bool KbVirtualFree(void* base,uint64_t size,Dword type){return Ok(native::NtFreeVirtualMemory(KbGetCurrentProcess(),&base,&size,type));}
extern "C" Bool KbVirtualProtect(void* base,uint64_t size,Dword protection,Dword* old){return Ok(native::NtProtectVirtualMemory(KbGetCurrentProcess(),&base,&size,protection,old));}
extern "C" uint64_t KbVirtualQuery(const void* base,memory::Information* info,uint64_t size){uint64_t returned=0;const auto s=native::NtQueryVirtualMemory(KbGetCurrentProcess(),base,0,info,size,&returned);if(s)return Fail(Error(s),uint64_t(0));return returned;}
extern "C" Handle KbGetProcessHeap(){return heap;}
extern "C" Handle KbHeapCreate(Dword flags,uint64_t initial,uint64_t maximum){if(flags||initial>(1u<<20)||maximum>(1u<<20)||(maximum&&initial>maximum))return Fail(87,static_cast<Handle>(nullptr));const auto h=native::RtlCreateHeap(2,nullptr,maximum?maximum:
#ifdef GUARDIAN_CRT_RUNTIME
    (1u<<20),
#else
    (initial?initial:65536),
#endif
    0,nullptr,nullptr);if(!h)win::SetLastError(8);return h;}
extern "C" Bool KbHeapDestroy(Handle h){if(h==heap)return Fail(87,0);return native::RtlDestroyHeap(h)?Fail(87,0):1;}
extern "C" void* KbHeapAlloc(Handle h,Dword f,uint64_t n){auto* p=native::RtlAllocateHeap(h,f,n);if(!p)win::SetLastError(8);return p;}
extern "C" void* KbHeapReAlloc(Handle h,Dword f,void* p,uint64_t n){auto* out=native::RtlReAllocateHeap(h,f,p,n);if(!out)win::SetLastError(8);return out;}
extern "C" Bool KbHeapFree(Handle h,Dword f,void* p){return native::RtlFreeHeap(h,f,p)?1:Fail(87,0);}
extern "C" uint64_t KbHeapSize(Handle h,Dword f,const void* p){const auto n=native::RtlSizeHeap(h,f,const_cast<void*>(p));if(n==UINT64_MAX)win::SetLastError(87);return n;}
extern "C" Bool KbGetVersionExW(native::VersionInfo* v){return Ok(native::RtlGetVersion(v));}
extern "C" void KbGetSystemInfo(SystemInfo* info){if(!info){win::SetLastError(87);return;}*info={9,0,4096,reinterpret_cast<void*>(memory::Arena),reinterpret_cast<void*>(memory::Arena+memory::ArenaBytes-1),1,1,8664,65536,6,0};}
extern "C" void KbGetNativeSystemInfo(SystemInfo* info){KbGetSystemInfo(info);}
extern "C" Bool KbGlobalMemoryStatusEx(MemoryStatus* out){if(!out||out->length!=sizeof(*out))return Fail(87,0);win32::Request r;r.op=8;const auto s=GuardianWin32Call(&r);if(s)return Ok(s);
    out->total=r.args[0];out->available=r.args[1];out->load=out->total?Dword((out->total-out->available)*100/out->total):0;out->totalPage=out->total;out->availablePage=out->available;out->totalVirtual=r.args[2];out->availableVirtual=r.args[3];out->extended=0;return 1;}
extern "C" void KbGetSystemTimePreciseAsFileTime(FileTime* t){win::GetSystemTimeAsFileTime(t);}
extern "C" uint16_t* KbGetCommandLineW(){return reinterpret_cast<uint16_t*>(Parameters()->command.buffer);}
extern "C" char* KbGetCommandLineA(){return commandA;}
extern "C" Dword KbGetFullPathNameW(const uint16_t* in,Dword cap,uint16_t* out,uint16_t** file){uint16_t full[128]{};if(!FullPath(in,full))return Fail(123,Dword(0));const auto n=Length(full);if(cap<=n)return n+1;if(!out)return Fail(87,Dword(0));Copy(out,full,(n+1)*2);if(file){*file=out;for(uint32_t i=0;i<n;++i)if(out[i]=='\\')*file=out+i+1;}return n;}
extern "C" Dword KbGetFullPathNameA(const char* in,Dword cap,char* out,char** file){uint16_t text[128]{},full[128]{};char narrow[128]{};if(!FromA(in,text)||!FullPath(text,full)||!Ascii(full,narrow))return Fail(123,Dword(0));const auto n=Length(full);if(cap<=n)return n+1;if(!out)return Fail(87,Dword(0));Copy(out,narrow,n+1);if(file){*file=out;for(uint32_t i=0;i<n;++i)if(out[i]=='\\')*file=out+i+1;}return n;}
extern "C" Dword KbGetCurrentDirectoryW(Dword cap,uint16_t* out){const auto* p=reinterpret_cast<const uint16_t*>(Parameters()->directory.buffer);const auto n=Length(p);if(cap<=n)return n+1;if(!out)return Fail(87,Dword(0));Copy(out,p,(n+1)*2);return n;}
extern "C" Dword KbGetCurrentDirectoryA(Dword cap,char* out){Dword e=0;const auto* p=reinterpret_cast<const uint16_t*>(Parameters()->directory.buffer);const auto n=nls::Wide(1252,0,p,-1,nullptr,0,nullptr,nullptr,e);if(!n)return Fail(e,Dword(0));if(cap<uint32_t(n))return Dword(n);if(!out)return Fail(87,Dword(0));nls::Wide(1252,0,p,-1,out,int(cap),nullptr,nullptr,e);return Dword(n-1);}
extern "C" Bool KbSetCurrentDirectoryW(const uint16_t* in){uint16_t full[128]{};if(!FullPath(in,full))return Fail(123,0);const auto s=PathCall(9,full);if(s)return Ok(s);if(!Lock())return Fail(8,0);const auto n=Length(full);Copy(reinterpret_cast<void*>(Parameters()->directory.buffer),full,(n+1)*2);Parameters()->directory.length=uint16_t(n*2);Unlock();return 1;}
extern "C" Bool KbSetCurrentDirectoryA(const char* in){uint16_t text[128]{};return FromA(in,text)?KbSetCurrentDirectoryW(text):Fail(1113,0);}
extern "C" Dword KbGetModuleFileNameA(Handle module,char* out,Dword cap){char text[128]{};win32::Request r;r.op=20;r.args[0]=reinterpret_cast<uint64_t>(module);r.args[1]=reinterpret_cast<uint64_t>(text);r.args[2]=sizeof(text);const auto s=GuardianWin32Call(&r);if(s)return Fail(Error(s),Dword(0));const auto n=Dword(r.args[3]);if(!out||!cap)return Fail(87,Dword(0));const auto copied=n<cap?n:cap-1;Copy(out,text,copied);out[copied]=0;if(n>=cap){win::SetLastError(122);return cap;}return n;}
extern "C" Dword KbGetModuleFileNameW(Handle module,uint16_t* out,Dword cap){char text[128]{};const auto n=KbGetModuleFileNameA(module,text,128);if(!n)return 0;if(!out||!cap)return Fail(87,Dword(0));const auto copied=n<cap?n:cap-1;for(uint32_t i=0;i<copied;++i)out[i]=uint8_t(text[i]);out[copied]=0;if(n>=cap){win::SetLastError(122);return cap;}return n;}
extern "C" Handle KbLoadLibraryW(const uint16_t* path){native::Unicode s;native::RtlInitUnicodeString(&s,path);Handle h=nullptr;const auto result=native::LdrLoadDll(nullptr,nullptr,&s,&h);if(result)win::SetLastError(Error(result));return h;}
extern "C" Handle KbGetModuleHandleW(const uint16_t* path){if(!path)return win::GetModuleHandleA(nullptr);native::Unicode s;native::RtlInitUnicodeString(&s,path);Handle h=nullptr;const auto result=native::LdrGetDllHandle(nullptr,nullptr,&s,&h);if(result)win::SetLastError(126);return h;}
extern "C" Procedure KbGetProcAddress(Handle h,const char* name){const auto p=win::GetProcAddress(h,name);if(!p)win::SetLastError(127);return p;}
#include "Win32Environment.inl"
#include "Win32Files.inl"
#include "Win32Async.inl"
#include "Win32Threads.inl"
#ifdef GUARDIAN_CRT_RUNTIME
extern "C" __declspec(dllimport) void GuardianCrtFlsCleanup();
#include "CrtWin32.inl"
#include "CrtSynchronization.inl"
#endif
extern "C" int KbDllEntry(void*,Dword reason,void*){if(reason==1){mutex=win::CreateMutexA(nullptr,0,nullptr);if(!mutex)return 0;heap=native::RtlCreateHeap(2,nullptr,
#ifdef GUARDIAN_CRT_RUNTIME
    1u<<20,
#else
    65536,
#endif
    0,nullptr,nullptr);if(!heap)return 0;
#ifdef GUARDIAN_CRT_RUNTIME
    *reinterpret_cast<Handle*>(__readgsqword(0x60)+0x30)=heap;
#endif
    Dword e=0;return nls::Wide(1252,0,KbGetCommandLineW(),-1,commandA,sizeof(commandA),nullptr,nullptr,e)!=0;}
    if(reason==0){
#ifdef GUARDIAN_CRT_RUNTIME
        GuardianCrtFlsCleanup();
#endif
        if(heap)native::RtlDestroyHeap(heap);if(mutex)win::CloseHandle(mutex);heap=mutex=nullptr;}return 1;}

#ifdef GUARDIAN_DESKTOP_RUNTIME
#include "DesktopWin32.inl"
#endif
