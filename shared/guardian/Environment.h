#pragma once
#include <stdint.h>
#include <stddef.h>
namespace guardian::environment {
inline constexpr uint64_t Magic=0x314E4556415547ull, InfoGpa=0x13D000;
inline constexpr uint32_t Version=1, ContentVersion=1, User=100, Group=10;
struct Info {
    uint64_t magic=Magic;
    uint32_t version=Version,bytes=sizeof(Info);
    uint64_t seed=0;
    uint32_t ntMajor=10,ntMinor=0,ntBuild=22631,preserve=1;
    uint32_t user=User,group=Group,contentVersion=ContentVersion,reserved=0;
    uint64_t catalogHash=0;
};
static_assert(sizeof(Info)==64);
inline bool Valid(const Info& p) noexcept {
    return p.magic==Magic && p.version==Version && p.bytes==sizeof(p) && p.seed<=UINT32_MAX &&
        p.preserve<=1 && p.user==User && p.group==Group && p.contentVersion==ContentVersion && !p.reserved &&
        ((p.ntMajor==6 && p.ntMinor==1 && p.ntBuild==7601) ||
         (p.ntMajor==10 && !p.ntMinor && (p.ntBuild==19045 || p.ntBuild==22631)));
}
inline uint64_t Hash(const void* data,size_t n) noexcept {
    const auto* b=static_cast<const uint8_t*>(data);uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<n;++i) h=(h^b[i])*1099511628211ull;
    return h;
}
inline bool Equal(const char* a,const char* b) noexcept {
    for(uint32_t i=0;i<128;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;
}
struct Text {
    char bytes[512]{};uint32_t size=0;bool good=true;
    void Add(const char* text) noexcept {for(uint32_t i=0;text[i];++i){if(size>=511){good=false;return;}bytes[size++]=text[i];}}
    void Number(uint64_t value) noexcept {char digits[20]{};uint32_t n=0;do{digits[n++]=char('0'+value%10);value/=10;}while(value);while(n){char t[2]{digits[--n],0};Add(t);}}
};
inline Text Account(const Info& p) noexcept {Text t;t.Add("lab-");t.Number(p.seed);return t;}
inline uint64_t BaseUtc(const Info& p) noexcept {return 133485408000000000ull+(p.seed%365)*864000000000ull;}
inline Text Document(const Info& p,uint32_t which) noexcept {
    Text t;
    if(which==0){t.Add("Guardian synthetic workspace v1\nAccount: ");t.Add(Account(p).bytes);t.Add("\nNT build: ");t.Number(p.ntBuild);t.Add("\nSeed: ");t.Number(p.seed);t.Add("\nThis document contains no imported personal data.\n");}
    else if(which==1){t.Add("item,units\npaper,");t.Number(1+p.seed%90);t.Add("\nink,");t.Number(1+(p.seed/7)%20);t.Add("\n");}
    else {t.Add("Synthetic notes for ");t.Add(Account(p).bytes);t.Add("\nEditable inside this guest.\n");}
    return t;
}
inline const char* DocumentPath(uint32_t which) noexcept {return which==0?"C:\\welcome.txt":which==1?"C:\\budget.csv":"C:\\notes.txt";}
inline bool DocumentAllowed(const char* name) noexcept {return Equal(name,DocumentPath(0))||Equal(name,DocumentPath(1))||Equal(name,DocumentPath(2));}
} // namespace guardian::environment
