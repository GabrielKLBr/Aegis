#pragma once
#include <stdint.h>
namespace guardian::apiset {
// Bounded readers for the Windows 7 (v2) and Windows 10/11 (v6) namespaces.
// The schema is immutable guest input. No host loader or filesystem is queried.
class Namespace {
public:
    bool Open(const uint8_t* data,uint32_t bytes,uint32_t expected=0) noexcept {
        *this={};if(!data||bytes<8||bytes>262144)return false;data_=data;bytes_=bytes;version_=U(0);
        if(version_!=2&&version_!=6)return Reset();if(expected&&expected!=version_)return Reset();
        if(version_==6){if(bytes<28||U(4)<28||U(4)>bytes)return Reset();bytes_=U(4);count_=U(12);entries_=U(16);
            if(!Span(U(20),uint64_t(count_)*8))return Reset();
            for(uint32_t i=0;i<count_;++i)if(U(U(20)+i*8+4)>=count_)return Reset();
        }else {count_=U(4);entries_=8;}
        if(!count_||count_>4096||!Span(entries_,uint64_t(count_)*(version_==6?24:12)))return Reset();
        for(uint32_t i=0;i<count_;++i){uint32_t name=0,length=0,values=0,n=0;Entry(i,name,length,values,n);
            if(!Text(name,length)||!n||n>64||!Span(values,uint64_t(n)*(version_==6?20:16)))return Reset();
            if(version_==6&&(U(entries_+i*24+12)>length||(U(entries_+i*24+12)&1)))return Reset();
            for(uint32_t j=0;j<n;++j){uint32_t alias=0,aliasBytes=0,host=0,hostBytes=0;Value(values,j,alias,aliasBytes,host,hostBytes);
                if((aliasBytes&&!Text(alias,aliasBytes))||(hostBytes&&!Text(host,hostBytes,true)))return Reset();}
        }return true;
    }
    bool OpenPe(const uint8_t* pe,uint32_t bytes,uint32_t expected=0) noexcept {
        *this={};if(!pe||bytes<64||pe[0]!='M'||pe[1]!='Z')return false;
        const auto read=[&](uint32_t o) noexcept {return uint32_t(pe[o])|(uint32_t(pe[o+1])<<8)|(uint32_t(pe[o+2])<<16)|(uint32_t(pe[o+3])<<24);};
        const auto offset=read(60);if(offset>bytes||bytes-offset<264||read(offset)!=0x4550||pe[offset+4]!=0x64||pe[offset+5]!=0x86||pe[offset+24]!=0x0B||pe[offset+25]!=2)return false;
        const auto sections=uint32_t(pe[offset+6])|(uint32_t(pe[offset+7])<<8),optional=uint32_t(pe[offset+20])|(uint32_t(pe[offset+21])<<8);
        if(!sections||sections>32||optional<240||uint64_t(offset)+24+optional+sections*40>bytes)return false;
        const char name[]={'.','a','p','i','s','e','t',0};bool found=false;
        for(uint32_t i=0;i<sections;++i){const auto at=offset+24+optional+i*40;bool match=true;for(uint32_t c=0;c<8;++c)if(pe[at+c]!=uint8_t(name[c]))match=false;if(!match)continue;
            if(found)return Reset();const auto size=read(at+16),raw=read(at+20);if(raw>bytes||size>bytes-raw||!Open(pe+raw,size,expected))return Reset();found=true;}
        return found;
    }
    bool Resolve(const char* contract,const char* importer,char (&host)[128]) const noexcept {
        host[0]=0;if(!data_||!contract)return false;
        for(uint32_t i=0;i<count_;++i){uint32_t name=0,length=0,values=0,n=0;Entry(i,name,length,values,n);if(!Match(name,length,contract)&&!Revision(i,name,length,contract))continue;
            uint32_t selected=UINT32_MAX,fallback=UINT32_MAX;
            for(uint32_t j=0;j<n;++j){uint32_t alias=0,aliasBytes=0,value=0,valueBytes=0;Value(values,j,alias,aliasBytes,value,valueBytes);
                if(!aliasBytes){if(fallback!=UINT32_MAX)return false;fallback=j;}
                else if(importer&&Match(alias,aliasBytes,importer)){if(selected!=UINT32_MAX)return false;selected=j;}}
            if(selected==UINT32_MAX)selected=fallback;if(selected==UINT32_MAX)return false;
            uint32_t alias=0,aliasBytes=0,value=0,valueBytes=0;Value(values,selected,alias,aliasBytes,value,valueBytes);if(!valueBytes)return false;
            for(uint32_t k=0;k<valueBytes/2;++k)host[k]=Lower(char(data_[value+k*2]));host[valueBytes/2]=0;return true;
        }return false;
    }
    uint32_t Version() const noexcept {return version_;}uint32_t Count() const noexcept {return count_;}
private:
    bool Reset() noexcept {*this={};return false;}
    uint32_t U(uint32_t at) const noexcept {return Span(at,4)?uint32_t(data_[at])|(uint32_t(data_[at+1])<<8)|(uint32_t(data_[at+2])<<16)|(uint32_t(data_[at+3])<<24):UINT32_MAX;}
    bool Span(uint32_t at,uint64_t size) const noexcept {return at<=bytes_&&size<=bytes_-at;}
    bool Text(uint32_t at,uint32_t size,bool host=false) const noexcept {
        if(!size||(size&1)||size>254||!Span(at,size))return false;
        for(uint32_t i=0;i<size;i+=2){const auto c=data_[at+i];if(data_[at+i+1]||!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;}
        if(host)for(uint32_t i=2;i<size;i+=2)if(data_[at+i]=='.'&&data_[at+i-2]=='.')return false;return true;
    }
    static char Lower(char c) noexcept {return c>='A'&&c<='Z'?char(c+32):c;}
    bool Revision(uint32_t index,uint32_t name,uint32_t length,const char* input) const noexcept {
        if(version_!=6)return false;const auto hashed=U(entries_+index*24+12);if(!hashed||hashed>=length)return false;
        uint32_t n=0;while(n<128&&input[n])++n;if(n==128)return false;
        if(n>4&&Lower(input[n-4])=='.'&&Lower(input[n-3])=='d'&&Lower(input[n-2])=='l'&&Lower(input[n-1])=='l')n-=4;
        uint32_t dash=n;while(dash&&input[dash-1]!='-')--dash;if(!dash||dash-1!=hashed/2||dash==n||data_[name+hashed]!='-')return false;
        for(uint32_t i=0;i<hashed/2;++i)if(Lower(input[i])!=Lower(char(data_[name+i*2])))return false;
        uint32_t requested=0,available=0;for(uint32_t i=dash;i<n;++i){if(input[i]<'0'||input[i]>'9'||requested>6553)return false;requested=requested*10+uint32_t(input[i]-'0');}
        for(uint32_t i=hashed/2+1;i<length/2;++i){const auto c=data_[name+i*2];if(c<'0'||c>'9'||available>6553)return false;available=available*10+uint32_t(c-'0');}
        return requested<=available;
    }
    bool Match(uint32_t at,uint32_t bytes,const char* input) const noexcept {
        const char* leaf=input;for(uint32_t i=0;i<128&&input[i];++i)if(input[i]=='\\'||input[i]=='/')leaf=input+i+1;
        uint32_t n=0;while(n<128&&leaf[n])++n;if(n==128)return false;
        if(n>4&&Lower(leaf[n-4])=='.'&&Lower(leaf[n-3])=='d'&&Lower(leaf[n-2])=='l'&&Lower(leaf[n-1])=='l')n-=4;
        auto count=bytes/2;if(count>4&&data_[at+(count-4)*2]=='.'&&Lower(char(data_[at+(count-3)*2]))=='d'&&Lower(char(data_[at+(count-2)*2]))=='l'&&Lower(char(data_[at+(count-1)*2]))=='l')count-=4;
        if(n!=count)return false;for(uint32_t i=0;i<n;++i)if(Lower(leaf[i])!=Lower(char(data_[at+i*2])))return false;return true;
    }
    void Entry(uint32_t i,uint32_t& name,uint32_t& length,uint32_t& values,uint32_t& count) const noexcept {
        const auto at=entries_+i*(version_==6?24:12);name=U(at+(version_==6?4:0));length=U(at+(version_==6?8:4));
        const auto array=U(at+(version_==6?16:8));count=version_==6?U(at+20):U(array);values=version_==6?array:array+4;
    }
    void Value(uint32_t array,uint32_t i,uint32_t& alias,uint32_t& aliasBytes,uint32_t& host,uint32_t& hostBytes) const noexcept {
        const auto at=array+i*(version_==6?20:16)+(version_==6?4:0);alias=U(at);aliasBytes=U(at+4);host=U(at+8);hostBytes=U(at+12);
    }
    const uint8_t* data_=nullptr;uint32_t bytes_=0,version_=0,count_=0,entries_=0;
};
}
