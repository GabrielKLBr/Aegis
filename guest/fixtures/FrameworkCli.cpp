#include <Windows.h>
#include <stdint.h>
#include <new>
#include "guardian/Cli.h"
namespace c=guardian::cli;
extern "C" const int GuardianCliAnchor=1411;
const void* volatile GuardianCliRelocation=&GuardianCliAnchor;
namespace {
enum class Kind:uint32_t {Integer,String,Null,Args,Object,Array,Text,BoxI4,BoxBool};
struct Value {Kind kind=Kind::Integer;int64_t number=0;};
struct Frame {Value stack[c::MaxStack]{},locals[c::MaxLocals]{},args[c::MaxArgs+1]{};uint32_t top=0;};
struct Object {uint32_t type=0;Value fields[c::MaxFields]{};};
struct Array {c::Type element=c::Type::Void;uint32_t length=0;Value values[c::MaxElements]{};};
struct Text {uint32_t length=0;uint16_t data[c::MaxChars+1]{};};
struct VM {
    c::Image image;Frame frames[c::MaxFrames]{};uint32_t instructions=0;uint32_t output=0;
    Object objects[c::MaxObjects]{};Array arrays[c::MaxArrays]{};Text texts[c::MaxStrings]{};
    Value statics[c::MaxFields]{};uint8_t initialized[c::MaxTypes+1]{};
    uint32_t objectCount=0,arrayCount=0,textCount=0;
    static bool Reference(c::Type type){return type==c::Type::String||type==c::Type::Object||type==c::Type::Args||type==c::Type::I4Array||type==c::Type::ObjectArray;}
    static bool IsString(Value value){return value.kind==Kind::String||value.kind==Kind::Text;}
    Object* GetObject(Value value){return value.kind==Kind::Object&&value.number>0&&value.number<=objectCount?&objects[uint32_t(value.number)-1]:nullptr;}
    Array* GetArray(Value value){return value.kind==Kind::Array&&value.number>0&&value.number<=arrayCount?&arrays[uint32_t(value.number)-1]:nullptr;}
    bool TextValue(Value value,uint16_t* data,uint32_t& count){count=0;if(value.kind==Kind::Null)return true;
        if(value.kind==Kind::Text){if(value.number<=0||value.number>textCount)return Error(c::Error::Stack);const auto& t=texts[uint32_t(value.number)-1];count=t.length;for(uint32_t i=0;i<count;++i)data[i]=t.data[i];return true;}
        const uint8_t* raw=nullptr;if(value.kind!=Kind::String||!image.Literal(uint32_t(value.number),raw,count))return Error(c::Error::Stack);
        for(uint32_t i=0;i<count;++i)data[i]=c::U16(raw+i*2);return true;
    }
    bool NewText(const uint16_t* data,uint32_t count,Value& value){if(count>c::MaxChars||textCount>=c::MaxStrings)return Error(c::Error::Quota);
        auto& text=texts[textCount++];text.length=count;for(uint32_t i=0;i<count;++i)text.data[i]=data[i];value={Kind::Text,textCount};return true;
    }
    bool InitType(uint32_t type,uint32_t depth){if(!type||type>c::MaxTypes)return Error(c::Error::Token);if(initialized[type])return initialized[type]<3;
        initialized[type]=1;const auto method=image.Initializer(type);if(method){Value unused;if(!Call(method,nullptr,0,unused,depth+1,true)){initialized[type]=3;return false;}}initialized[type]=2;return true;
    }
    bool Error(c::Error e,uint32_t detail=0){if(image.error==c::Error::Ok){image.error=e;image.detail=detail;}return false;}
    bool PrintBytes(const char* data,uint32_t count){
        if(count>c::MaxOutput-output)return Error(c::Error::Output);
        for(uint32_t at=0;at<count;){auto n=count-at>256?256u:count-at;
            if(n<count-at)while(n&&(uint8_t(data[at+n])&0xC0)==0x80)--n;
            DWORD done=0;if(!n||!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),data+at,n,&done,nullptr)||done!=n)return Error(c::Error::Output);
            at+=n;output+=n;
        }return true;
    }
    bool Conforms(c::Type type,Value value,uint32_t classToken=0){
        if(classToken>>24==2){if(value.kind==Kind::Null)return true;const auto* object=GetObject(value);return object&&object->type==(classToken&0xFFFFFF);}
        if(type==c::Type::I4||type==c::Type::Bool)return value.kind==Kind::Integer;
        if(type==c::Type::Args||type==c::Type::I4Array||type==c::Type::ObjectArray){if(value.kind==Kind::Null)return true;if(type==c::Type::Args&&value.kind==Kind::Args)return true;const auto* array=GetArray(value);return array&&array->element==(type==c::Type::Args?c::Type::String:type==c::Type::I4Array?c::Type::I4:c::Type::Object);}
        if(type==c::Type::String)return IsString(value)||value.kind==Kind::Null;
        if(type==c::Type::Object)return IsString(value)||value.kind==Kind::Null||value.kind==Kind::Args||value.kind==Kind::Object||value.kind==Kind::Array||value.kind==Kind::BoxI4||value.kind==Kind::BoxBool;
        return false;
    }
    bool Print(Value value,c::Type type){
        if(value.kind==Kind::Null)return type==c::Type::String||type==c::Type::Object||Error(c::Error::Stack);
        if(IsString(value)){if(type!=c::Type::String&&type!=c::Type::Object)return Error(c::Error::Stack);uint16_t text[c::MaxChars]{};uint32_t count=0;if(!TextValue(value,text,count))return false;char bytes[2048]{};uint32_t at=0;for(uint32_t i=0;i<count;++i){uint32_t point=text[i];if(point>=0xD800&&point<=0xDBFF){const auto low=text[++i];point=0x10000+((point-0xD800)<<10)+(low-0xDC00);}if(point<128)bytes[at++]=char(point);else if(point<2048){bytes[at++]=char(0xC0|(point>>6));bytes[at++]=char(0x80|(point&63));}else if(point<65536){bytes[at++]=char(0xE0|(point>>12));bytes[at++]=char(0x80|((point>>6)&63));bytes[at++]=char(0x80|(point&63));}else{bytes[at++]=char(0xF0|(point>>18));bytes[at++]=char(0x80|((point>>12)&63));bytes[at++]=char(0x80|((point>>6)&63));bytes[at++]=char(0x80|(point&63));}}return PrintBytes(bytes,at);}
        if(value.kind==Kind::Object&&type==c::Type::Object){const auto* object=GetObject(value);char name[128]{};if(!object||!image.TypeName(0x02000000|object->type,name))return Error(c::Error::Stack);uint32_t length=0;while(name[length])++length;return PrintBytes(name,length);}
        if(value.kind==Kind::Array&&type==c::Type::Object){const auto* array=GetArray(value);if(!array)return Error(c::Error::Stack);const auto* name=array->element==c::Type::I4?"System.Int32[]":array->element==c::Type::String?"System.String[]":"System.Object[]";uint32_t length=0;while(name[length])++length;return PrintBytes(name,length);}
        if(value.kind==Kind::Args&&type==c::Type::Object)return PrintBytes("System.String[]",15);
        if(value.kind==Kind::BoxI4||value.kind==Kind::BoxBool){if(type!=c::Type::Object)return Error(c::Error::Stack);if(value.number<=0||value.number>objectCount)return Error(c::Error::Stack);return Print(objects[uint32_t(value.number)-1].fields[0],value.kind==Kind::BoxI4?c::Type::I4:c::Type::Bool);}
        if(value.kind!=Kind::Integer)return Error(c::Error::Stack);
        if(type==c::Type::Bool)return value.number?PrintBytes("True",4):PrintBytes("False",5);
        if(type!=c::Type::I4)return Error(c::Error::Stack);
        char bytes[32]{},reverse[24]{};uint32_t n=0,at=0;const auto signedValue=int64_t(int32_t(value.number));uint64_t number=uint64_t(signedValue);if(signedValue<0){bytes[at++]='-';number=0-number;}do{reverse[n++]=char('0'+number%10);number/=10;}while(number);while(n)bytes[at++]=reverse[--n];return PrintBytes(bytes,at);
    }
    bool StringCall(const char* name,const Value* args,uint32_t count,Value& result){
        if(c::Equal(name,"IsNullOrEmpty")){uint16_t data[c::MaxChars]{};uint32_t length=0;if(count!=1||!TextValue(args[0],data,length))return Error(c::Error::Stack);result={Kind::Integer,length==0};return true;}
        if(c::Equal(name,"get_Length")){uint16_t data[c::MaxChars]{};uint32_t length=0;if(count!=1||!TextValue(args[0],data,length))return Error(c::Error::Stack);result={Kind::Integer,length};return true;}
        if(c::Equal(name,"Concat")){uint16_t joined[c::MaxChars]{},part[c::MaxChars]{};uint32_t total=0;
            for(uint32_t i=0;i<count;++i){uint32_t length=0;if(!TextValue(args[i],part,length))return false;if(length>c::MaxChars-total)return Error(c::Error::Quota);for(uint32_t j=0;j<length;++j)joined[total++]=part[j];}
            return NewText(joined,total,result);
        }
        if(count!=2)return Error(c::Error::UnsupportedCall);bool equal=args[0].kind==Kind::Null&&args[1].kind==Kind::Null;
        if(args[0].kind!=Kind::Null&&args[1].kind!=Kind::Null){uint16_t a[c::MaxChars]{},b[c::MaxChars]{};uint32_t na=0,nb=0;if(!TextValue(args[0],a,na)||!TextValue(args[1],b,nb))return false;equal=na==nb;for(uint32_t i=0;equal&&i<na;++i)equal=a[i]==b[i];}
        result={Kind::Integer,c::Equal(name,"op_Inequality")?!equal:equal};return true;
    }
    bool Call(uint32_t token,const Value* args,uint32_t count,Value& result,uint32_t depth,bool initializer=false){
        if(depth>=c::MaxFrames)return Error(c::Error::Quota);
        if(token>>24==10){char type[128]{},name[128]{};c::Signature sig;if(!image.Member(token,type,name,sig)||count!=sig.count+(sig.instance?1u:0u))return Error(c::Error::Token,token);
            if(sig.instance){if(!count||args[0].kind==Kind::Null)return Error(c::Error::Null);}
            for(uint32_t i=0;i<sig.count;++i)if(!Conforms(sig.args[i],args[i+(sig.instance?1:0)],sig.argClass[i]))return Error(c::Error::Stack);
            if(c::Equal(type,"System.Object")&&c::Equal(name,".ctor"))return count==1&&GetObject(args[0])!=nullptr||Error(c::Error::Stack);
            if(c::Equal(type,"System.Console")){if(count&&!Print(args[0],sig.args[0]))return false;return !c::Equal(name,"WriteLine")||PrintBytes("\n",1);}
            if(c::Equal(type,"System.String"))return StringCall(name,args,count,result);
            return Error(c::Error::UnsupportedCall,token);
        }
        c::Signature sig;c::Body body;if(!image.Method(token,sig,body)||sig.count+(sig.instance?1u:0u)!=count)return Error(c::Error::Signature,token);
        const auto owner=image.Owner(6,token&0xFFFFFF);if(!owner)return Error(c::Error::Token,token);
        if(image.MethodNamed(token,".cctor")&&(!initializer||sig.instance||sig.count||sig.result!=c::Type::Void))return Error(c::Error::Signature,token);
        if(!initializer&&!InitType(owner,depth))return false;
        if(sig.instance){if(args[0].kind==Kind::Null)return Error(c::Error::Null);const auto* object=GetObject(args[0]);if(!object||object->type!=owner)return Error(c::Error::Stack);}
        for(uint32_t i=0;i<sig.count;++i)if(!Conforms(sig.args[i],args[i+(sig.instance?1:0)],sig.argClass[i]))return Error(c::Error::Stack,token);
        auto& f=frames[depth];f={};for(uint32_t i=0;i<count;++i)f.args[i]=args[i];for(uint32_t i=0;i<body.locals;++i)if(Reference(body.local[i]))f.locals[i].kind=Kind::Null;
        auto push=[&](Value v){if(f.top>=body.maxStack||f.top>=c::MaxStack)return Error(c::Error::Stack);f.stack[f.top++]=v;return true;};
        auto pop=[&](Value& v){if(!f.top)return Error(c::Error::Stack);v=f.stack[--f.top];return true;};
        uint32_t ip=0;while(ip<body.bytes){if(++instructions>c::MaxInstructions)return Error(c::Error::Instructions);c::Instruction op;if(!c::Decode(body.code,body.bytes,ip,op))return Error(c::Error::Opcode,ip);ip+=op.bytes;Value a{},b{};
            if(op.op==0)continue;
            if(op.op>=2&&op.op<=5){if(op.op-2>=count||!push(f.args[op.op-2]))return Error(c::Error::Stack);continue;}
            if(op.op>=6&&op.op<=9){if(op.op-6>=body.locals||!push(f.locals[op.op-6]))return Error(c::Error::Stack);continue;}
            if(op.op>=10&&op.op<=13){if(op.op-10>=body.locals||(!pop(f.locals[op.op-10])||!Conforms(body.local[op.op-10],f.locals[op.op-10],body.localClass[op.op-10])))return Error(c::Error::Stack);continue;}
            if(op.op==0x0E){if(op.arg>=count||!push(f.args[op.arg]))return Error(c::Error::Stack);continue;}
            if(op.op==0x10){if(op.arg>=count||(!pop(f.args[op.arg])||!(sig.instance&&op.arg==0?GetObject(f.args[op.arg])!=nullptr:Conforms(sig.args[op.arg-(sig.instance?1u:0u)],f.args[op.arg],sig.argClass[op.arg-(sig.instance?1u:0u)]))))return Error(c::Error::Stack);continue;}
            if(op.op==0x11){if(op.arg>=body.locals||!push(f.locals[op.arg]))return Error(c::Error::Stack);continue;}
            if(op.op==0x13){if(op.arg>=body.locals||(!pop(f.locals[op.arg])||!Conforms(body.local[op.arg],f.locals[op.arg],body.localClass[op.arg])))return Error(c::Error::Stack);continue;}
            if(op.op==0x14){if(!push({Kind::Null,0}))return false;continue;}
            if(op.op>=0x15&&op.op<=0x1E){if(!push({Kind::Integer,int64_t(op.op)-0x16}))return false;continue;}
            if(op.op==0x1F||op.op==0x20){if(!push({Kind::Integer,op.op==0x1F?int64_t(int8_t(op.arg)):int64_t(int32_t(op.arg))}))return false;continue;}
            if(op.op==0x25){if(!f.top||!push(f.stack[f.top-1]))return Error(c::Error::Stack);continue;}
            if(op.op==0x26){if(!pop(a))return false;continue;}
            if(op.op==0x72){if(!push({Kind::String,op.arg}))return false;continue;}
            if(op.op==0x8E){if(!pop(a))return false;if(a.kind==Kind::Null)return Error(c::Error::Null);const auto* array=GetArray(a);if(a.kind!=Kind::Args&&!array)return Error(c::Error::Stack);if(!push({Kind::Integer,array?array->length:0}))return false;continue;}
            if(op.op==0x8C||op.op==0xA5){c::Type type;if(!image.Element(op.arg,type)||!pop(a))return Error(c::Error::Token);const auto boxed=type==c::Type::Bool?Kind::BoxBool:Kind::BoxI4;
                if(op.op==0x8C){if(a.kind!=Kind::Integer)return Error(c::Error::Stack);if(objectCount>=c::MaxObjects)return Error(c::Error::Quota);auto& box=objects[objectCount++];box.type=boxed==Kind::BoxI4?0xFFFFFFFFu:0xFFFFFFFEu;box.fields[0]=a;a={boxed,objectCount};}else{if(a.kind==Kind::Null)return Error(c::Error::Null);if(a.kind!=boxed||a.number<=0||a.number>objectCount)return Error(c::Error::Stack);a=objects[uint32_t(a.number)-1].fields[0];}if(!push(a))return false;continue;}
            if(op.op==0x8D){c::Type element;if(!image.Element(op.arg,element)||!pop(a)||a.kind!=Kind::Integer)return Error(c::Error::Stack);
                if(a.number<0||a.number>c::MaxElements||arrayCount>=c::MaxArrays)return Error(c::Error::Quota);auto& array=arrays[arrayCount++];array.element=element;array.length=uint32_t(a.number);for(uint32_t i=0;i<array.length;++i)array.values[i]=element==c::Type::I4?Value{}:Value{Kind::Null,0};if(!push({Kind::Array,arrayCount}))return false;continue;}
            if(op.op==0x94||op.op==0x9A||op.op==0x9E||op.op==0xA2){const bool store=op.op==0x9E||op.op==0xA2;Value value;if(store&&!pop(value))return false;if(!pop(b)||b.kind!=Kind::Integer||!pop(a))return Error(c::Error::Stack);
                if(a.kind==Kind::Null)return Error(c::Error::Null);auto* array=GetArray(a);if(!array){if(a.kind==Kind::Args)return Error(c::Error::Bounds);return Error(c::Error::Stack);}
                if(b.number<0||uint64_t(b.number)>=array->length)return Error(c::Error::Bounds);const bool integer=op.op==0x94||op.op==0x9E;if(integer!=(array->element==c::Type::I4))return Error(c::Error::Stack);
                if(store){if(!Conforms(array->element,value))return Error(c::Error::Stack);array->values[uint32_t(b.number)]=value;}else if(!push(array->values[uint32_t(b.number)]))return false;continue;}
            if(op.op==0x7B||op.op==0x7D||op.op==0x7E||op.op==0x80){c::Image::FieldInfo field;if(!image.Field(op.arg,field))return Error(c::Error::Token);const bool store=op.op==0x7D||op.op==0x80;
                if(store&&!pop(b))return false;if(!InitType(field.owner,depth))return false;Value* target=nullptr;
                if(field.isStatic)target=&statics[field.index];else {if(!pop(a))return false;if(a.kind==Kind::Null)return Error(c::Error::Null);auto* object=GetObject(a);if(!object||object->type!=field.owner)return Error(c::Error::Stack);target=&object->fields[field.index];}
                if(store){if(!Conforms(field.type,b,field.classToken)||field.readOnly&&(!image.MethodNamed(token,field.isStatic?".cctor":".ctor")||owner!=field.owner))return Error(c::Error::Stack);*target=b;}else if(!push(*target))return false;continue;}
            if(op.op==0x73){c::Signature ctor;c::Body unused;if(!image.Method(op.arg,ctor,unused)||!ctor.instance||f.top<ctor.count||objectCount>=c::MaxObjects)return Error(c::Error::Quota);
                const auto type=image.Owner(6,op.arg&0xFFFFFF);if(!InitType(type,depth))return false;auto& object=objects[objectCount++];object.type=type;Value objectValue{Kind::Object,objectCount};
                for(uint32_t i=0;i<image.Fields();++i){c::Image::FieldInfo field;if(!image.Field(0x04000000|(i+1),field))return Error(c::Error::Token);if(field.owner==type&&Reference(field.type))object.fields[i]={Kind::Null,0};}
                Value arguments[c::MaxArgs+1]{};arguments[0]=objectValue;for(uint32_t i=ctor.count;i;--i)if(!pop(arguments[i]))return false;Value unusedResult;if(!Call(op.arg,arguments,ctor.count+1,unusedResult,depth+1)||!push(objectValue))return false;continue;}
            if(op.op==0x28||op.op==0x6F){c::Signature called;c::Body unused;char type[128]{},name[128]{};if(op.arg>>24==6){if(!image.Method(op.arg,called,unused))return false;}else if(!image.Member(op.arg,type,name,called))return Error(c::Error::Token,op.arg);const auto n=called.count+(called.instance?1u:0u);if(n>c::MaxArgs+1||f.top<n)return Error(c::Error::Stack);Value arguments[c::MaxArgs+1]{};for(uint32_t i=n;i;--i)if(!pop(arguments[i-1]))return false;Value returned;if(op.op==0x6F&&(!called.instance||arguments[0].kind==Kind::Null))return Error(c::Error::Null);if(!Call(op.arg,arguments,n,returned,depth+1))return false;if(called.result!=c::Type::Void&&!push(returned))return false;continue;}
            if(op.op==0x2A){if(sig.result==c::Type::Void){if(f.top)return Error(c::Error::Stack);result={};return true;}if(f.top!=1||!pop(result)||!Conforms(sig.result,result,sig.resultClass))return Error(c::Error::Stack);return true;}
            if(op.op>=0x2B&&op.op<=0x44){const auto kind=op.bytes==2?op.op-0x2B:op.op-0x38;bool take=false;if(kind==0)take=true;else if(kind<=2){if(!pop(a))return false;const bool zero=a.kind==Kind::Null||(a.kind==Kind::Integer&&!a.number);take=kind==1?zero:!zero;}else{if(!pop(b)||!pop(a))return Error(c::Error::Stack);if((kind==3||kind==8)&&a.kind!=Kind::Integer&&b.kind!=Kind::Integer){const bool equal=a.kind==b.kind&&a.number==b.number;take=kind==3?equal:!equal;}else{if(a.kind!=Kind::Integer||b.kind!=Kind::Integer)return Error(c::Error::Stack);const auto x=int32_t(a.number),y=int32_t(b.number);switch(kind){case 3:take=x==y;break;case 4:take=x>=y;break;case 5:take=x>y;break;case 6:take=x<=y;break;case 7:take=x<y;break;case 8:take=x!=y;break;case 9:take=uint32_t(x)>=uint32_t(y);break;case 10:take=uint32_t(x)>uint32_t(y);break;case 11:take=uint32_t(x)<=uint32_t(y);break;case 12:take=uint32_t(x)<uint32_t(y);break;default:return Error(c::Error::Opcode);}}}if(take){const auto delta=op.bytes==2?int32_t(int8_t(op.arg)):int32_t(op.arg);const auto target=int64_t(ip)+delta;if(target<0||target>=body.bytes)return Error(c::Error::Branch);ip=uint32_t(target);}continue;}
            if(op.op==0x65||op.op==0x66||op.op==0x69||op.op==0x6A||op.op==0x6D||op.op==0x6E){if(!pop(a)||a.kind!=Kind::Integer)return Error(c::Error::Stack);if(op.op==0x65)a.number=int32_t(0u-uint32_t(a.number));else if(op.op==0x66)a.number=int32_t(~uint32_t(a.number));else if(op.op==0x69)a.number=int32_t(a.number);else if(op.op==0x6D)a.number=uint32_t(a.number);if(!push(a))return false;continue;}
            if(op.op==0xFE01&&f.top>=2&&f.stack[f.top-1].kind!=Kind::Integer&&f.stack[f.top-2].kind!=Kind::Integer){if(!pop(b)||!pop(a)||!push({Kind::Integer,a.kind==b.kind&&a.number==b.number}))return false;continue;}
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
    if(ok){for(uint32_t i=0;i<vm->image.Fields();++i){c::Image::FieldInfo field;if(!vm->image.Field(0x04000000|(i+1),field)){ok=false;break;}if(field.isStatic&&VM::Reference(field.type))vm->statics[i]={Kind::Null,0};}}
    if(ok){c::Signature signature;c::Body body;ok=vm->image.Method(vm->image.entry,signature,body);Value args[1]{{Kind::Args,0}};if(ok&&signature.count&&!EmptyArguments())ok=vm->Error(c::Error::Arguments);if(ok)ok=vm->Call(vm->image.entry,args,signature.count,result,0);if(ok&&signature.result==c::Type::I4){ok=result.kind==Kind::Integer;exit=uint32_t(result.number);}}
    if(!ok){const auto* name=c::Name(vm->image.error);char message[160]="MININT_CIL rejected: ";uint32_t n=21;for(uint32_t i=0;name[i]&&n<150;++i)message[n++]=name[i];message[n++]='\n';DWORD wrote=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),message,n,&wrote,nullptr);exit=0x80131515;}
    vm->~VM();HeapFree(GetProcessHeap(),0,vm);return int(exit);
}
}
extern "C" BOOL WINAPI FrameworkCliEntry(HINSTANCE,DWORD,LPVOID){return TRUE;}
extern "C" int STDMETHODCALLTYPE MiniCorExeMain(){const auto* base=static_cast<const uint8_t*>(static_cast<const void*>(GetModuleHandleW(nullptr)));if(!base||c::U16(base)!=0x5A4D)return int(0x80131515);const auto pe=c::U32(base+60);if(pe>4096)return int(0x80131515);return Run(base,c::U32(base+pe+24+56),true);}
