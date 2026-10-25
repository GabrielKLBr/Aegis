#pragma once
#include <stdint.h>
namespace guardian::nls {
inline bool CaseSupported(uint16_t c) noexcept {return c<=255||c==0x178||c==0x152||c==0x153||c==0x160||c==0x161||c==0x17D||c==0x17E||c==0x191||c==0x192||(c>=0x2000&&c<=0x206F);}
inline uint16_t Upper(uint16_t c) noexcept {if((c>='a'&&c<='z')||(c>=0xE0&&c<=0xFE&&c!=0xF7))return uint16_t(c-32);if(c==0xFF)return 0x178;if(c==0x153||c==0x161||c==0x17E||c==0x192)return uint16_t(c-1);return c;}
inline uint16_t Lower(uint16_t c) noexcept {if((c>='A'&&c<='Z')||(c>=0xC0&&c<=0xDE&&c!=0xD7))return uint16_t(c+32);if(c==0x178)return 0xFF;if(c==0x152||c==0x160||c==0x17D||c==0x191)return uint16_t(c+1);return c;}
inline constexpr uint16_t Windows1252[32]={0x20AC,0x81,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,0x2C6,0x2030,0x160,0x2039,0x152,0x8D,0x17D,0x8F,0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0x9D,0x17E,0x178};
inline int Multi(uint32_t page,uint32_t flags,const char* in,int length,uint16_t* out,int capacity,uint32_t& error) noexcept {
    error=0;if(!page)page=1252;if((page!=1252&&page!=65001)||(flags&~8u)||!in||!length||length< -1||capacity<0||(capacity&&!out)){error=87;return 0;}
    if(length==-1){length=0;while(length<(1<<20)&&in[length])++length;if(length==(1<<20)){error=87;return 0;}++length;}if(length>(1<<20)){error=87;return 0;}
    const auto pass=[&](bool write) noexcept {int count=0;for(int i=0;i<length;){const auto lead=uint8_t(in[i++]);uint32_t value=lead;
        if(page==1252){if(lead>=128&&lead<=159)value=Windows1252[lead-128];}
        else if(lead>=128){uint32_t extra=0,minimum=0;
            if(lead>=0xC2&&lead<=0xDF){extra=1;value=lead&31;minimum=128;}else if(lead>=0xE0&&lead<=0xEF){extra=2;value=lead&15;minimum=2048;}else if(lead>=0xF0&&lead<=0xF4){extra=3;value=lead&7;minimum=65536;}
            bool valid=extra!=0&&extra<=uint32_t(length-i);for(uint32_t j=0;valid&&j<extra;++j){const auto b=uint8_t(in[i+int(j)]);if((b&0xC0)!=0x80)valid=false;else value=(value<<6)|(b&63);}
            valid=valid&&value>=minimum&&value<=0x10FFFF&&!(value>=0xD800&&value<=0xDFFF);
            if(valid)i+=int(extra);else {if(flags&8){error=1113;return 0;}value=0xFFFD;}}
        if(value>0xFFFF){value-=0x10000;if(write){out[count]=uint16_t(0xD800+(value>>10));out[count+1]=uint16_t(0xDC00+(value&1023));}count+=2;}
        else {if(write)out[count]=uint16_t(value);++count;}}
        return count;};
    const auto required=pass(false);if(error)return 0;if(!capacity)return required;if(capacity<required){error=122;return 0;}return pass(true);
}
inline int Wide(uint32_t page,uint32_t flags,const uint16_t* in,int length,char* out,int capacity,const char* replacement,int* used,uint32_t& error) noexcept {
    error=0;if(used)*used=0;if(!page)page=1252;
    if((page!=1252&&page!=65001)||(flags&~(page==65001?0x80u:0u))||!in||!length||length< -1||capacity<0||(capacity&&!out)||(page==65001&&(replacement||used))){error=87;return 0;}
    if(length==-1){length=0;while(length<(1<<20)&&in[length])++length;if(length==(1<<20)){error=87;return 0;}++length;}if(length>(1<<20)){error=87;return 0;}
    const auto pass=[&](bool write) noexcept {int count=0;for(int i=0;i<length;++i){uint32_t value=in[i];
        if(value>=0xD800&&value<=0xDBFF&&i+1<length&&in[i+1]>=0xDC00&&in[i+1]<=0xDFFF){value=0x10000+((value-0xD800)<<10)+(in[++i]-0xDC00);}
        else if(value>=0xD800&&value<=0xDFFF){if(flags&0x80){error=1113;return 0;}value=0xFFFD;}
        if(page==1252){uint32_t byte=256;if(value<128||(value>=160&&value<=255))byte=value;else for(uint32_t n=0;n<32;++n)if(Windows1252[n]==value)byte=n+128;
            if(byte==256){byte=uint8_t(replacement?*replacement:'?');if(used)*used=1;}if(write)out[count]=char(byte);++count;
        }else {uint32_t n=value<128?1u:(value<2048?2u:(value<65536?3u:4u));if(write){if(n==1)out[count]=char(value);else {for(uint32_t j=n-1;j;--j){out[count+int(j)]=char(0x80|(value&63));value>>=6;}out[count]=char((n==2?0xC0:(n==3?0xE0:0xF0))|value);}}count+=int(n);}}
        return count;};
    const auto required=pass(false);if(error)return 0;if(!capacity)return required;if(capacity<required){error=122;return 0;}return pass(true);
}
}
