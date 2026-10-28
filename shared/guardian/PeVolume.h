#pragma once
#include "Abi.h"
#include "NtCompatibility.h"
namespace guardian::pevolume {
inline constexpr uint64_t Magic=0x314C4F56504547ull,InfoMagic=0x31464E49504547ull;
inline constexpr uint64_t InfoGpa=0x1F2000,SourceGpa=0x1000000;
inline constexpr uint32_t Version=1,MaxFiles=32,LegacyFiles=16,MaxAliases=16,MaxAssemblies=8,MaxBytes=64u<<20,MaxFileBytes=8u<<20;
enum class Kind:uint32_t {Pe=1,Manifest=2,Data=3};
struct File {char path[128]{};uint32_t offset=0,bytes=0;Kind kind{};uint32_t assembly=0;uint64_t hash=0;};
struct Alias {char name[64]{},target[128]{};};
struct Assembly {char name[64]{};uint32_t version[4]{};char directory[128]{};};
struct Header {
    uint64_t magic=Magic;
    uint32_t version=Version,headerBytes=sizeof(Header),bytes=0,profile=1,files=0,root=0,aliases=0,assemblies=0;
    uint64_t hash=0;
    File entries[LegacyFiles]{};Alias redirects[MaxAliases]{};Assembly identities[MaxAssemblies]{};
};
struct Info {uint64_t magic=InfoMagic,boot=0;uint32_t version=Version,bytes=sizeof(Info),volumeBytes=0,reserved=0;uint64_t hash=0;};
static_assert(sizeof(File)==152&&sizeof(Alias)==192&&sizeof(Assembly)==208&&sizeof(Header)==7216&&sizeof(Info)==40);
inline constexpr uint32_t ExtendedBytes=sizeof(Header)+(MaxFiles-LegacyFiles)*sizeof(File);
inline const File& FileAt(const Header& h,uint32_t i) noexcept {
    return i<LegacyFiles?h.entries[i]:*reinterpret_cast<const File*>(reinterpret_cast<const uint8_t*>(&h)+sizeof(Header)+(i-LegacyFiles)*sizeof(File));
}
inline uint64_t Hash(const uint8_t* data,uint32_t count,bool volume=false) noexcept {
    uint64_t h=14695981039346656037ull;
    for(uint32_t i=0;i<count;++i){h^=volume&&i>=40&&i<48?0:data[i];h*=1099511628211ull;}return h;
}
template<size_t N> inline bool Text(const char (&text)[N],bool empty=false) noexcept {
    bool end=false;for(size_t i=0;i<N;++i){const auto ch=static_cast<uint8_t>(text[i]);if(!ch)end=true;else if(end||ch<32||ch>126)return false;}
    return end&&(empty||text[0]);
}
inline bool Equal(const char* a,const char* b) noexcept {for(uint32_t i=0;i<128;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
inline bool Prefix(const char* a,const char* prefix) noexcept {for(uint32_t i=0;i<128;++i){if(!prefix[i])return true;if(a[i]!=prefix[i])return false;}return false;}
inline bool Suffix(const char* name,const char* suffix) noexcept {uint32_t length=0,tail=0;while(length<128&&name[length])++length;while(tail<128&&suffix[tail])++tail;return length<128&&tail<128&&length>=tail&&Equal(name+length-tail,suffix);}
inline bool DataPath(const char* name,uint32_t version) noexcept {return Prefix(name,"c:\\app\\")&&(Suffix(name,".txt")||Suffix(name,".ico")||Suffix(name,".dat")||Suffix(name,".bin")||version==2&&(Suffix(name,".cfg")||Suffix(name,".der")||Suffix(name,".png")||Suffix(name,".bmp")||Suffix(name,".jpg")||Suffix(name,".jpeg")||Suffix(name,".wav")));}
inline bool Canonical(const char* text,char* out,bool directory=false) noexcept {
    if(!text)return false;uint32_t n=0,component=0;
    for(;n<127&&text[n];++n){char ch=text[n];if(ch>='A'&&ch<='Z')ch=char(ch+32);if(ch=='/')ch='\\';
        if(ch<32||ch>126||ch=='"'||ch=='*'||ch=='?'||ch=='<'||ch=='>'||ch=='|'||(ch==':'&&n!=1))return false;
        if(ch=='\\'){if(n>2&&(!component||out[n-1]=='.'||out[n-1]==' '))return false;component=0;}else ++component;
        out[n]=ch;
    }out[n]=0;if(n==127&&text[n])return false;
    if(!n||out[n-1]=='.'||out[n-1]==' ')return false;
    if(directory&&out[n-1]!='\\'){if(n==127)return false;out[n++]='\\';out[n]=0;}
    if(!directory&&!component)return false;
    return Prefix(out,"c:\\app\\")||Prefix(out,"c:\\windows\\system32\\")||Prefix(out,"c:\\windows\\winsxs\\");
}
inline bool Valid(const uint8_t* data,uint32_t bytes) noexcept {
    if(!data||bytes<sizeof(Header)||bytes>MaxBytes)return false;
    const auto& h=*reinterpret_cast<const Header*>(data);
    const auto headerBytes=h.version==1?uint32_t(sizeof(h)):h.version==2?ExtendedBytes:0u;
    if(h.magic!=Magic||!headerBytes||h.headerBytes!=headerBytes||bytes<headerBytes||h.bytes!=bytes||!nt::Find(h.profile).id||
        !h.files||h.files>(h.version==1?LegacyFiles:MaxFiles)||h.root>=h.files||h.aliases>MaxAliases||h.assemblies>MaxAssemblies||Hash(data,bytes,true)!=h.hash)return false;
    uint32_t previous=(headerBytes+4095)&~4095u;
    for(uint32_t i=0;i<h.files;++i){const auto& f=FileAt(h,i);char normalized[128]{};
        if(!Text(f.path)||!Canonical(f.path,normalized)||!Equal(normalized,f.path)||f.assembly>h.assemblies||
            (f.kind!=Kind::Pe&&f.kind!=Kind::Manifest&&f.kind!=Kind::Data)||!f.bytes||f.bytes>(f.kind==Kind::Manifest?4096u:MaxFileBytes)||
            (f.offset&4095)||f.offset<previous||f.offset>bytes||f.bytes>bytes-f.offset||Hash(data+f.offset,f.bytes)!=f.hash)return false;
        if(f.kind==Kind::Data&&!DataPath(f.path,h.version))return false;
        if(f.assembly&&!Prefix(f.path,h.identities[f.assembly-1].directory))return false;
        for(uint32_t j=0;j<i;++j)if(Equal(f.path,FileAt(h,j).path))return false;
        previous=f.offset+f.bytes;
    }
    if(FileAt(h,h.root).kind!=Kind::Pe)return false;
    for(uint32_t i=0;i<h.assemblies;++i){const auto& a=h.identities[i];char normalized[128]{};
        if(!Text(a.name)||!Text(a.directory)||!Canonical(a.directory,normalized,true)||!Equal(normalized,a.directory)||!Prefix(a.directory,"c:\\windows\\winsxs\\"))return false;
        for(auto v:a.version)if(v>65535)return false;
        for(uint32_t j=0;j<i;++j){const auto& other=h.identities[j];if(Equal(a.directory,other.directory))return false;
            bool same=Equal(a.name,other.name);for(uint32_t v=0;v<4;++v)if(a.version[v]!=other.version[v])same=false;if(same)return false;}
    }
    for(uint32_t i=0;i<h.aliases;++i){const auto& a=h.redirects[i];char normalized[128]{};
        if(!Text(a.name)||(!Prefix(a.name,"api-ms-")&&!Prefix(a.name,"ext-ms-"))||!Text(a.target)||!Canonical(a.target,normalized)||!Equal(normalized,a.target))return false;
        bool exists=false;for(uint32_t j=0;j<h.files;++j)if(FileAt(h,j).kind==Kind::Pe&&Equal(a.target,FileAt(h,j).path))exists=true;
        if(!exists)return false;for(uint32_t j=0;j<i;++j)if(Equal(a.name,h.redirects[j].name))return false;
    }return true;
}
}
