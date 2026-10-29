#include "Win32Api.h"
#include "guardian/FrameworkFiles.h"
#include "guardian/PeVolume.h"
#include "guardian/Win32.h"
extern "C" uint32_t GuardianWin32Call(guardian::win32::Request*) noexcept;
using namespace guardian::win;
extern "C" Handle KbCreateFileW(const uint16_t*,Dword,Dword,void*,Dword,Dword,Handle);
extern "C" Bool KbGetFileSizeEx(Handle,int64_t*);
extern "C" Bool KbReadFile(Handle,void*,Dword,Dword*,void*);
extern "C" Bool KbWriteFile(Handle,const void*,Dword,Dword*,void*);
extern "C" Bool KbCloseHandle(Handle);
extern "C" Bool KbDeleteFileW(const uint16_t*);
namespace {
struct File {Handle value=nullptr;~File(){if(value&&value!=reinterpret_cast<Handle>(UINT64_MAX))KbCloseHandle(value);}};
Bool Error(Dword error){SetLastError(error);return 0;}
}
extern "C" Bool FwCopyFileW(const uint16_t* source,const uint16_t* target,Bool failIfExists){
    if(!source||!target)return Error(87);
    char input[128]{},canonical[128]{};uint32_t length=0;
    for(;length<127&&source[length];++length){if(source[length]>126)return Error(50);input[length]=char(source[length]);}
    if(length==127||!guardian::pevolume::Canonical(input,canonical)||!guardian::pevolume::Prefix(canonical,"c:\\app\\"))return Error(50);
    char destination[128]{},targetPath[128]{};uint32_t targetChars=0;for(;targetChars<127&&target[targetChars];++targetChars){if(target[targetChars]>126)return Error(50);destination[targetChars]=char(target[targetChars]);}
    if(targetChars==127)return Error(206);if(guardian::pevolume::Canonical(destination,targetPath)&&guardian::pevolume::Equal(canonical,targetPath))return Error(32);
    File from;from.value=KbCreateFileW(source,0x80000000u,7,nullptr,3,0x80,nullptr);
    if(from.value==reinterpret_cast<Handle>(UINT64_MAX))return 0;int64_t size=0;
    if(!KbGetFileSizeEx(from.value,&size))return 0;if(size<0||size>(8<<20))return Error(50);
    uint8_t buffer[512]{};guardian::frameworks::FileReference reference;reference.fileBytes=uint64_t(size);
    for(uint32_t i=0;i<128;++i)reference.source[i]=canonical[i];reference.hash=14695981039346656037ull;
    for(uint32_t at=0;at<uint64_t(size);){const auto n=uint64_t(size)-at>512?512u:uint32_t(size)-at;Dword got=0;
        if(!KbReadFile(from.value,buffer,n,&got,nullptr)||got!=n)return Error(30);
        for(uint32_t i=0;i<n;++i){reference.hash^=buffer[i];reference.hash*=1099511628211ull;}at+=n;
    }
    if(size>512){guardian::frameworks::FileReference package;guardian::win32::Request request;request.op=80;
        request.args[0]=reinterpret_cast<uint64_t>(canonical);request.args[1]=reinterpret_cast<uint64_t>(&package);
        if(GuardianWin32Call(&request))return Error(50);if(package.fileBytes!=uint64_t(size)||package.hash!=reference.hash)return Error(13);reference=package;}
    File to;to.value=KbCreateFileW(target,0x40000000u,7,nullptr,failIfExists?1u:2u,0x80,nullptr);
    if(to.value==reinterpret_cast<Handle>(UINT64_MAX))return 0;
    Dword written=0;const void* bytes=size>512?static_cast<const void*>(&reference):buffer;const auto n=size>512?uint32_t(sizeof(reference)):uint32_t(size);
    if(!KbWriteFile(to.value,bytes,n,&written,nullptr)||written!=n){const auto error=GetLastError();KbCloseHandle(to.value);to.value=nullptr;KbDeleteFileW(target);return Error(error?error:29);}
    return 1;
}
extern "C" Bool FwCopyFileA(const char* source,const char* target,Bool failIfExists){
    if(!source||!target)return Error(87);uint16_t a[128]{},b[128]{};
    const auto widen=[](const char* in,uint16_t* out){for(uint32_t i=0;i<128;++i){const auto c=uint8_t(in[i]);if(!c)return true;if(i==127||c>126)return false;out[i]=c;}return false;};
    return widen(source,a)&&widen(target,b)?FwCopyFileW(a,b,failIfExists):Error(50);
}
