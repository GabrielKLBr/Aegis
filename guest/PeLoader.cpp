#include "PeLoader.h"
#include "guardian/Telemetry.h"
namespace guardian::pe {
namespace {
constexpr uint32_t ReadFlag=0x40000000,WriteFlag=0x80000000,ExecuteFlag=0x20000000;
uint16_t U16(const uint8_t* p) noexcept {return uint16_t(p[0]|(uint16_t(p[1])<<8));}
uint32_t U32(const uint8_t* p) noexcept {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint64_t U64(const uint8_t* p) noexcept {return uint64_t(U32(p))|(uint64_t(U32(p+4))<<32);}
bool Range(uint32_t at,uint32_t bytes,uint32_t limit) noexcept {return at<=limit && bytes<=limit-at;}
uint32_t Pages(uint32_t n) noexcept {return (n+4095)&~4095u;}
char Lower(char c) noexcept {return c>='A' && c<='Z'?char(c+('a'-'A')):c;}
bool Equal(const char* a,const char* b,bool fold=false) noexcept {
    for(uint32_t i=0;i<peprofile::SymbolCharacters;++i){const auto x=fold?Lower(a[i]):a[i],y=fold?Lower(b[i]):b[i];if(x!=y)return false;if(!x)return true;}return false;
}
bool Name(const char* s) noexcept {
    if(!s || !s[0])return false;
    for(uint32_t i=0;i<(peprofile::Extended?128u:64u);++i){const char c=s[i];if(!c)return true;
        if constexpr(peprofile::Extended){if(c=='\\'||c==':'||c=='/')continue;}
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))return false;}
    return false;
}
}
bool Loader::Configure(const Backend& b,void* c,const Source* s,uint32_t count,uint64_t arena,bool handlers) noexcept {
    for(const auto& m:modules_)if(m.mapped)return Fail(Error::InvalidState);
    if(!b.map || !b.read || !b.write || !b.protect || !b.unmap || !s || !count || count>MaxModules ||
        !arena || (arena&0xFFFF) || arena>0x0000800000000000ull-uint64_t(MaxModules)*MaxImageBytes)return Fail(Error::InvalidState);
    for(uint32_t i=0;i<count;++i){if(!Name(s[i].name) || !s[i].data || s[i].bytes<64 || s[i].bytes>peprofile::FileBytes)return Fail(Error::InvalidImage);
        for(uint32_t j=0;j<i;++j)if(Equal(s[i].name,s[j].name,true))return Fail(Error::InvalidImage);}
    backend_=b;context_=c;sources_=s;count_=count;arena_=arena;error_=Error::Ok;
    for(auto& m:modules_)m={};pinned_=0;for(auto& r:references_)r=0;handlers_=handlers;return true;
}
uint32_t Loader::Find(const char* name,const char* importer) const noexcept {
    if constexpr(peprofile::Extended){if(namespace_.find){const auto i=namespace_.find(namespace_.context,name,importer);return i<count_?i:UINT32_MAX;}}
    for(uint32_t i=0;i<count_;++i)if(Equal(name,sources_[i].name,true))return i;return UINT32_MAX;
}
bool Loader::Span(const Module& m,uint32_t at,uint32_t n,uint32_t flags) const noexcept {
    if(!n || !Range(at,n,m.size))return false;
    if(at<m.headers)return flags==ReadFlag && Range(at,n,m.headers);
    for(uint32_t i=0;i<m.sectionCount;++i){const auto& s=m.sections[i];if(at>=s.rva && Range(at-s.rva,n,s.bytes))return (s.flags&flags)==flags;}
    return false;
}
bool Loader::Read(const Module& m,uint32_t at,void* out,uint32_t n) noexcept {
    if(!Span(m,at,n))return Fail(Error::InvalidImage);
    return backend_.read(context_,m.base+at,out,n) || Fail(Error::Access);
}
bool Loader::Write(const Module& m,uint32_t at,const void* in,uint32_t n) noexcept {
    if(!Span(m,at,n))return Fail(Error::InvalidImage);
    return backend_.write(context_,m.base+at,in,n) || Fail(Error::Access);
}
bool Loader::String(const Module& m,uint32_t at,char* out,uint32_t capacity) noexcept {
    if(!capacity)return Fail(Error::InvalidImage);
    for(uint32_t i=0;i<capacity;++i){if(at>UINT32_MAX-i || !Read(m,at+i,out+i,1))return false;if(!out[i])return true;
        if(static_cast<uint8_t>(out[i])<32 || static_cast<uint8_t>(out[i])>126)return Fail(Error::InvalidImage);}
    return Fail(Error::Limit);
}
bool Loader::Headers(uint32_t index) noexcept {
    auto& m=modules_[index];const auto& source=sources_[index];const auto* raw=source.data;
    if(U16(raw)!=0x5A4D)return Fail(Error::InvalidImage);
    const auto nt=U32(raw+60);if(nt<64 || !Range(nt,24,source.bytes) || U32(raw+nt)!=0x4550)return Fail(Error::InvalidImage);
    const auto* file=raw+nt+4;const auto sections=U16(file+2),optional=U16(file+16),characteristics=U16(file+18);
    if(U16(file)!=0x8664 || !(characteristics&2) || (characteristics&1))return Fail(Error::Unsupported);
    if(!sections || sections>MaxSections || optional<240 || !Range(nt+24,optional,source.bytes))return Fail(Error::InvalidImage);
    const auto* opt=file+20;
    if(U16(opt)!=0x20B || U32(opt+32)!=4096 || U32(opt+36)!=512 || U32(opt+108)!=16)return Fail(Error::Unsupported);
    m.size=U32(opt+56);m.headers=U32(opt+60);m.preferred=U64(opt+24);m.sectionCount=sections;m.dll=(characteristics&0x2000)!=0;
    const auto entry=U32(opt+16),table=nt+24+optional;
    if(!m.size || m.size>MaxImageBytes || (m.size&4095) || !m.headers || (m.headers&511) || m.headers>source.bytes || m.headers>m.size ||
        !Range(table,uint32_t(sections)*40,m.headers) || !m.preferred || (m.preferred&0xFFFF) || m.preferred>0x0000800000000000ull-m.size)return Fail(Error::InvalidImage);
    uint32_t previous=Pages(m.headers);
    for(uint32_t i=0;i<sections;++i){const auto* sh=raw+table+i*40;auto& s=m.sections[i];
        s.rva=U32(sh+12);s.rawBytes=U32(sh+16);s.raw=U32(sh+20);s.flags=U32(sh+36);s.bytes=U32(sh+8);if(s.bytes<s.rawBytes)s.bytes=s.rawBytes;
        if(!s.bytes || (s.rva&4095) || s.rva<previous || !Range(s.rva,s.bytes,m.size) || !(s.flags&ReadFlag) ||
            ((s.flags&WriteFlag) && (s.flags&ExecuteFlag)))return Fail(Error::InvalidImage);
        if(s.rawBytes && ((s.raw&511) ||
#ifndef GUARDIAN_DESKTOP_NATIVE
            (s.rawBytes&511) ||
#endif
            s.raw<m.headers || !Range(s.raw,s.rawBytes,source.bytes)))return Fail(Error::InvalidImage);
        for(uint32_t j=0;j<i;++j){const auto& p=m.sections[j];if(s.rawBytes && p.rawBytes && s.raw<p.raw+p.rawBytes && p.raw<s.raw+s.rawBytes)return Fail(Error::InvalidImage);}
        previous=s.rva+Pages(s.bytes);
    }
    if(previous!=m.size || (!m.dll && !entry) || (entry && !Span(m,entry,1,ReadFlag|ExecuteFlag)))return Fail(Error::InvalidImage);
    m.entry=entry?m.base+entry:0;
    for(uint32_t i=0;i<16;++i){auto& d=m.directories[i];d={U32(opt+112+i*8),U32(opt+116+i*8)};
        if(bool(d.rva)!=bool(d.bytes))return Fail(Error::InvalidImage);
        if constexpr(peprofile::Extended){if(i==4){if(d.bytes&&((d.rva&7)||!Range(d.rva,d.bytes,source.bytes)))return Fail(Error::InvalidImage);continue;}}
        if(d.bytes&&!Span(m,d.rva,d.bytes))return Fail(Error::InvalidImage);
        if(d.bytes && i!=0 && i!=1 && i!=3 && i!=5 && i!=6 && i!=9 && i!=12 &&
           !(peprofile::Extended&&(i==2||i==13))
#ifdef GUARDIAN_NATIVE_NT
           &&i!=10
#endif
           )return Fail(Error::Unsupported);}
    return true;
}
bool Loader::Relocate(Module& m) noexcept {
    const auto& d=m.directories[5];const uint64_t delta=m.base-m.preferred;
    if(delta && !d.bytes)return Fail(Error::InvalidImage);
#ifdef GUARDIAN_SCALABLE_PE
    auto* targets=relocationTargets_;
    for(auto& target:relocationTargets_)target=0;
#else
    uint32_t targets[peprofile::Relocations]{};
#endif
    uint32_t offset=0,count=0;
    while(offset<d.bytes){uint8_t block[8]{};if(d.bytes-offset<8 || !Read(m,d.rva+offset,block,8))return Fail(Error::InvalidImage);
        const auto page=U32(block),size=U32(block+4);
        if((page&4095) || size<8 || (size&1) || size>d.bytes-offset)return Fail(Error::InvalidImage);
        for(uint32_t i=8;i<size;i+=2){uint8_t b[2]{};if(!Read(m,d.rva+offset+i,b,2))return false;const auto type=U16(b)>>12;
            if(!type)continue;if(type!=10)return Fail(Error::Unsupported);
            const uint64_t target=uint64_t(page)+(U16(b)&4095);if(target>UINT32_MAX || !Span(m,static_cast<uint32_t>(target),8))return Fail(Error::InvalidImage);
            if(count==peprofile::Relocations)return Fail(Error::Limit);
#ifdef GUARDIAN_SCALABLE_PE
            uint32_t bucket=(static_cast<uint32_t>(target)*2654435761u)%(peprofile::Relocations*2);
            for(uint32_t probe=0;probe<peprofile::Relocations*2;++probe){
                if(targets[bucket]==target+1)return Fail(Error::InvalidImage);
                if(!targets[bucket]){targets[bucket]=static_cast<uint32_t>(target)+1;break;}
                bucket=(bucket+1)%(peprofile::Relocations*2);
            }++count;
#else
            for(uint32_t j=0;j<count;++j)if(targets[j]==target)return Fail(Error::InvalidImage);targets[count++]=static_cast<uint32_t>(target);
#endif
            uint64_t value=0;if(!Read(m,static_cast<uint32_t>(target),&value,8))return false;value+=delta;
            if(!Write(m,static_cast<uint32_t>(target),&value,8))return false;++m.relocated;
        }offset+=size;
    }return true;
}
bool Loader::Exports(Module& m) noexcept {
    const auto& d=m.directories[0];if(!d.bytes)return true;
    uint8_t h[40]{};if(d.bytes<40 || !Read(m,d.rva,h,40))return Fail(Error::InvalidImage);
    const auto functions=U32(h+20),names=U32(h+24),ft=U32(h+28),nt=U32(h+32),ot=U32(h+36);
    if(!functions || functions>peprofile::Exports || names>functions || !Span(m,ft,functions*4) || (names && (!Span(m,nt,names*4) || !Span(m,ot,names*2))))return Fail(Error::InvalidImage);
    for(uint32_t i=0;i<functions;++i){uint32_t rva=0;if(!Read(m,ft+i*4,&rva,4))return false;
        if(rva>=d.rva && rva-d.rva<d.bytes){
            if(!peprofile::Extended)return Fail(Error::Unsupported);
            char forward[128]{};const auto remaining=d.bytes-(rva-d.rva);if(!String(m,rva,forward,remaining<sizeof(forward)?remaining:sizeof(forward)))return false;
            bool dot=false;for(uint32_t c=0;forward[c];++c)if(forward[c]=='.')dot=true;
            if(!dot)return Fail(Error::InvalidImage);
        }
        if(rva && !Span(m,rva,1))return Fail(Error::InvalidImage);}
    char text[peprofile::SymbolCharacters]{};for(uint32_t i=0;i<names;++i){uint32_t rva=0;uint16_t ordinal=0;
        if(!Read(m,nt+i*4,&rva,4) || !Read(m,ot+i*2,&ordinal,2) || ordinal>=functions || !String(m,rva,text,sizeof(text)) || !text[0])return Fail(Error::InvalidImage);}
    return true;
}
bool Loader::Symbol(Module& m,const char* name,uint32_t ordinal,uint64_t& address,uint32_t depth) noexcept {
    if(depth>=MaxModules)return Fail(Error::Cycle);
    const auto& d=m.directories[0];if(!d.bytes)return Fail(Error::MissingSymbol);
    if(!Exports(m))return false;uint8_t h[40]{};if(!Read(m,d.rva,h,40))return false;
    const auto functions=U32(h+20),names=U32(h+24),ft=U32(h+28),nt=U32(h+32),ot=U32(h+36);uint32_t selected=UINT32_MAX;
    if(!name){if(ordinal<U32(h+16) || ordinal-U32(h+16)>=functions)return Fail(Error::MissingSymbol);selected=ordinal-U32(h+16);}
    else for(uint32_t i=0;i<names;++i){uint32_t rva=0;uint16_t slot=0;char text[peprofile::SymbolCharacters]{};
        if(!Read(m,nt+i*4,&rva,4) || !Read(m,ot+i*2,&slot,2) || !String(m,rva,text,sizeof(text)))return false;
        if(Equal(name,text)){selected=slot;break;}}
    if(selected>=functions)return Fail(Error::MissingSymbol);uint32_t rva=0;if(!Read(m,ft+selected*4,&rva,4))return false;
    if(!rva)return Fail(Error::MissingSymbol);
    if constexpr(peprofile::Extended){if(rva>=d.rva&&rva-d.rva<d.bytes){
        char text[128]{},dll[128]{};const auto remaining=d.bytes-(rva-d.rva);if(!String(m,rva,text,remaining<sizeof(text)?remaining:sizeof(text)))return false;
        uint32_t dot=UINT32_MAX;for(uint32_t i=0;text[i];++i)if(text[i]=='.')dot=i;
        if(dot==UINT32_MAX||!dot||!text[dot+1]||dot>120)return Fail(Error::InvalidImage);
        for(uint32_t i=0;i<dot;++i)dll[i]=text[i];
        if(dot<4||Lower(dll[dot-4])!='.'||Lower(dll[dot-3])!='d'||Lower(dll[dot-2])!='l'||Lower(dll[dot-1])!='l'){
            dll[dot++]='.';dll[dot++]='d';dll[dot++]='l';dll[dot++]='l';}
        uint32_t source=UINT32_MAX;for(uint32_t i=0;i<count_;++i)if(&modules_[i]==&m)source=i;
        const auto dep=Find(dll,source<count_?sources_[source].name:nullptr);if(dep==UINT32_MAX)return Fail(Error::MissingModule);
        if(!LoadOne(dep))return false;if(!modules_[dep].dll)return Fail(Error::InvalidImage);
        m.dependencies|=1u<<dep;
        const char* symbol=text;uint32_t split=0;for(uint32_t i=0;text[i];++i)if(text[i]=='.')split=i;symbol+=split+1;
        if(symbol[0]=='#'){uint32_t value=0;if(!symbol[1])return Fail(Error::InvalidImage);
            for(uint32_t i=1;symbol[i];++i){if(symbol[i]<'0'||symbol[i]>'9'||value>6553)return Fail(Error::InvalidImage);value=value*10+uint32_t(symbol[i]-'0');}
            if(value>65535)return Fail(Error::InvalidImage);return Symbol(modules_[dep],nullptr,value,address,depth+1);}
        return Symbol(modules_[dep],symbol,0,address,depth+1);
    }}
    address=m.base+rva;return true;
}
bool Loader::Imports(Module& m) noexcept {
    const auto& d=m.directories[1];if(!d.bytes)return true;
    for(uint32_t at=0;at+20<=d.bytes;at+=20){uint8_t h[20]{};if(!Read(m,d.rva+at,h,20))return false;
        const auto lookup=U32(h),name=U32(h+12),iat=U32(h+16);
        if(!lookup && !name && !iat && !U32(h+4) && !U32(h+8))return true;
        if(!name || !iat || ((!peprofile::Extended||!lookup)&&(U32(h+4)||U32(h+8))) || (iat&7) || (lookup&7))return Fail(Error::InvalidImage);
        char dll[64]{};if(!String(m,name,dll,sizeof(dll)) || !Name(dll))return Fail(Error::InvalidImage);
        uint32_t source=UINT32_MAX;for(uint32_t i=0;i<count_;++i)if(&modules_[i]==&m)source=i;
        const auto dep=Find(dll,source<count_?sources_[source].name:nullptr);if(dep==UINT32_MAX)return Fail(Error::MissingModule);
        if(!LoadOne(dep))return false;if(!modules_[dep].dll)return Fail(Error::InvalidImage);m.dependencies|=1u<<dep;
        bool ended=false;for(uint32_t i=0;i<peprofile::Imports;++i){const auto step=i*8;const uint64_t from=uint64_t(lookup?lookup:iat)+step,to=uint64_t(iat)+step;
            if(from>UINT32_MAX || to>UINT32_MAX)return Fail(Error::InvalidImage);uint64_t thunk=0,address=0;
            if(!Read(m,static_cast<uint32_t>(from),&thunk,8) || to<m.headers || !Span(m,static_cast<uint32_t>(to),8) ||
                Span(m,static_cast<uint32_t>(to),8,ReadFlag|ExecuteFlag))return Fail(Error::InvalidImage);
            if(!thunk){ended=true;break;}
            if(thunk>>63){if(thunk&0x7FFFFFFFFFFF0000ull)return Fail(Error::InvalidImage);
                if(!Symbol(modules_[dep],nullptr,static_cast<uint16_t>(thunk),address))return false;}
            else {if(thunk>UINT32_MAX-2)return Fail(Error::InvalidImage);char symbol[peprofile::SymbolCharacters]{};
                if(!String(m,static_cast<uint32_t>(thunk)+2,symbol,sizeof(symbol)))return false;
                if(!symbol[0])return Fail(Error::InvalidImage);if(!Symbol(modules_[dep],symbol,0,address))return false;}
            if(!Write(m,static_cast<uint32_t>(to),&address,8))return false;++m.imports;
        }if(!ended)return Fail(Error::Limit);
    }return Fail(Error::InvalidImage);
}
bool Loader::Tls(Module& m,uint32_t index) noexcept {
    const auto& d=m.directories[9];if(!d.bytes)return true;uint8_t h[40]{};
    if(d.bytes<40 || !Read(m,d.rva,h,40))return Fail(Error::InvalidImage);
    const auto start=U64(h),end=U64(h+8),slot=U64(h+16),callbacks=U64(h+24);const auto zero=U32(h+32),flags=U32(h+36);
    if(end<start || end-start>peprofile::TlsBytes || zero>peprofile::TlsBytes || end-start+zero>peprofile::TlsBytes || flags&~0x00F00000u || ((flags>>20)&15)>5 ||
        slot<m.base || slot-m.base>UINT32_MAX || !Span(m,static_cast<uint32_t>(slot-m.base),4,ReadFlag|WriteFlag))return Fail(Error::InvalidImage);
    if(end!=start && (start<m.base || start-m.base>UINT32_MAX || !Span(m,static_cast<uint32_t>(start-m.base),static_cast<uint32_t>(end-start))))return Fail(Error::InvalidImage);
    m.tlsTemplate=start;m.tlsBytes=static_cast<uint32_t>(end-start);m.tlsZeroBytes=zero;m.tlsIndexAddress=slot;m.tlsIndex=index;
    if(!Write(m,static_cast<uint32_t>(slot-m.base),&index,4))return false;
    if(callbacks){if(callbacks<m.base || callbacks-m.base>UINT32_MAX || (callbacks&7))return Fail(Error::InvalidImage);
        for(uint32_t i=0;i<=8;++i){const uint64_t offset=callbacks-m.base+uint64_t(i)*8;if(offset>UINT32_MAX)return Fail(Error::InvalidImage);
            uint64_t entry=0;if(!Read(m,static_cast<uint32_t>(offset),&entry,8))return false;if(!entry)return true;
            if(i==8)return Fail(Error::Limit);
            if(entry<m.base || entry-m.base>UINT32_MAX || !Span(m,static_cast<uint32_t>(entry-m.base),1,ReadFlag|ExecuteFlag))return Fail(Error::InvalidImage);
            m.tlsCallbacks[m.tlsCallbackCount++]=entry;}}
    return true;
}

