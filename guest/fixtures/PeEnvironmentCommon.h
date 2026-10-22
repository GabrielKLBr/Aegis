#pragma once
#include "PeRuntimeApi.h"
#include "guardian/Environment.h"
#include "guardian/FileTimes.h"
#include "guardian/Runtime.h"
namespace guardian::envapp {
using namespace guardian::win;
using environment::Info;using environment::Text;
inline Handle Invalid(){return reinterpret_cast<Handle>(UINT64_MAX);}
inline bool Console(const char* text){uint32_t n=0,done=0;while(text[n])++n;return WriteFile(GetStdHandle(uint32_t(-11)),text,n,&done,nullptr)&&done==n;}
inline uint32_t Fail(uint32_t code){Text t;t.Add("12E FAIL ");t.Number(code);t.Add("\n");Console(t.bytes);return code;}
inline bool Read(const char* path,uint8_t* data,uint32_t capacity,uint32_t& size){
    const auto file=CreateFileA(path,0x80000000,3,nullptr,3,0,nullptr);if(file==Invalid())return false;
    int64_t bytes=0;uint32_t got=0;const bool ok=GetFileSizeEx(file,&bytes)&&bytes>=0&&uint64_t(bytes)<=capacity&&ReadFile(file,data,uint32_t(bytes),&got,nullptr)&&got==bytes;
    const bool closed=CloseHandle(file)!=0;size=got;return ok&&closed;
}
inline bool SetDates(Handle file,uint64_t creation,uint64_t access,uint64_t write){
    const FileTime c{uint32_t(creation),uint32_t(creation>>32)},a{uint32_t(access),uint32_t(access>>32)},w{uint32_t(write),uint32_t(write>>32)};
    return SetFileTime(file,&c,&a,&w)!=0;
}
inline bool Write(const char* path,const void* bytes,uint32_t size,uint64_t initialDate=0,bool flush=false){
    const auto file=CreateFileA(path,0xC0000000,3,nullptr,2,0,nullptr);if(file==Invalid())return false;
    uint32_t done=0;bool ok=WriteFile(file,bytes,size,&done,nullptr)&&done==size;
    if(ok&&initialDate){
        ok=SetDates(file,initialDate-864000000000ull,initialDate,initialDate);
        FileTime creation{},access{},write{};if(ok)ok=GetFileTime(file,&creation,&access,&write)!=0;
        const uint64_t c=uint64_t(creation.low)|(uint64_t(creation.high)<<32),a=uint64_t(access.low)|(uint64_t(access.high)<<32),w=uint64_t(write.low)|(uint64_t(write.high)<<32);
        ok=ok&&c==initialDate-864000000000ull&&a==initialDate&&w==initialDate;
    }
    if(ok&&flush)ok=FlushFileBuffers(file)!=0;
    const bool closed=CloseHandle(file)!=0;return ok&&closed;
}
inline bool WriteText(const char* path,const Text& text,uint64_t date=0,bool flush=false){return text.good&&Write(path,text.bytes,text.size,date,flush);}
inline bool Same(const void* a,const void* b,uint32_t size){const auto* x=static_cast<const uint8_t*>(a);const auto* y=static_cast<const uint8_t*>(b);for(uint32_t i=0;i<size;++i)if(x[i]!=y[i])return false;return true;}
inline bool ReadText(const char* path,Text& text){text={};if(!Read(path,reinterpret_cast<uint8_t*>(text.bytes),511,text.size))return false;text.bytes[text.size]=0;return true;}
inline Text Marker(const Info& info){Text t;t.Add("GuardianProfile/1\nsynthetic=true\nseed=");t.Number(info.seed);t.Add("\nnt=");t.Number(info.ntBuild);t.Add("\npreserve=");t.Number(info.preserve);t.Add("\ncatalog=");t.Number(info.catalogHash);t.Add("\n");return t;}
inline Text Catalog(const Info& info){Text t;t.Add("GuardianCatalog/1\nGuardian Docs 1\nimage=guardian-docs.exe\nbacking=immutable guest catalog\nhash64=");t.Number(info.catalogHash);t.Add("\nformats=.txt,.csv\n");return t;}
inline Text Shortcut(){Text t;t.Add("GuardianShortcut/1\nC:\\welcome.txt\n");return t;}
inline bool Registry(Handle& key,bool create){
    const auto root=reinterpret_cast<Handle>(runtime::CurrentUser);
    if(create){uint32_t disposition=0;return RegCreateKeyExA(root,"Lab",0,nullptr,0,0x2001F,nullptr,&key,&disposition)==0;}
    return RegOpenKeyExA(root,"Lab",0,1,&key)==0;
}
inline bool SetValue(Handle key,const char* name,const void* bytes,uint32_t size,uint32_t type=3){return RegSetValueExA(key,name,0,type,static_cast<const uint8_t*>(bytes),size)==0;}
inline bool Value(Handle key,const char* name,void* bytes,uint32_t capacity,uint32_t& size,uint32_t expected=3){uint32_t type=0;size=capacity;return RegQueryValueExA(key,name,nullptr,&type,static_cast<uint8_t*>(bytes),&size)==0&&type==expected;}
struct HistoryEvent {uint64_t utc=0,sequence=0;uint32_t document=0,reserved=0;};
struct History {uint64_t magic=0x31545349484547ull;uint32_t version=1,count=0,visits=0,next=0;uint64_t sequence=0;HistoryEvent events[12]{};};
static_assert(sizeof(History)==320);
inline bool ValidHistory(const History& h){
    if(h.magic!=0x31545349484547ull||h.version!=1||h.count>12||h.next>=12||h.visits>1000000||h.sequence<h.count)return false;
    for(uint32_t i=0;i<h.count;++i){const auto& e=h.events[i];if(!e.utc||!e.sequence||e.sequence>h.sequence||e.document>2||e.reserved)return false;}return true;
}
} // namespace guardian::envapp
