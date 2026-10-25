#include "SecurityApi.h"
#include <initializer_list>
using namespace guardian;using namespace guardian::win;using namespace guardian::secwin;
extern "C" uint32_t GuardianSecurityCall(security::Request*) noexcept;
extern "C" const int GuardianSecurityAnchor=146;
const void* volatile GuardianSecurityRelocation=&GuardianSecurityAnchor;
namespace {
void Copy(void* d,const void* s,uint32_t n){for(uint32_t i=0;i<n;++i)static_cast<uint8_t*>(d)[i]=static_cast<const uint8_t*>(s)[i];}
bool A(const char* s,char* d,uint32_t cap){if(!s){*d=0;return true;}for(uint32_t i=0;i<cap;++i){if(!s[i]){d[i]=0;return true;}if(s[i]<32||s[i]>126)return false;d[i]=s[i];}return false;}
bool W(const uint16_t* s,char* d,uint32_t cap){if(!s){*d=0;return true;}for(uint32_t i=0;i<cap;++i){if(!s[i]){d[i]=0;return true;}if(s[i]<32||s[i]>126)return false;d[i]=char(s[i]);}return false;}
uint32_t Length(const char* s){uint32_t n=0;while(s[n])++n;return n;}
void Wide(const char* s,uint16_t* d){for(uint32_t i=0;;++i){d[i]=uint8_t(s[i]);if(!s[i])return;}}
bool Equal(const char* a,const char* b){for(uint32_t i=0;i<128;++i){auto x=a[i],y=b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;if(!x)return true;}return false;}
Bool Fail(Dword e){win::SetLastError(e);return 0;}Bool Result(Dword e){return e?Fail(e):1;}
Handle HandleResult(security::Request& r){const auto e=GuardianSecurityCall(&r);if(e){Fail(e);return nullptr;}return reinterpret_cast<Handle>(r.handle);}
bool Token(Handle h,security::Request& r){r.op=security::Op::TokenQuery;r.handle=uint64_t(h);return Result(GuardianSecurityCall(&r))!=0;}
Dword OpenKey(Handle root,const char* sub,Dword options,Dword access,Handle* out,bool create,Dword* disposition){if(!out)return 87;*out=nullptr;security::Request r;r.op=create?security::Op::RegCreate:security::Op::RegOpen;r.handle=uint64_t(root);r.access=access;r.flags=options;if(!A(sub,r.path,128))return 1113;const auto e=GuardianSecurityCall(&r);if(!e){*out=reinterpret_cast<Handle>(r.handle);if(disposition)*disposition=r.returned;}return e;}
Dword Value(Handle key,const char* name,Dword type,const void* in,Dword count,bool set,void* out,Dword* size,Dword* outType){security::Request r;r.op=set?security::Op::RegSet:security::Op::RegQuery;r.handle=uint64_t(key);r.type=type;
    if(!A(name,r.name,32)||(set&&(count>512||(!in&&count)))||(!set&&!size))return 87;
    if(set){r.length=count;if(count)Copy(r.data,in,count);}else r.length=out?(*size>512?512:*size):512;
    const auto e=GuardianSecurityCall(&r);if(!set&&(e==0||e==234)){if(outType)*outType=r.type;const auto cap=*size;*size=r.returned;if(!e&&out&&cap>=r.returned)Copy(out,r.data,r.returned);}return e;}
}
extern "C" Bool SecurityDllEntry(void*,Dword,void*){return 1;}
extern "C" Dword AvRegOpenKeyExA(Handle h,const char* s,Dword o,Dword a,Handle* p){return OpenKey(h,s,o,a,p,false,nullptr);}
extern "C" Dword AvRegOpenKeyExW(Handle h,const uint16_t* s,Dword o,Dword a,Handle* p){char b[128]{};return W(s,b,128)?OpenKey(h,b,o,a,p,false,nullptr):1113;}
extern "C" Dword AvRegCreateKeyExA(Handle h,const char* s,Dword reserved,char* cls,Dword o,Dword a,void* sd,Handle* p,Dword* d){if(reserved||cls||sd)return 50;return OpenKey(h,s,o,a,p,true,d);}
extern "C" Dword AvRegCreateKeyExW(Handle h,const uint16_t* s,Dword reserved,uint16_t* cls,Dword o,Dword a,void* sd,Handle* p,Dword* d){char b[128]{};if(!W(s,b,128))return 1113;return AvRegCreateKeyExA(h,b,reserved,reinterpret_cast<char*>(cls),o,a,sd,p,d);}
extern "C" Dword AvRegSetValueExW(Handle h,const uint16_t* n,Dword reserved,Dword type,const uint8_t* data,Dword bytes){char name[32]{};if(reserved)return 87;if(!W(n,name,32))return 1113;return Value(h,name,type,data,bytes,true,nullptr,nullptr,nullptr);}
extern "C" Dword AvRegSetValueExA(Handle h,const char* n,Dword reserved,Dword type,const uint8_t* data,Dword bytes){if(reserved)return 87;if(type==1||type==2||type==7){uint16_t wide[256]{};if(bytes>256||(!data&&bytes))return 87;for(uint32_t i=0;i<bytes;++i){if(data[i]>127)return 1113;wide[i]=data[i];}return Value(h,n,type,wide,bytes*2,true,nullptr,nullptr,nullptr);}return Value(h,n,type,data,bytes,true,nullptr,nullptr,nullptr);}
extern "C" Dword AvRegQueryValueExW(Handle h,const uint16_t* n,Dword* reserved,Dword* type,uint8_t* data,Dword* size){char name[32]{};if(reserved)return 87;return W(n,name,32)?Value(h,name,0,nullptr,0,false,data,size,type):1113;}
extern "C" Dword AvRegQueryValueExA(Handle h,const char* n,Dword* reserved,Dword* type,uint8_t* data,Dword* size){if(reserved||!size)return 87;uint8_t raw[512]{};Dword bytes=512,t=0;const auto e=Value(h,n,0,nullptr,0,false,raw,&bytes,&t);if(e)return e;if(type)*type=t;const auto count=(t==1||t==2||t==7)?bytes/2:bytes,cap=*size;*size=count;if(data&&cap<count)return 234;if(!data)return 0;if(t==1||t==2||t==7){for(uint32_t i=0;i<count;++i){const auto c=uint32_t(raw[i*2])|(uint32_t(raw[i*2+1])<<8);if(c>127)return 1113;data[i]=uint8_t(c);}}else Copy(data,raw,count);return 0;}
extern "C" Dword AvRegCloseKey(Handle h){const auto root=uint32_t(uint64_t(h));if((root==0x80000001||root==0x80000002)&&(uint64_t(h)==root||uint64_t(h)==(0xFFFFFFFF00000000ull|root)))return 0;security::Request r;r.op=security::Op::Close;r.handle=uint64_t(h);r.flags=1;return GuardianSecurityCall(&r);}
extern "C" Dword AvRegFlushKey(Handle h){security::Request r;r.op=security::Op::RegFlush;r.handle=uint64_t(h);return GuardianSecurityCall(&r);}
extern "C" Dword AvRegNotifyChangeKeyValue(Handle h,Bool subtree,Dword filter,Handle event,Bool asynchronous){if(asynchronous)return 50;(void)event;security::Request r;r.op=security::Op::RegWatch;r.handle=uint64_t(h);r.flags=subtree?1u:0u;r.access=filter;auto error=GuardianSecurityCall(&r);if(error)return error;r.op=security::Op::RegPoll;r.handle=r.other;do{error=GuardianSecurityCall(&r);if(error==997)Sleep(1);}while(error==997);return error;}
extern "C" Dword AvRegDeleteValueA(Handle h,const char* name){security::Request r;r.op=security::Op::RegDeleteValue;r.handle=uint64_t(h);if(!A(name,r.name,32))return 1113;return GuardianSecurityCall(&r);}
extern "C" Dword AvRegDeleteValueW(Handle h,const uint16_t* name){char n[32]{};return W(name,n,32)?AvRegDeleteValueA(h,n):1113;}
extern "C" Dword AvRegDeleteKeyA(Handle h,const char* name){security::Request r;r.op=security::Op::RegDeleteKey;r.handle=uint64_t(h);if(!name||!A(name,r.path,128))return 87;return GuardianSecurityCall(&r);}
extern "C" Dword AvRegDeleteKeyW(Handle h,const uint16_t* name){char n[128]{};return W(name,n,128)?AvRegDeleteKeyA(h,n):1113;}
extern "C" Dword AvRegEnumKeyExW(Handle h,Dword index,uint16_t* name,Dword* chars,Dword* reserved,uint16_t* cls,Dword* clsChars,void* time){if(!chars||reserved||cls||clsChars||time)return 50;security::Request r;r.op=security::Op::RegEnum;r.handle=uint64_t(h);r.index=index;const auto e=GuardianSecurityCall(&r);if(e)return e;const auto n=Length(r.name),cap=*chars;*chars=n;if(!name||cap<=n)return 234;Wide(r.name,name);return 0;}
extern "C" Dword AvRegEnumValueW(Handle h,Dword index,uint16_t* name,Dword* chars,Dword* reserved,Dword* type,uint8_t* data,Dword* bytes){if(!chars||reserved||(data&&!bytes))return 87;security::Request r;r.op=security::Op::RegEnum;r.handle=uint64_t(h);r.index=index;r.flags=1;const auto e=GuardianSecurityCall(&r);if(e)return e;const auto n=Length(r.name),cap=*chars;*chars=n;if(type)*type=r.type;const auto count=bytes?*bytes:0;if(bytes)*bytes=r.returned;if(!name||cap<=n||(data&&count<r.returned))return 234;Wide(r.name,name);if(data)Copy(data,r.data,r.returned);return 0;}
extern "C" Bool AvOpenProcessToken(Handle process,Dword access,Handle* token){if(!token)return Fail(87);*token=nullptr;security::Request r;r.op=security::Op::TokenOpen;r.other=uint64_t(process);r.access=access;*token=HandleResult(r);return *token!=nullptr;}
extern "C" Bool AvDuplicateTokenEx(Handle token,Dword access,void* attributes,Dword level,Dword type,Handle* out){if(!out||attributes)return Fail(87);*out=nullptr;security::Request r;r.op=security::Op::TokenDuplicate;r.handle=uint64_t(token);r.access=access;r.flags=level;r.type=type;*out=HandleResult(r);return *out!=nullptr;}
extern "C" Bool AvDuplicateToken(Handle token,Dword level,Handle* out){return AvDuplicateTokenEx(token,0,nullptr,level,2,out);}
#include "SecurityIdentity.inl"
#include "SecurityServices.inl"
#include "SecurityEvents.inl"
