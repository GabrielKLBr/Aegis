#pragma once
#include <stdint.h>
// ECMA-335 metadata and CIL inspection for the explicitly bounded MiniNT
// console interpreter. This is not the Microsoft CLR or a CLI-conformance claim.
namespace guardian::cli {
inline constexpr uint32_t MaxFile=1u<<20,MaxMetadata=256u<<10,MaxMethods=128,MaxCode=4096,MaxStack=32,MaxLocals=32,MaxArgs=8,MaxFrames=8,MaxInstructions=100000,MaxOutput=65536;
inline uint16_t U16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
inline uint32_t U32(const uint8_t* p){return uint32_t(U16(p))|uint32_t(U16(p+2))<<16;}
inline bool Range(uint32_t at,uint32_t n,uint32_t size){return at<=size&&n<=size-at;}
inline bool Equal(const char* a,const char* b){for(uint32_t i=0;i<256;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
enum class Error:uint32_t {Ok,Pe,Architecture,Header,Metadata,Quota,Signature,Body,Opcode,Token,UnsupportedCall,Branch,Stack,Arithmetic,Instructions,Output,Arguments};
inline const char* Name(Error e){switch(e){case Error::Ok:return "ok";case Error::Pe:return "invalid-pe";case Error::Architecture:return "unsupported-architecture";case Error::Header:return "unsupported-cli-header";case Error::Metadata:return "invalid-cli-metadata";case Error::Quota:return "cli-quota";case Error::Signature:return "unsupported-cli-signature";case Error::Body:return "unsupported-cli-body";case Error::Opcode:return "unsupported-cli-opcode";case Error::Token:return "invalid-cli-token";case Error::UnsupportedCall:return "unsupported-managed-api";case Error::Branch:return "invalid-cli-branch";case Error::Stack:return "invalid-cli-stack";case Error::Arithmetic:return "cli-arithmetic-error";case Error::Instructions:return "cli-instruction-limit";case Error::Output:return "cli-output-failed";case Error::Arguments:return "unsupported-cli-arguments";}return "cli-error";}
enum class Type:uint8_t {Void=1,Bool=2,I4=8,String=14,Object=28,Args=29};
struct Signature {Type result=Type::Void,args[MaxArgs]{};uint32_t count=0;bool instance=false;};
struct Body {const uint8_t* code=nullptr;uint32_t bytes=0,locals=0,maxStack=0;Type local[MaxLocals]{};};
struct Instruction {uint32_t op=0,bytes=0,arg=0;};
inline bool Decode(const uint8_t* code,uint32_t size,uint32_t at,Instruction& out){
    if(at>=size)return false;out={code[at],1,0};if(out.op==0xFE){if(!Range(at,2,size))return false;out.op=0xFE00|code[at+1];out.bytes=2;return out.op>=0xFE01&&out.op<=0xFE05;}
    const auto op=out.op;
    if((op>=0x02&&op<=0x0D)||op==0||op==0x14||(op>=0x15&&op<=0x1E)||op==0x25||op==0x26||op==0x2A||(op>=0x58&&op<=0x66)||op==0x69||op==0x6D||op==0x8E)return true;
    if(op==0x0E||op==0x10||op==0x11||op==0x13||op==0x1F||(op>=0x2B&&op<=0x37)){if(!Range(at,2,size))return false;out.bytes=2;out.arg=code[at+1];return true;}
    if(op==0x20||op==0x28||op==0x6F||op==0x72||op==0x8C||(op>=0x38&&op<=0x44)){if(!Range(at,5,size))return false;out.bytes=5;out.arg=U32(code+at+1);return true;}
    return false;
}
class Image {
    struct Section {uint32_t rva=0,size=0,raw=0;};
    const uint8_t* data_=nullptr;uint32_t bytes_=0,headers_=0,image_=0,sections_=0;bool mapped_=false;
    Section section_[16]{};const uint8_t* strings_=nullptr,*blob_=nullptr,*us_=nullptr,*table_=nullptr;
    uint32_t stringsBytes_=0,blobBytes_=0,usBytes_=0,tableBytes_=0,row_[45]{},offset_[45]{},width_[45]{};uint8_t heaps_=0;
    bool Fail(Error e,uint32_t at=0){if(error==Error::Ok){error=e;detail=at;}return false;}
    uint32_t Index(const uint8_t* p,uint32_t n) const{return n==2?U16(p):U32(p);}
    uint32_t Str() const{return heaps_&1?4u:2u;}uint32_t Guid() const{return heaps_&2?4u:2u;}uint32_t Blob() const{return heaps_&4?4u:2u;}
    uint32_t Width(uint32_t t) const{
        const auto s=Str(),b=Blob(),g=Guid();
        switch(t){case 0:return 2+s+3*g;case 1:return 2+2*s;case 2:return 4+2*s+6;case 3:case 5:case 7:case 19:case 22:case 41:return t==41?4:2;case 4:return 2+s+b;case 6:return 8+s+b+2;case 8:return 4+s;case 9:return 4;case 10:return 2+s+b;case 11:return 4+b;case 12:return 4+b;case 13:return 2+b;case 14:return 4+b;case 15:return 8;case 16:return 6;case 17:return b;case 18:case 21:return 4;case 20:return 4+s;case 23:return 2+s+b;case 24:case 25:return 6;case 26:return s;case 27:return b;case 28:return 6+s;case 29:return 6;case 30:return 8;case 31:return 4;case 32:return 16+b+2*s;case 33:return 4;case 34:return 12;case 35:return 12+2*b+2*s;case 36:return 6;case 37:return 14;case 38:return 4+s+b;case 39:return 10+2*s;case 40:return 10+s;case 42:return 6+s;case 43:return 2+b;case 44:return 4;default:return 0;}
    }
    const uint8_t* Row(uint32_t t,uint32_t i) const {return t<45&&i&&i<=row_[t]?table_+offset_[t]+(i-1)*width_[t]:nullptr;}
    bool NameAt(uint32_t index,char* out,uint32_t cap) const {if(!strings_||index>=stringsBytes_||cap<2)return false;for(uint32_t i=0;i<cap;++i){if(i>=stringsBytes_-index)return false;const auto c=strings_[index+i];if(!c){out[i]=0;return true;}if(c<32||c>126)return false;out[i]=char(c);}return false;}
    bool Compressed(const uint8_t* p,uint32_t n,uint32_t& at,uint32_t& out) const {
        if(at>=n)return false;const auto a=p[at++];if(!(a&0x80)){out=a;return true;}if((a&0xC0)==0x80){if(at>=n)return false;out=uint32_t(a&63)<<8|p[at++];return out>=128;}if((a&0xE0)!=0xC0||!Range(at,3,n))return false;out=uint32_t(a&31)<<24|uint32_t(p[at])<<16|uint32_t(p[at+1])<<8|p[at+2];at+=3;return out>=16384&&out<=0x1FFFFFFF;
    }
    bool GetBlob(uint32_t index,const uint8_t*& p,uint32_t& n) const{uint32_t at=index;if(!blob_||!Compressed(blob_,blobBytes_,at,n)||!Range(at,n,blobBytes_))return false;p=blob_+at;return true;}
    bool ReadType(const uint8_t* p,uint32_t n,uint32_t& at,Type& out,bool result=false) const{if(at>=n)return false;const auto t=p[at++];if(t==29){if(at>=n||p[at++]!=14)return false;out=Type::Args;return true;}if(t==1&&!result)return false;if(t!=1&&t!=2&&t!=8&&t!=14&&t!=28)return false;out=static_cast<Type>(t);return true;}
    bool ParseSignature(uint32_t index,Signature& s) const{const uint8_t* p=nullptr;uint32_t n=0,at=0;if(!GetBlob(index,p,n)||n<3)return false;const auto convention=p[at++];if(convention!=0&&convention!=0x20)return false;s.instance=convention==0x20;if(!Compressed(p,n,at,s.count)||s.count>MaxArgs||!ReadType(p,n,at,s.result,true))return false;for(uint32_t i=0;i<s.count;++i)if(!ReadType(p,n,at,s.args[i]))return false;return at==n;}
public:
    Error error=Error::Ok;uint32_t detail=0,entry=0,methods=0;
    const uint8_t* At(uint32_t rva,uint32_t n) const{
        if(mapped_)return Range(rva,n,image_)&&Range(rva,n,bytes_)?data_+rva:nullptr;
        if(Range(rva,n,headers_)&&Range(rva,n,bytes_))return data_+rva;
        const uint8_t* found=nullptr;for(uint32_t i=0;i<sections_;++i){const auto& s=section_[i];if(rva>=s.rva&&Range(rva-s.rva,n,s.size)&&Range(s.raw+rva-s.rva,n,bytes_)){if(found)return nullptr;found=data_+s.raw+rva-s.rva;}}return found;
    }
    bool Open(const void* bytes,uint32_t length,bool mapped=false){
        *this={};data_=static_cast<const uint8_t*>(bytes);bytes_=length;mapped_=mapped;
        if(!data_||length<64||length>(mapped?8u<<20:MaxFile)||U16(data_)!=0x5A4D)return Fail(Error::Pe);
        const auto pe=U32(data_+60);if(!Range(pe,264,length)||U32(data_+pe)!=0x4550)return Fail(Error::Pe);
        if(U16(data_+pe+4)!=0x8664||U16(data_+pe+24)!=0x20B)return Fail(Error::Architecture);
        const auto opt=pe+24;sections_=U16(data_+pe+6);const auto optional=U16(data_+pe+20);image_=U32(data_+opt+56);headers_=U32(data_+opt+60);
        if(!sections_||sections_>16||optional<240||!Range(opt,optional+sections_*40,length)||!image_||image_>(8u<<20)||!headers_||headers_>length||headers_>image_||U32(data_+opt+108)!=16)return Fail(Error::Pe);
        for(uint32_t i=0;i<sections_;++i){const auto* s=data_+opt+optional+i*40;section_[i]={U32(s+12),U32(s+16),U32(s+20)};if(!mapped&&!Range(section_[i].raw,section_[i].size,length))return Fail(Error::Pe);}
        const auto corRva=U32(data_+opt+112+14*8),corBytes=U32(data_+opt+116+14*8);const auto* cor=At(corRva,72);
        if(corBytes!=72||!cor||U32(cor)!=72||U16(cor+4)!=2||U16(cor+6)!=5||U32(cor+16)!=1)return Fail(Error::Header);
        for(uint32_t i=24;i<72;++i)if(cor[i])return Fail(Error::Header);
        entry=U32(cor+20);const auto metaBytes=U32(cor+12);const auto* meta=At(U32(cor+8),metaBytes);
        if(!meta||metaBytes<24||metaBytes>MaxMetadata||U32(meta)!=0x424A5342)return Fail(Error::Metadata);
        const auto version=U32(meta+12);if(!version||version>256||!Range(16,version,metaBytes))return Fail(Error::Metadata);uint32_t at=(16+version+3)&~3u;if(!Range(at,4,metaBytes)||U16(meta+at))return Fail(Error::Metadata);const auto streams=U16(meta+at+2);at+=4;if(streams>8)return Fail(Error::Metadata);
        uint32_t streamOffset[8]{},streamSize[8]{};bool guid=false;
        for(uint32_t i=0;i<streams;++i){if(!Range(at,8,metaBytes))return Fail(Error::Metadata);const auto offset=U32(meta+at),size=U32(meta+at+4);at+=8;char name[32]{};bool end=false;for(uint32_t j=0;j<32;++j){if(!Range(at+j,1,metaBytes))return Fail(Error::Metadata);name[j]=char(meta[at+j]);if(!name[j]){at=(at+j+4)&~3u;end=true;break;}}if(!end||!Range(offset,size,metaBytes))return Fail(Error::Metadata);
            if((offset&3)||!size)return Fail(Error::Metadata);streamOffset[i]=offset;streamSize[i]=size;
            for(uint32_t j=0;j<i;++j)if(offset<streamOffset[j]+streamSize[j]&&streamOffset[j]<offset+size)return Fail(Error::Metadata);
            if(Equal(name,"#Strings")){if(strings_)return Fail(Error::Metadata);strings_=meta+offset;stringsBytes_=size;}else if(Equal(name,"#Blob")){if(blob_)return Fail(Error::Metadata);blob_=meta+offset;blobBytes_=size;}else if(Equal(name,"#US")){if(us_)return Fail(Error::Metadata);us_=meta+offset;usBytes_=size;}else if(Equal(name,"#~")){if(table_)return Fail(Error::Metadata);table_=meta+offset;tableBytes_=size;}else if(Equal(name,"#GUID")){if(guid||size%16)return Fail(Error::Metadata);guid=true;}else return Fail(Error::Metadata);
        }
        for(uint32_t i=0;i<streams;++i)if(streamOffset[i]<at)return Fail(Error::Metadata);
        if(!guid||!strings_||!blob_||!us_||!table_||tableBytes_<24||strings_[0]||blob_[0]||us_[0]||U32(table_)||table_[4]!=2||table_[5]!=0||table_[7]!=1)return Fail(Error::Metadata);heaps_=table_[6];if(heaps_&~7u)return Fail(Error::Metadata);at=24;
        for(uint32_t i=0;i<64;++i)if(table_[8+i/8]&(1u<<(i%8))){if(i>=45||!Range(at,4,tableBytes_))return Fail(Error::Metadata);row_[i]=U32(table_+at);at+=4;if(row_[i]>1024)return Fail(Error::Quota,i);}
        for(uint32_t i=0;i<45;++i){width_[i]=Width(i);offset_[i]=at;const auto n=row_[i]*width_[i];if(!Range(at,n,tableBytes_))return Fail(Error::Metadata,i);at+=n;}
        methods=row_[6];if(!methods||methods>MaxMethods||entry>>24!=6||!(entry&0xFFFFFF)||(entry&0xFFFFFF)>methods||row_[4]||row_[14]||row_[28]||row_[42]||row_[43])return Fail(Error::Header);
        for(uint32_t i=1;i<=row_[35];++i){const auto* r=Row(35,i);char name[128]{};if(!NameAt(Index(r+12+Blob(),Str()),name,sizeof(name))||!Equal(name,"mscorlib")||U16(r)!=4)return Fail(Error::UnsupportedCall,0x23000000|i);}
        return true;
    }
    bool Method(uint32_t token,Signature& signature,Body& body){
        signature={};body={};
        if(token>>24!=6)return Fail(Error::Token,token);const auto* m=Row(6,token&0xFFFFFF);if(!m)return Fail(Error::Token,token);
        char name[128]{};if(!NameAt(Index(m+8,Str()),name,sizeof(name))||Equal(name,".cctor"))return Fail(Error::Signature,token);
        if(U16(m+4)!=0||(U16(m+6)&0x2000)||!ParseSignature(Index(m+8+Str(),Blob()),signature)||bool(U16(m+6)&0x10)==signature.instance)return Fail(Error::Signature,token);
        const auto rva=U32(m);const auto* h=At(rva,1);if(!h)return Fail(Error::Body,token);uint32_t header=0,local=0;
        if((*h&3)==2){header=1;body.bytes=*h>>2;body.maxStack=8;}else {h=At(rva,12);if(!h||(U16(h)!=0x3003&&U16(h)!=0x3013))return Fail(Error::Body,token);header=12;body.maxStack=U16(h+2);body.bytes=U32(h+4);local=U32(h+8);if(local&&U16(h)!=0x3013)return Fail(Error::Body,token);}
        if(!body.bytes||body.bytes>MaxCode||body.maxStack>MaxStack)return Fail(Error::Quota,token);body.code=At(rva+header,body.bytes);if(!body.code)return Fail(Error::Body,token);
        if(local){if(local>>24!=17)return Fail(Error::Signature,local);const auto* row=Row(17,local&0xFFFFFF);const uint8_t* p=nullptr;uint32_t n=0,pos=1;if(!row||!GetBlob(Index(row,Blob()),p,n)||!n||p[0]!=7||!Compressed(p,n,pos,body.locals)||body.locals>MaxLocals)return Fail(Error::Signature,local);for(uint32_t i=0;i<body.locals;++i)if(!ReadType(p,n,pos,body.local[i]))return Fail(Error::Signature,local);if(pos!=n)return Fail(Error::Signature,local);}
        return true;
    }
    bool Literal(uint32_t token,const uint8_t*& text,uint32_t& chars) const{uint32_t at=token&0xFFFFFF,n=0;if(token>>24!=0x70||!us_||!Compressed(us_,usBytes_,at,n)||!n||!(n&1)||n>1025||!Range(at,n,usBytes_)||us_[at+n-1]>1)return false;text=us_+at;chars=(n-1)/2;for(uint32_t i=0;i<chars;++i){const auto c=U16(text+i*2);if(c>=0xD800&&c<=0xDBFF){if(++i>=chars)return false;const auto next=U16(text+i*2);if(next<0xDC00||next>0xDFFF)return false;}else if(c>=0xDC00&&c<=0xDFFF)return false;}return true;}
    bool Member(uint32_t token,char* type,char* name,Signature& signature) const{
        if(token>>24!=10)return false;const auto* m=Row(10,token&0xFFFFFF);if(!m)return false;const auto parent=U16(m);if((parent&7)!=1)return false;const auto* r=Row(1,parent>>3);if(!r)return false;const auto scope=U16(r);if((scope&3)!=2||!(scope>>2)||uint32_t(scope>>2)>row_[35])return false;char ns[128]{},simple[128]{};if(!NameAt(Index(r+2,Str()),simple,sizeof(simple))||!NameAt(Index(r+2+Str(),Str()),ns,sizeof(ns))||!NameAt(Index(m+2,Str()),name,128)||!ParseSignature(Index(m+2+Str(),Blob()),signature))return false;
        uint32_t at=0;for(uint32_t i=0;ns[i];++i){if(at>=126)return false;type[at++]=ns[i];}if(at)type[at++]='.';for(uint32_t i=0;simple[i];++i){if(at>=127)return false;type[at++]=simple[i];}type[at]=0;return true;
    }
    bool AllowedCall(uint32_t token){Signature s;Body b;if(token>>24==6)return Method(token,s,b)&&!s.instance;
        char type[128]{},name[128]{};if(!Member(token,type,name,s))return Fail(Error::Token,token);
        const bool console=Equal(type,"System.Console")&&(Equal(name,"WriteLine")||Equal(name,"Write"))&&!s.instance&&s.result==Type::Void&&s.count<=1&&((s.count==0&&Equal(name,"WriteLine"))||s.args[0]==Type::String||s.args[0]==Type::I4||s.args[0]==Type::Bool||s.args[0]==Type::Object);
        const bool ctor=Equal(type,"System.Object")&&Equal(name,".ctor")&&s.instance&&!s.count&&s.result==Type::Void;
        return console||ctor||Fail(Error::UnsupportedCall,token);
    }
    bool Validate(){
        Signature main;Body entryBody;if(!Method(entry,main,entryBody)||main.instance||main.count>1||(main.count&&main.args[0]!=Type::Args)||(main.result!=Type::Void&&main.result!=Type::I4))return Fail(Error::Signature,entry);
        for(uint32_t method=1;method<=methods;++method){Signature sig;Body b;if(!Method(0x06000000|method,sig,b))return false;uint8_t boundaries[MaxCode+1]{};
            for(uint32_t at=0;at<b.bytes;){boundaries[at]=1;Instruction op;if(!Decode(b.code,b.bytes,at,op))return Fail(Error::Opcode,at);if(op.op==0x28||op.op==0x6F){if(!AllowedCall(op.arg))return false;}if(op.op==0x72){const uint8_t* text=nullptr;uint32_t n=0;if(!Literal(op.arg,text,n))return Fail(Error::Token,op.arg);}if(op.op==0x8C||op.op==0x6F)return Fail(Error::Opcode,op.op);at+=op.bytes;}
            boundaries[b.bytes]=1;
            for(uint32_t at=0;at<b.bytes;){Instruction op;Decode(b.code,b.bytes,at,op);if(op.op>=0x2B&&op.op<=0x44){const auto delta=op.bytes==2?int32_t(int8_t(op.arg)):int32_t(op.arg);const auto target=int64_t(at)+op.bytes+delta;if(target<0||target>=b.bytes||!boundaries[uint32_t(target)])return Fail(Error::Branch,at);}at+=op.bytes;}
        }return true;
    }
};
}