#include "PeLoaderExtra.inl"
bool Loader::Exceptions(Module& m) noexcept {
    const auto& d=m.directories[3];if(!d.bytes)return true;
    if((d.bytes%12) || d.bytes/12>MaxRuntimeFunctions)return Fail(Error::InvalidImage);uint32_t last=0;
    for(uint32_t i=0;i<d.bytes;i+=12){uint8_t f[12]{},h[4]{};if(!Read(m,d.rva+i,f,12))return false;
#ifdef GUARDIAN_NATIVE_NT
        diagnosticDetail_=i/12+1;
#endif
        const auto begin=U32(f),end=U32(f+4),unwind=U32(f+8);
        if(begin<last || end<=begin || !Span(m,begin,end-begin,ReadFlag|ExecuteFlag) || (unwind&3) || !Read(m,unwind,h,4))return Fail(Error::InvalidImage);
        const auto flags=h[0]>>3;
        const auto version=h[0]&7;
        if((version!=1
#ifdef GUARDIAN_NATIVE_NT
            &&version!=2
#endif
            ) || (flags!=0 && flags!=4 && !(handlers_ && flags<=3)))return Fail(Error::Unsupported);
        if(h[1]>end-begin || h[2]>64 || (!(h[3]&15) && (h[3]>>4)) || !Span(m,unwind,4+((uint32_t(h[2])+1)&~1u)*2))return Fail(Error::InvalidImage);
        uint32_t code=0,previous=h[1];
#ifdef GUARDIAN_NATIVE_NT
        uint32_t epilogs=0,epilogSize=0;
#endif
        while(code<h[2]){uint8_t item[2]{};if(!Read(m,unwind+4+code*2,item,2))return false;
            const auto op=item[1]&15,info=item[1]>>4;uint32_t extra=0;
#ifdef GUARDIAN_NATIVE_NT
            if(version==2&&op==6){if(code!=epilogs)return Fail(Error::InvalidImage);
                if(!epilogs){epilogSize=item[0];if(!epilogSize||epilogSize>end-begin||info>1)return Fail(Error::InvalidImage);}
                else {const uint32_t offset=uint32_t(item[0])+uint32_t(info)*256;if(offset&&(offset<epilogSize||offset>end-begin))return Fail(Error::InvalidImage);}
                ++epilogs;++code;continue;}
#endif
            if(item[0]>previous || item[0]>h[1])return Fail(Error::InvalidImage);previous=item[0];
            switch(op){case 0:if(info==4)return Fail(Error::InvalidImage);break;
            case 1:if(info>1)return Fail(Error::InvalidImage);extra=info?2u:1u;break;
            case 2:break;case 3:if(
#ifndef GUARDIAN_NATIVE_NT
                info ||
#endif
                !(h[3]&15))return Fail(Error::InvalidImage);break;
            case 4:case 8:extra=1;break;case 5:case 9:extra=2;break;
            case 10:if(info>1)return Fail(Error::InvalidImage);break;default:return Fail(Error::Unsupported);}
            if(extra>=uint32_t(h[2])-code)return Fail(Error::InvalidImage);code+=extra+1;
        }
#ifdef GUARDIAN_NATIVE_NT
        if(epilogs&1)return Fail(Error::InvalidImage);
#endif
        if(flags && flags!=4){
            uint32_t handler=0;const auto tail=unwind+4+((uint32_t(h[2])+1)&~1u)*2;
            if(!Read(m,tail,&handler,4) || !handler || !Span(m,handler,1,ReadFlag|ExecuteFlag))return Fail(Error::InvalidImage);
        }
        if(flags==4){
#ifdef GUARDIAN_DESKTOP_NATIVE
            uint32_t seen[32]{},visited=0,current=unwind;uint8_t record[4]{};for(uint32_t j=0;j<4;++j)record[j]=h[j];
            for(;;){if(visited==32)return Fail(Error::Limit);for(uint32_t j=0;j<visited;++j)if(seen[j]==current)return Fail(Error::InvalidImage);seen[visited++]=current;
                if(record[2]>64)return Fail(Error::InvalidImage);const auto tail=current+4+((uint32_t(record[2])+1)&~1u)*2;uint8_t chained[12]{};if(!Read(m,tail,chained,12))return false;bool found=false;
                for(uint32_t at=0;at<d.bytes;at+=12){uint8_t entry[12]{};if(!Read(m,d.rva+at,entry,12))return false;if(U32(entry)==U32(chained)&&U32(entry+4)==U32(chained+4)&&U32(entry+8)==U32(chained+8)){found=true;break;}}
                if(!found||!Read(m,U32(chained+8),record,4)||record[3]!=h[3])return Fail(Error::InvalidImage);current=U32(chained+8);if((record[0]>>3)!=4)break;
            }
#else
            // A secondary record must refer to an already validated .pdata
            // entry. Strictly backward edges bound traversal and exclude cycles.
            const auto tail=unwind+4+((uint32_t(h[2])+1)&~1u)*2;uint8_t chained[12]{};
            if(!Read(m,tail,chained,12))return false;bool found=false;
            for(uint32_t prior=0;prior<i;prior+=12){uint8_t entry[12]{};if(!Read(m,d.rva+prior,entry,12))return false;
                if(U32(entry)==U32(chained) && U32(entry+4)==U32(chained+4) && U32(entry+8)==U32(chained+8)){found=true;break;}}
            uint8_t primary[4]{};
            if(!found || !Read(m,U32(chained+8),primary,4) || primary[3]!=h[3])return Fail(Error::InvalidImage);
#endif
        }
        last=end;++m.functions;
    }return true;
}
bool Loader::Seal(Module& m) noexcept {
    if(!backend_.protect(context_,m.base,m.size,Protection::None) || !backend_.protect(context_,m.base,Pages(m.headers),Protection::Read))return Fail(Error::Access);
    for(uint32_t i=0;i<m.sectionCount;++i){const auto& s=m.sections[i];const auto p=(s.flags&ExecuteFlag)?Protection::ExecuteRead:((s.flags&WriteFlag)?Protection::ReadWrite:Protection::Read);
        if(!backend_.protect(context_,m.base+s.rva,Pages(s.bytes),p))return Fail(Error::Access);}
    return true;
}
bool Loader::LoadOne(uint32_t index) noexcept {
    auto& m=modules_[index];if(m.state==2)return true;if(m.state==1)return Fail(Error::Cycle);
    if(nextGeneration_==UINT32_MAX)return Fail(Error::Limit);m.generation=nextGeneration_++;
    m.base=arena_+uint64_t(index)*MaxImageBytes;m.state=1;
    if(!Headers(index)){
#ifdef GUARDIAN_NATIVE_NT
        diagnosticStage_=1;diagnosticModule_=index;
#endif
        return false;}
    if(!backend_.map(context_,m.base,m.size))return Fail(Error::NoMemory);m.mapped=true;
    if(!backend_.write(context_,m.base,sources_[index].data,m.headers))return Fail(Error::Access);
    for(uint32_t i=0;i<m.sectionCount;++i){const auto& s=m.sections[i];if(s.rawBytes && !backend_.write(context_,m.base+s.rva,sources_[index].data+s.raw,s.rawBytes))return Fail(Error::Access);}
#ifdef GUARDIAN_NATIVE_NT
    const auto step=[&](uint32_t stage,bool ok) noexcept {if(!ok&&!diagnosticStage_){diagnosticStage_=stage;diagnosticModule_=index;}return ok;};
    if(!step(2,Relocate(m))||!step(3,Resources(m,index==0))||!step(4,Imports(m))||!step(5,DelayImports(m))||!step(6,Tls(m,index))||!step(7,Exports(m))||!step(8,Exceptions(m))||!step(9,Seal(m)))return false;
#else
    if(!Relocate(m) || (peprofile::Extended&&!Resources(m,index==0)) || !Imports(m) ||
       (peprofile::Extended&&!DelayImports(m)) || !Tls(m,index) || !Exports(m) || !Exceptions(m) || !Seal(m))return false;
#endif
    m.state=2;return true;
}
bool Loader::Rollback(uint32_t keep) noexcept {
    bool ok=true;for(uint32_t i=count_;i;--i){auto& m=modules_[i-1];if(keep&(1u<<(i-1)))continue;
        if(m.mapped && !backend_.unmap(context_,m.base)){ok=false;continue;}m={};references_[i-1]=0;pinned_&=~(1u<<(i-1));}
    return ok || Fail(Error::Access);
}
bool Loader::LoadRaw(const char* name,uint32_t& index) noexcept {
    index=UINT32_MAX;if(!sources_ || !Name(name))return Fail(Error::InvalidState);
    uint32_t keep=0;for(uint32_t i=0;i<count_;++i){if(modules_[i].state==1)return Fail(Error::InvalidState);if(modules_[i].state==2)keep|=1u<<i;}
    const auto found=Find(name);if(found==UINT32_MAX)return Fail(Error::MissingModule);error_=Error::Ok;
    if(!LoadOne(found)){Rollback(keep);return false;}pinned_|=MappedMask();index=found;return true;
}
uint32_t Loader::MappedMask() const noexcept {uint32_t mask=0;for(uint32_t i=0;i<count_;++i)if(modules_[i].state==2)mask|=1u<<i;return mask;}
uint32_t Loader::Reachable() const noexcept {
    uint32_t mask=pinned_;for(uint32_t i=0;i<count_;++i)if(references_[i])mask|=1u<<i;
    for(uint32_t pass=0;pass<count_;++pass)for(uint32_t i=0;i<count_;++i)if(mask&(1u<<i))mask|=modules_[i].dependencies;return mask;
}
bool Loader::AcquireRaw(const char* name,uint32_t& index,uint32_t& added) noexcept {
    index=UINT32_MAX;added=0;if(!sources_ || !Name(name))return Fail(Error::InvalidState);
    for(uint32_t i=0;i<count_;++i)if(modules_[i].state==1)return Fail(Error::InvalidState);
    const auto found=Find(name);if(found==UINT32_MAX)return Fail(Error::MissingModule);
    if(references_[found]>=64)return Fail(Error::Limit);const auto keep=MappedMask();error_=Error::Ok;
    if(!LoadOne(found)){Rollback(keep);return false;}
    if(!modules_[found].dll){Rollback(keep);return Fail(Error::Unsupported);}
    ++references_[found];added=MappedMask()&~keep;index=found;return true;
}
bool Loader::Release(uint32_t index,uint32_t& removable) noexcept {
    removable=0;if(!Get(index) || !references_[index])return Fail(Error::InvalidState);
    --references_[index];removable=MappedMask()&~Reachable();error_=Error::Ok;return true;
}
bool Loader::Remove(uint32_t mask) noexcept {
    if(!sources_ || (mask&~MappedMask()) || (mask&Reachable()))return Fail(Error::InvalidState);
    // Dependents go first, even when catalog order differs from dependency order.
    for(uint32_t pass=0;mask && pass<count_;++pass){bool progress=false;
        for(uint32_t i=0;i<count_;++i)if(mask&(1u<<i)){bool needed=false;
            for(uint32_t j=0;j<count_;++j)if(i!=j && (mask&(1u<<j)) && (modules_[j].dependencies&(1u<<i)))needed=true;
            if(needed)continue;if(!backend_.unmap(context_,modules_[i].base))return Fail(Error::Access);
            modules_[i]={};references_[i]=0;mask&=~(1u<<i);progress=true;}
        if(!progress)return Fail(Error::Cycle);
    }return !mask || Fail(Error::Cycle);
}
bool Loader::Resolve(uint32_t index,const char* name,uint32_t ordinal,uint64_t& address) noexcept {
    address=0;if(!Get(index) || (name && !name[0]))return Fail(Error::InvalidState);uint64_t result=0;
    const auto keep=MappedMask();uint32_t dependencies[MaxModules]{};for(uint32_t i=0;i<count_;++i)dependencies[i]=modules_[i].dependencies;
    if(!Symbol(modules_[index],name,ordinal,result)){
        for(uint32_t i=0;i<count_;++i)modules_[i].dependencies=dependencies[i];Rollback(keep);return false;}
    if constexpr(!peprofile::Extended){if(!Executable(result))return Fail(Error::Unsupported);}
    address=result;error_=Error::Ok;return true;
}
bool Loader::CancelResolution(uint32_t added) noexcept {
    if(added&~MappedMask()||added&pinned_)return Fail(Error::InvalidState);
    for(uint32_t i=0;i<count_;++i)if((added&(1u<<i))&&references_[i])return Fail(Error::InvalidState);
    for(uint32_t i=0;i<count_;++i)modules_[i].dependencies&=~added;
    return Rollback(MappedMask()&~added);
}
bool Loader::InitializationOrder(uint32_t mask,uint32_t* order,uint32_t& count) const noexcept {
    count=0;uint32_t done=0;
    for(uint32_t pass=0;done!=mask&&pass<count_;++pass){bool progress=false;
        for(uint32_t i=0;i<count_;++i)if((mask&~done)&(1u<<i)){
            if(modules_[i].state!=2)return false;if(modules_[i].dependencies&mask&~done)continue;
            order[count++]=i;done|=1u<<i;progress=true;
        }if(!progress)return false;
    }return done==mask;
}
bool Loader::Load(const char* name,uint32_t& index) noexcept {
    auto event=telemetry::Begin(telemetry::Domain::Module,telemetry::Operation::Load,auditPid_,auditUser_,name);
    if(!telemetry::Before(event)){index=UINT32_MAX;Fail(Error::Access);telemetry::End(event,0xC0000022);return false;}
    const bool ok=LoadRaw(name,index);if(ok)event.related=modules_[index].base;telemetry::End(event,ok?0:uint32_t(error_));return ok;
}
bool Loader::Acquire(const char* name,uint32_t& index,uint32_t& added) noexcept {
    auto event=telemetry::Begin(telemetry::Domain::Module,telemetry::Operation::Load,auditPid_,auditUser_,name);
    if(!telemetry::Before(event)){index=UINT32_MAX;added=0;Fail(Error::Access);telemetry::End(event,0xC0000022);return false;}
    const bool ok=AcquireRaw(name,index,added);if(ok)event.related=modules_[index].base;telemetry::End(event,ok?0:uint32_t(error_));return ok;
}
bool Loader::UnloadAll() noexcept {
    auto event=telemetry::Begin(telemetry::Domain::Module,telemetry::Operation::Unload,auditPid_,auditUser_,"owned process modules");
    telemetry::Before(event);const bool ok=Rollback(0);telemetry::End(event,ok?0:uint32_t(error_));return ok;
}
bool Loader::Executable(uint64_t at) const noexcept {
    for(uint32_t i=0;i<count_;++i){const auto& m=modules_[i];if(m.state==2 && at>=m.base && at-m.base<=UINT32_MAX && Span(m,static_cast<uint32_t>(at-m.base),1,ReadFlag|ExecuteFlag))return true;}
    return false;
}
}
