#include <Windows.h>
#include <stdint.h>
#include <new>
#include "guardian/Cli.h"
namespace c=guardian::cli;
extern "C" const int GuardianCliAnchor=1411;
const void* volatile GuardianCliRelocation=&GuardianCliAnchor;
namespace {
enum class Kind:uint32_t {Integer,String,Null,Args};
struct Value {Kind kind=Kind::Integer;int64_t number=0;};
struct Frame {Value stack[c::MaxStack]{},locals[c::MaxLocals]{},args[c::MaxArgs+1]{};uint32_t top=0;};
struct VM {
    c::Image image;Frame frames[c::MaxFrames]{};uint32_t instructions=0;uint32_t output=0;
    bool Error(c::Error e,uint32_t detail=0){if(image.error==c::Error::Ok){image.error=e;image.detail=detail;}return false;}
    bool PrintBytes(const char* data,uint32_t count){
        if(count>c::MaxOutput-output)return Error(c::Error::Output);
        for(uint32_t at=0;at<count;){auto n=count-at>256?256u:count-at;
            if(n<count-at)while(n&&(uint8_t(data[at+n])&0xC0)==0x80)--n;
            DWORD done=0;if(!n||!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),data+at,n,&done,nullptr)||done!=n)return Error(c::Error::Output);
            at+=n;output+=n;
        }return true;
    }
    bool Conforms(c::Type type,Value value){
        if(type==c::Type::I4||type==c::Type::Bool)return value.kind==Kind::Integer;
        if(type==c::Type::Args)return value.kind==Kind::Args||value.kind==Kind::Null;
        if(type==c::Type::String)return value.kind==Kind::String||value.kind==Kind::Null;
        if(type==c::Type::Object)return value.kind==Kind::String||value.kind==Kind::Null||value.kind==Kind::Args;
        return false;
    }
    bool Print(Value value,c::Type type){
        if(value.kind==Kind::Null)return type==c::Type::String||type==c::Type::Object||Error(c::Error::Stack);
        if(value.kind==Kind::String){if(type!=c::Type::String&&type!=c::Type::Object)return Error(c::Error::Stack);const uint8_t* text=nullptr;uint32_t count=0;if(!image.Literal(uint32_t(value.number),text,count))return Error(c::Error::Token);char bytes[2048]{};uint32_t at=0;for(uint32_t i=0;i<count;++i){uint32_t point=c::U16(text+i*2);if(point>=0xD800&&point<=0xDBFF){const auto low=c::U16(text+(++i)*2);point=0x10000+((point-0xD800)<<10)+(low-0xDC00);}if(point<128)bytes[at++]=char(point);else if(point<2048){bytes[at++]=char(0xC0|(point>>6));bytes[at++]=char(0x80|(point&63));}else if(point<65536){bytes[at++]=char(0xE0|(point>>12));bytes[at++]=char(0x80|((point>>6)&63));bytes[at++]=char(0x80|(point&63));}else{bytes[at++]=char(0xF0|(point>>18));bytes[at++]=char(0x80|((point>>12)&63));bytes[at++]=char(0x80|((point>>6)&63));bytes[at++]=char(0x80|(point&63));}}return PrintBytes(bytes,at);}
        if(value.kind==Kind::Args&&type==c::Type::Object)return PrintBytes("System.String[]",15);
        if(value.kind!=Kind::Integer)return Error(c::Error::Stack);
        if(type==c::Type::Bool)return value.number?PrintBytes("True",4):PrintBytes("False",5);
        if(type!=c::Type::I4)return Error(c::Error::Stack);
        char bytes[32]{},reverse[24]{};uint32_t n=0,at=0;const auto signedValue=int64_t(int32_t(value.number));uint64_t number=uint64_t(signedValue);if(signedValue<0){bytes[at++]='-';number=0-number;}do{reverse[n++]=char('0'+number%10);number/=10;}while(number);while(n)bytes[at++]=reverse[--n];return PrintBytes(bytes,at);
    }
    bool Call(uint32_t token,const Value* args,uint32_t count,Value& result,uint32_t depth){
        if(depth>=c::MaxFrames)return Error(c::Error::Quota);if(token>>24==10){char type[128]{},name[128]{};c::Signature sig;if(!image.Member(token,type,name,sig))return Error(c::Error::Token,token);if(c::Equal(type,"System.Object")&&c::Equal(name,".ctor"))return Error(c::Error::UnsupportedCall,token);if(!c::Equal(type,"System.Console")||count!=sig.count)return Error(c::Error::UnsupportedCall,token);if(count&&(!Conforms(sig.args[0],args[0])||!Print(args[0],sig.args[0])))return Error(c::Error::Stack);return !c::Equal(name,"WriteLine")||PrintBytes("\n",1);}
        c::Signature sig;c::Body body;if(!image.Method(token,sig,body)||sig.instance||sig.count!=count)return Error(c::Error::Signature,token);
        for(uint32_t i=0;i<count;++i)if(!Conforms(sig.args[i],args[i]))return Error(c::Error::Stack,token);
        auto& f=frames[depth];f={};for(uint32_t i=0;i<count;++i)f.args[i]=args[i];for(uint32_t i=0;i<body.locals;++i)if(body.local[i]==c::Type::String||body.local[i]==c::Type::Object||body.local[i]==c::Type::Args)f.locals[i].kind=Kind::Null;
        auto push=[&](Value v){if(f.top>=body.maxStack||f.top>=c::MaxStack)return Error(c::Error::Stack);f.stack[f.top++]=v;return true;};
        auto pop=[&](Value& v){if(!f.top)return Error(c::Error::Stack);v=f.stack[--f.top];return true;};
        uint32_t ip=0;while(ip<body.bytes){if(++instructions>c::MaxInstructions)return Error(c::Error::Instructions);c::Instruction op;if(!c::Decode(body.code,body.bytes,ip,op))return Error(c::Error::Opcode,ip);ip+=op.bytes;Value a{},b{};
            if(op.op==0)continue;
            if(op.op>=2&&op.op<=5){if(op.op-2>=count||!push(f.args[op.op-2]))return Error(c::Error::Stack);continue;}
            if(op.op>=6&&op.op<=9){if(op.op-6>=body.locals||!push(f.locals[op.op-6]))return Error(c::Error::Stack);continue;}
            if(op.op>=10&&op.op<=13){if(op.op-10>=body.locals||(!pop(f.locals[op.op-10])||!Conforms(body.local[op.op-10],f.locals[op.op-10])))return Error(c::Error::Stack);continue;}
            if(op.op==0x0E){if(op.arg>=count||!push(f.args[op.arg]))return Error(c::Error::Stack);continue;}
            if(op.op==0x10){if(op.arg>=count||(!pop(f.args[op.arg])||!Conforms(sig.args[op.arg],f.args[op.arg])))return Error(c::Error::Stack);continue;}
            if(op.op==0x11){if(op.arg>=body.locals||!push(f.locals[op.arg]))return Error(c::Error::Stack);continue;}
            if(op.op==0x13){if(op.arg>=body.locals||(!pop(f.locals[op.arg])||!Conforms(body.local[op.arg],f.locals[op.arg])))return Error(c::Error::Stack);continue;}
            if(op.op==0x14){if(!push({Kind::Null,0}))return false;continue;}
            if(op.op>=0x15&&op.op<=0x1E){if(!push({Kind::Integer,int64_t(op.op)-0x16}))return false;continue;}
            if(op.op==0x1F||op.op==0x20){if(!push({Kind::Integer,op.op==0x1F?int64_t(int8_t(op.arg)):int64_t(int32_t(op.arg))}))return false;continue;}
            if(op.op==0x25){if(!f.top||!push(f.stack[f.top-1]))return Error(c::Error::Stack);continue;}
            if(op.op==0x26){if(!pop(a))return false;continue;}
            if(op.op==0x72){if(!push({Kind::String,op.arg}))return false;continue;}
            if(op.op==0x8E){if(!pop(a)||a.kind!=Kind::Args||!push({Kind::Integer,0}))return Error(c::Error::Stack);continue;}
            if(op.op==0x28||op.op==0x6F){c::Signature called;c::Body unused;char type[128]{},name[128]{};if(op.arg>>24==6){if(!image.Method(op.arg,called,unused))return false;}else if(!image.Member(op.arg,type,name,called))return Error(c::Error::Token,op.arg);const auto n=called.count+(called.instance?1u:0u);if(n>c::MaxArgs+1||f.top<n)return Error(c::Error::Stack);Value arguments[c::MaxArgs+1]{};for(uint32_t i=n;i;--i)if(!pop(arguments[i-1]))return false;Value returned;if(!Call(op.arg,arguments,n,returned,depth+1))return false;if(called.result!=c::Type::Void&&!push(returned))return false;continue;}
            if(op.op==0x2A){if(sig.result==c::Type::Void){if(f.top)return Error(c::Error::Stack);result={};return true;}if(f.top!=1||!pop(result)||!Conforms(sig.result,result))return Error(c::Error::Stack);return true;}
            if(op.op>=0x2B&&op.op<=0x44){const auto kind=op.bytes==2?op.op-0x2B:op.op-0x38;bool take=false;if(kind==0)take=true;else if(kind<=2){if(!pop(a))return false;const bool zero=a.kind==Kind::Null||(a.kind==Kind::Integer&&!a.number);take=kind==1?zero:!zero;}else{if(!pop(b)||!pop(a)||a.kind!=Kind::Integer||b.kind!=Kind::Integer)return Error(c::Error::Stack);const auto x=int32_t(a.number),y=int32_t(b.number);switch(kind){case 3:take=x==y;break;case 4:take=x>=y;break;case 5:take=x>y;break;case 6:take=x<=y;break;case 7:take=x<y;break;case 8:take=x!=y;break;case 9:take=uint32_t(x)>=uint32_t(y);break;case 10:take=uint32_t(x)>uint32_t(y);break;case 11:take=uint32_t(x)<=uint32_t(y);break;case 12:take=uint32_t(x)<uint32_t(y);break;default:return Error(c::Error::Opcode);}}if(take){const auto delta=op.bytes==2?int32_t(int8_t(op.arg)):int32_t(op.arg);const auto target=int64_t(ip)+delta;if(target<0||target>=body.bytes)return Error(c::Error::Branch);ip=uint32_t(target);}continue;}
            if(op.op==0x65||op.op==0x66||op.op==0x69||op.op==0x6A||op.op==0x6D||op.op==0x6E){if(!pop(a)||a.kind!=Kind::Integer)return Error(c::Error::Stack);if(op.op==0x65)a.number=int32_t(0u-uint32_t(a.number));else if(op.op==0x66)a.number=int32_t(~uint32_t(a.number));else if(op.op==0x69)a.number=int32_t(a.number);else if(op.op==0x6D)a.number=uint32_t(a.number);if(!push(a))return false;continue;}
            if((op.op>=0x58&&op.op<=0x64)||(op.op>=0xFE01&&op.op<=0xFE05)){if(!pop(b)||!pop(a)||a.kind!=Kind::Integer||b.kind!=Kind::Integer)return Error(c::Error::Stack);uint32_t x=uint32_t(a.number),y=uint32_t(b.number),value=0;switch(op.op){case 0x58:value=x+y;break;case 0x59:value=x-y;break;case 0x5A:value=x*y;break;case 0x5B:case 0x5D:if(!y||(x==0x80000000&&y==0xFFFFFFFF))return Error(c::Error::Arithmetic);value=uint32_t(op.op==0x5B?int32_t(x)/int32_t(y):int32_t(x)%int32_t(y));break;case 0x5C:case 0x5E:if(!y)return Error(c::Error::Arithmetic);value=op.op==0x5C?x/y:x%y;break;case 0x5F:value=x&y;break;case 0x60:value=x|y;break;case 0x61:value=x^y;break;case 0x62:value=x<<(y&31);break;case 0x63:value=uint32_t(int32_t(x)>>(y&31));break;case 0x64:value=x>>(y&31);break;case 0xFE01:value=x==y;break;case 0xFE02:value=int32_t(x)>int32_t(y);break;case 0xFE03:value=x>y;break;case 0xFE04:value=int32_t(x)<int32_t(y);break;case 0xFE05:value=x<y;break;default:return Error(c::Error::Opcode);}if(!push({Kind::Integer,int32_t(value)}))return false;continue;}
            return Error(c::Error::Opcode,op.op);
        }return Error(c::Error::Body);
    }
};
bool EmptyArguments(){const auto* text=GetCommandLineW();if(!text)return false;
    uint32_t at=0;while(text[at]==' '||text[at]=='\t')++at;
    if(text[at]=='"'){++at;while(text[at]&&text[at]!='"')++at;if(text[at]!='"')return false;++at;}
    else while(text[at]&&text[at]!=' '&&text[at]!='\t')++at;
    while(text[at]==' '||text[at]=='\t')++at;return !text[at];
}
int Run(const void* bytes,uint32_t count,bool mapped){
    auto* vm=static_cast<VM*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(VM)));if(!vm)return int(0x8007000E);new(vm)VM();
    bool ok=vm->image.Open(bytes,count,mapped)&&vm->image.Validate();Value result;uint32_t exit=0;
    if(ok){c::Signature signature;c::Body body;ok=vm->image.Method(vm->image.entry,signature,body);Value args[1]{{Kind::Args,0}};if(ok&&signature.count&&!EmptyArguments())ok=vm->Error(c::Error::Arguments);if(ok)ok=vm->Call(vm->image.entry,args,signature.count,result,0);if(ok&&signature.result==c::Type::I4){ok=result.kind==Kind::Integer;exit=uint32_t(result.number);}}
    if(!ok){const auto* name=c::Name(vm->image.error);char message[160]="MININT_CIL rejected: ";uint32_t n=21;for(uint32_t i=0;name[i]&&n<150;++i)message[n++]=name[i];message[n++]='\n';DWORD wrote=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),message,n,&wrote,nullptr);exit=0x80131515;}
    vm->~VM();HeapFree(GetProcessHeap(),0,vm);return int(exit);
}
}
extern "C" BOOL WINAPI FrameworkCliEntry(HINSTANCE,DWORD,LPVOID){return TRUE;}
extern "C" int STDMETHODCALLTYPE MiniCorExeMain(){const auto* base=static_cast<const uint8_t*>(static_cast<const void*>(GetModuleHandleW(nullptr)));if(!base||c::U16(base)!=0x5A4D)return int(0x80131515);const auto pe=c::U32(base+60);if(pe>4096)return int(0x80131515);return Run(base,c::U32(base+pe+24+56),true);}
