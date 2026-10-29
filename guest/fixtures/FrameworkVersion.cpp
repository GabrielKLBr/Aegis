#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
extern "C" const int FrameworkVersionAnchor=1411;
const void* volatile FrameworkVersionRelocation=&FrameworkVersionAnchor;
namespace {
bool Range(uint32_t at,uint32_t n,uint32_t total){return at<=total&&n<=total-at;}
uint16_t U16(const BYTE* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
uint32_t U32(const BYTE* p){return uint32_t(U16(p))|uint32_t(U16(p+2))<<16;}
uint32_t Align(uint32_t n){return (n+3)&~3u;}
BOOL Error(DWORD error){SetLastError(error);return FALSE;}
void Copy(void* to,const void* from,uint32_t n){for(uint32_t i=0;i<n;++i)static_cast<BYTE*>(to)[i]=static_cast<const BYTE*>(from)[i];}
bool Wide(const char* text,wchar_t* out){if(!text)return false;for(uint32_t i=0;i<128;++i){const auto c=BYTE(text[i]);if(!c)return true;if(i==127||c>126)return false;out[i]=c;}return false;}
struct File {
    BYTE* data=nullptr;uint32_t bytes=0;
    ~File(){if(data)HeapFree(GetProcessHeap(),0,data);}
    bool Open(LPCWSTR path){HANDLE file=CreateFileW(path,GENERIC_READ,7,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size{};bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=(1<<20);
        if(!ok){CloseHandle(file);Error(ERROR_NOT_SUPPORTED);return false;}bytes=uint32_t(size.QuadPart);data=static_cast<BYTE*>(HeapAlloc(GetProcessHeap(),0,bytes));
        if(!data){CloseHandle(file);Error(ERROR_NOT_ENOUGH_MEMORY);return false;}
        for(uint32_t at=0;ok&&at<bytes;){const auto n=bytes-at>512?512u:bytes-at;DWORD got=0;ok=ReadFile(file,data+at,n,&got,nullptr)&&got==n;at+=n;}
        const auto error=GetLastError();CloseHandle(file);if(!ok)Error(error?error:ERROR_READ_FAULT);return ok;
    }
    const BYTE* At(uint32_t rva,uint32_t n) const {
        const auto pe=U32(data+60),optional=pe+24;const auto count=U16(data+pe+6),width=U16(data+pe+20);const auto headers=U32(data+optional+60);
        if(rva<headers)return Range(rva,n,headers)&&Range(rva,n,bytes)?data+rva:nullptr;
        for(uint32_t i=0;i<count;++i){const auto* s=data+optional+width+i*40;const auto va=U32(s+12),rawBytes=U32(s+16),raw=U32(s+20);
            if(rva>=va&&Range(rva-va,n,rawBytes)&&Range(raw,rawBytes,bytes))return data+raw+(rva-va);}
        return nullptr;
    }
    const BYTE* Version(uint32_t& length) const {
        length=0;if(bytes<64||U16(data)!=0x5A4D)return nullptr;const auto pe=U32(data+60);
        if(!Range(pe,264,bytes)||U32(data+pe)!=0x4550||U16(data+pe+24)!=0x20B)return nullptr;
        const auto optional=pe+24;const auto count=U16(data+pe+6),width=U16(data+pe+20);
        if(!count||count>16||width<240||!Range(optional,uint32_t(width)+count*40,bytes)||U32(data+optional+108)<3)return nullptr;
        const auto base=U32(data+optional+128),size=U32(data+optional+132);const auto* directory=At(base,size);if(!directory||size<16||size>65536)return nullptr;
        uint32_t offset=0;
        for(uint32_t depth=0;depth<3;++depth){if(!Range(offset,16,size))return nullptr;const auto* h=directory+offset;const uint32_t named=U16(h+12),ids=U16(h+14),total=named+ids;
            if(total>128||(depth==2&&(named||ids!=1))||!Range(offset+16,total*8,size))return nullptr;bool found=false;uint32_t next=0;
            for(uint32_t i=named;i<total;++i){const auto* item=h+16+i*8;if(depth==0&&U32(item)!=16)continue;if(depth==1&&U32(item)!=1)continue;next=U32(item+4);found=true;break;}
            if(!found)return nullptr;if(depth<2){if(!(next>>31))return nullptr;offset=next&0x7FFFFFFF;}
            else {if(next>>31||!Range(next,16,size))return nullptr;const auto* leaf=directory+next;length=U32(leaf+4);if(length<6||length>65535)return nullptr;return At(U32(leaf),length);}
        }return nullptr;
    }
};
struct Node {const BYTE* data=nullptr;uint32_t bytes=0,valueAt=0,valueBytes=0,children=0,units=0;uint16_t type=0;};
bool Parse(const BYTE* data,uint32_t available,Node& node){
    if(available<6)return false;node={};node.data=data;node.bytes=U16(data);node.units=U16(data+2);node.type=U16(data+4);
    if(node.bytes<8||node.bytes>available||node.type>1)return false;uint32_t at=6;bool end=false;
    for(uint32_t n=0;n<128&&Range(at,2,node.bytes);++n,at+=2)if(!U16(data+at)){at+=2;end=true;break;}
    if(!end)return false;node.valueAt=Align(at);node.valueBytes=node.units*(node.type?2u:1u);
    if(!Range(node.valueAt,node.valueBytes,node.bytes))return false;node.children=Align(node.valueAt+node.valueBytes);return node.children<=node.bytes+3;
}
bool Key(const Node& node,const wchar_t* text,uint32_t n){uint32_t at=6;for(uint32_t i=0;i<n;++i){if(!Range(at,2,node.bytes))return false;auto a=U16(node.data+at),b=uint16_t(text[i]);if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)return false;at+=2;}return Range(at,2,node.bytes)&&!U16(node.data+at);}
bool Root(const BYTE* bytes,uint32_t size,Node& node){return Parse(bytes,size,node)&&Key(node,L"VS_VERSION_INFO",15)&&node.type==0&&node.valueBytes>=52&&U32(bytes+node.valueAt)==0xFEEF04BD;}
DWORD Size(LPCWSTR name,DWORD* handle){if(handle)*handle=0;File file;if(!file.Open(name))return 0;uint32_t bytes=0;const auto* version=file.Version(bytes);Node root;if(!version||!Root(version,bytes,root)){Error(ERROR_RESOURCE_DATA_NOT_FOUND);return 0;}return bytes;}
BOOL Info(LPCWSTR name,DWORD length,void* out){if(!out)return Error(ERROR_INVALID_PARAMETER);File file;if(!file.Open(name))return FALSE;uint32_t bytes=0;const auto* version=file.Version(bytes);Node root;if(!version||!Root(version,bytes,root))return Error(ERROR_RESOURCE_DATA_NOT_FOUND);if(length<bytes)return Error(ERROR_INSUFFICIENT_BUFFER);Copy(out,version,bytes);return TRUE;}
}
extern "C" BOOL WINAPI FrameworkVersionEntry(HINSTANCE,DWORD,LPVOID){return TRUE;}
extern "C" DWORD WINAPI FwGetFileVersionInfoSizeW(LPCWSTR name,LPDWORD handle){return Size(name,handle);}
extern "C" DWORD WINAPI FwGetFileVersionInfoSizeA(LPCSTR name,LPDWORD handle){wchar_t text[128]{};if(!Wide(name,text)){Error(ERROR_NOT_SUPPORTED);return 0;}return Size(text,handle);}
extern "C" BOOL WINAPI FwGetFileVersionInfoW(LPCWSTR name,DWORD,DWORD length,LPVOID out){return Info(name,length,out);}
extern "C" BOOL WINAPI FwGetFileVersionInfoA(LPCSTR name,DWORD,DWORD length,LPVOID out){wchar_t text[128]{};return Wide(name,text)?Info(text,length,out):Error(ERROR_NOT_SUPPORTED);}
extern "C" DWORD WINAPI FwGetFileVersionInfoSizeExW(DWORD flags,LPCWSTR name,LPDWORD handle){if(flags&~3u){Error(ERROR_NOT_SUPPORTED);return 0;}return Size(name,handle);}
extern "C" DWORD WINAPI FwGetFileVersionInfoSizeExA(DWORD flags,LPCSTR name,LPDWORD handle){if(flags&~3u){Error(ERROR_NOT_SUPPORTED);return 0;}return FwGetFileVersionInfoSizeA(name,handle);}
extern "C" BOOL WINAPI FwGetFileVersionInfoExW(DWORD flags,LPCWSTR name,DWORD,DWORD length,LPVOID out){return flags&~3u?Error(ERROR_NOT_SUPPORTED):Info(name,length,out);}
extern "C" BOOL WINAPI FwGetFileVersionInfoExA(DWORD flags,LPCSTR name,DWORD,DWORD length,LPVOID out){return flags&~3u?Error(ERROR_NOT_SUPPORTED):FwGetFileVersionInfoA(name,0,length,out);}
extern "C" BOOL WINAPI FwVerQueryValueW(LPCVOID block,LPCWSTR path,LPVOID* out,PUINT length){
    if(out)*out=nullptr;if(length)*length=0;if(!block||!path||!out||!length)return Error(ERROR_INVALID_PARAMETER);
    const auto* bytes=static_cast<const BYTE*>(block);Node node;if(!Root(bytes,U16(bytes),node))return Error(ERROR_INVALID_DATA);
    uint32_t at=0;if(path[at]=='\\')++at;
    for(uint32_t depth=0;path[at];++depth){if(depth>=4)return Error(ERROR_NOT_SUPPORTED);const auto start=at;while(path[at]&&path[at]!='\\'){if(at-start>=127)return Error(ERROR_INVALID_PARAMETER);++at;}if(at==start)return Error(ERROR_INVALID_PARAMETER);
        bool found=false;for(uint32_t offset=node.children;offset<node.bytes;){Node child;if(!Parse(node.data+offset,node.bytes-offset,child))return Error(ERROR_INVALID_DATA);if(Key(child,path+start,at-start)){node=child;found=true;break;}offset+=Align(child.bytes);}
        if(!found)return Error(ERROR_RESOURCE_NAME_NOT_FOUND);if(path[at]=='\\')++at;
    }
    *out=const_cast<BYTE*>(node.data+node.valueAt);*length=node.type?node.units:node.valueBytes;return TRUE;
}
extern "C" BOOL WINAPI FwVerQueryValueA(LPCVOID block,LPCSTR path,LPVOID* out,PUINT length){wchar_t text[128]{};if(!Wide(path,text))return Error(ERROR_NOT_SUPPORTED);if(!FwVerQueryValueW(block,text,out,length))return FALSE;
    // Binary fixed information and translations have the same ANSI/Wide ABI.
    if(text[0]=='\\'&&!text[1])return TRUE;if(text[1]=='V'||text[1]=='v')return TRUE;*out=nullptr;*length=0;return Error(ERROR_NOT_SUPPORTED);}
