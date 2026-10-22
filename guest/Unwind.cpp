#include "Unwind.h"
namespace guardian::unwind {
namespace {
constexpr uint32_t Sp=4;
struct Function {uint32_t begin,end,info;};
bool Nonvolatile(uint32_t r) noexcept {return r==3 || r==5 || r==6 || r==7 || r>=12;}
bool Span(uint64_t at,uint64_t bytes,uint64_t low,uint64_t high) noexcept {return low<=at && at<=high && bytes<=high-at;}
bool Add(uint64_t& at,uint64_t n) noexcept {if(at>UINT64_MAX-n)return false;at+=n;return true;}
bool Signed(uint64_t base,int64_t offset,uint64_t& value) noexcept {
    if(offset<0){const auto n=static_cast<uint64_t>(-offset);if(base<n)return false;value=base-n;}
    else {value=base;if(!Add(value,static_cast<uint64_t>(offset)))return false;}return true;
}
struct Reader {
    const Backend& api;void* context;const Stack& stack;const Image& image;
    bool Meta(uint32_t rva,void* out,uint32_t n) const noexcept {return Span(rva,n,0,image.size) && api.read(context,image.base+rva,out,n);}
    bool Code(uint64_t at,void* out,uint32_t n,const Function& fn) const noexcept {return Span(at,n,image.base+fn.begin,image.base+fn.end) && api.read(context,at,out,n);}
    bool Saved(uint64_t at,void* out,uint32_t n) const noexcept {return Span(at,n,stack.low,stack.high) && api.read(context,at,out,n);}
    bool Pop(Context& c,uint64_t& value) const noexcept {if(!Saved(c.gpr[Sp],&value,8))return false;return Add(c.gpr[Sp],8);}
};
bool Valid(const Image& i,const Function& f) noexcept {return f.begin<f.end && f.end<=i.size && !(f.info&3) && Span(f.info,4,0,i.size);}
Status Lookup(const Reader& r,uint64_t pc,Function& f,bool& found) noexcept {
    found=false;uint32_t low=0,high=r.image.count;const auto offset=pc-r.image.base;
    while(low<high){const auto mid=low+(high-low)/2;Function x{};
        if(!r.Meta(r.image.table+mid*12,&x,12))return Status::Access;if(!Valid(r.image,x))return Status::Invalid;
        if(offset<x.begin)high=mid;else if(offset>=x.end)low=mid+1;else {f=x;found=true;return Status::Ok;}}
    return Status::Ok;
}
// Recognition is speculative: an ordinary body instruction must not change
// the virtual context. Only a complete ABI epilog is committed.
Status Epilog(const Reader& r,const Function& fn,uint32_t frameRegister,Context& c,bool& found) noexcept {
    found=false;Context work=c;uint64_t pc=c.rip;bool adjusted=false,popped=false;
    for(uint32_t instruction=0;instruction<20;++instruction){
        if(pc>=r.image.base+fn.end)return Status::Ok;uint8_t b=0;if(!r.Code(pc,&b,1,fn))return Status::Access;
        uint32_t length=1;uint8_t rex=0,op=b;
        if(b>=0x40 && b<=0x4F){rex=b;if(!r.Code(pc+1,&op,1,fn))return Status::Access;length=2;}
        if(!adjusted && !popped && rex==0x48 && (op==0x83 || op==0x81)){
            uint8_t mod=0;if(!r.Code(pc+length,&mod,1,fn))return Status::Access;if(mod!=0xC4)return Status::Ok;
            int64_t amount=0;if(op==0x83){int8_t x=0;if(!r.Code(pc+length+1,&x,1,fn))return Status::Access;amount=x;length+=2;}
            else {int32_t x=0;if(!r.Code(pc+length+1,&x,4,fn))return Status::Access;amount=x;length+=5;}
            if(amount<=0 || (amount&7) || !Add(work.gpr[Sp],static_cast<uint64_t>(amount)))return Status::Invalid;adjusted=true;
        }else if(!adjusted && !popped && (rex==0x48 || rex==0x49) && op==0x8D){
            uint8_t mod=0;if(!r.Code(pc+length,&mod,1,fn))return Status::Access;const uint32_t mode=mod>>6,reg=(mod&7)+((rex&1)?8:0);
            if((mod&0x38)!=0x20 || (mode!=1 && mode!=2) || reg!=frameRegister || !Nonvolatile(reg))return Status::Ok;
            int64_t displacement=0;if(mode==1){int8_t x=0;if(!r.Code(pc+length+1,&x,1,fn))return Status::Access;displacement=x;length+=2;}
            else {int32_t x=0;if(!r.Code(pc+length+1,&x,4,fn))return Status::Access;displacement=x;length+=5;}
            if(!Signed(work.gpr[reg],displacement,work.gpr[Sp]))return Status::Invalid;adjusted=true;
        }else if(op>=0x58 && op<=0x5F && (!rex || rex==0x41)){
            const auto reg=(op-0x58)+((rex&1)?8:0);if(!Nonvolatile(reg))return Status::Ok;
            if(!r.Pop(work,work.gpr[reg]))return Status::Access;popped=true;
        }else {
            bool terminal=!rex && op==0xC3;
            if(!rex && op==0xC2){uint16_t n=0;if(!r.Code(pc+1,&n,2,fn))return Status::Access;terminal=true;}
            if(!rex && op==0xF3){uint8_t n=0;if(!r.Code(pc+1,&n,1,fn))return Status::Access;terminal=n==0xC3;}
            if(!rex && (op==0xE9 || op==0xEB)){
                int64_t displacement=0;uint32_t size=0;
                if(op==0xEB){int8_t n=0;if(!r.Code(pc+1,&n,1,fn))return Status::Access;displacement=n;size=2;}
                else {int32_t n=0;if(!r.Code(pc+1,&n,4,fn))return Status::Access;displacement=n;size=5;}
                uint64_t target=0;if(!Signed(pc+size,displacement,target))return Status::Invalid;
                terminal=target<r.image.base+fn.begin || target>=r.image.base+fn.end;
            }
            if((!rex || rex==0x48) && op==0xFF){uint8_t mod=0;if(!r.Code(pc+length,&mod,1,fn))return Status::Access;
                if(mod==0x25){int32_t displacement=0;if(!r.Code(pc+length+1,&displacement,4,fn))return Status::Access;terminal=true;}}
            if(!terminal)return Status::Ok;
            if(!r.Pop(work,work.rip))return Status::Access;c=work;found=true;return Status::Ok;
        }
        pc+=length;
    }
    return Status::Limit;
}
bool SupportedFlags(uint32_t flags,bool inspect) noexcept {
    return flags==0 || flags==4 || (inspect && flags<=3);
}
// Before SET_FPREG executes, the incoming frame register still belongs to
// the caller. Skipping operand slots is essential: their bytes are not codes.
bool FrameEstablished(const uint16_t* nodes,uint32_t count,uint64_t control) noexcept {
    for(uint32_t at=0;at<count;){const auto slot=nodes[at++];const uint32_t op=(slot>>8)&15,info=slot>>12;
        if(op==3 && (slot&255)<=control)return true;
        const uint32_t extra=op==1?(info?2u:1u):((op==4 || op==8)?1u:((op==5 || op==9)?2u:0u));
        if(extra>count-at)return false;at+=extra;
    }return false;
}
Status Restore(const Reader& r,Function fn,Context& c,bool inspect,HandlerType type,Handler& handler) noexcept {
    uint8_t originalFrame=0;bool machine=false,body=false;uint64_t establisher=c.gpr[Sp];
    for(uint32_t depth=0;depth<MaxChain;++depth){
        uint8_t h[4]{};if(!r.Meta(fn.info,h,4))return Status::Access;
        const auto flags=h[0]>>3,frame=h[3]&15,offset=h[3]>>4;
        if((h[0]&7)!=1 || !SupportedFlags(flags,inspect))return Status::Unsupported;
        if(h[2]>64 || h[1]>fn.end-fn.begin || (!frame && offset) || (frame && !Nonvolatile(frame)))return Status::Invalid;
        if(depth && h[3]!=originalFrame)return Status::Invalid;originalFrame=h[3];
        const uint32_t tail=fn.info+4+((h[2]+1u)&~1u)*2;
        const uint32_t trailer=flags==4?12u:(flags?4u:0u);
        if(!Span(fn.info,4+(flags?uint64_t((h[2]+1u)&~1u)*2+trailer:uint64_t(h[2])*2),0,r.image.size))return Status::Invalid;
        uint16_t nodes[64]{};if(h[2] && !r.Meta(fn.info+4,nodes,h[2]*2))return Status::Access;
        const auto control=depth?UINT64_MAX:c.rip-(r.image.base+fn.begin);uint32_t previous=256;
        if(!depth)body=control>=h[1];
        if(!depth && frame && (!inspect || control>=h[1] || FrameEstablished(nodes,h[2],control)) &&
           !Signed(c.gpr[frame],-int64_t(offset*16),establisher))return Status::Invalid;
        for(uint32_t at=0;at<h[2];){
            const auto slot=nodes[at++];const uint32_t code=slot&255,op=(slot>>8)&15,info=slot>>12;uint32_t extra=0;
            if(code>h[1] || code>previous)return Status::Invalid;previous=code;
            if(op==1){if(info>1)return Status::Invalid;extra=info?2:1;}
            else if(op==4 || op==8)extra=1;else if(op==5 || op==9)extra=2;
            else if(op!=0 && op!=2 && op!=3 && op!=10)return Status::Unsupported;
            if(at+extra>h[2])return Status::Invalid;
            uint64_t value=extra?nodes[at]:0;if(extra==2)value|=uint64_t(nodes[at+1])<<16;at+=extra;
            if(((op==0 || op==4 || op==5) && !Nonvolatile(info)) || ((op==8 || op==9) && info<6) ||
               (op==3 && (info || !frame)) || (op==10 && (info>1 || machine || at!=h[2] || flags)))return Status::Invalid;
            if(code>control)continue;
            uint64_t address=0;
            switch(op){
            case 0:if(!r.Pop(c,c.gpr[info]))return Status::Access;break;
            case 1:if(!info)value*=8;if(!value || (value&7) || !Add(c.gpr[Sp],value))return Status::Invalid;break;
            case 2:if(!Add(c.gpr[Sp],info*8+8))return Status::Invalid;break;
            case 3:if(!Signed(c.gpr[frame],-int64_t(offset*16),c.gpr[Sp]))return Status::Invalid;break;
            case 4:case 5:case 8:case 9:
                if(op==4)value*=8;if(op==8)value*=16;
                if(value&((op==8 || op==9)?15:7))return Status::Invalid;
                address=establisher;
                if(!Add(address,value))return Status::Invalid;
                if(op==4 || op==5){if(!r.Saved(address,&c.gpr[info],8))return Status::Access;}
                else if(!r.Saved(address,c.xmm[info],16))return Status::Access;break;
            case 10:
                address=c.gpr[Sp];if(!Add(address,info*8) || !r.Saved(address,&c.rip,8) || !Add(address,24) || !r.Saved(address,&c.gpr[Sp],8))return Status::Access;machine=true;break;
            default:return Status::Unsupported;
            }
            if(!Span(c.gpr[Sp],0,r.stack.low,r.stack.high))return Status::Invalid;
        }
        if(flags!=4){
            if(inspect && flags){uint32_t routine=0;if(!r.Meta(tail,&routine,4))return Status::Access;
                if(!routine || routine>=r.image.size || !r.api.executable(r.context,r.image.base+routine))return Status::Invalid;
                // Chained records inherit the primary handler. Only a completed
                // initial prolog is eligible; an epilog never enters Restore.
                if((flags&static_cast<uint32_t>(type)) && body){
                    if(!Span(establisher,0,r.stack.low,r.stack.high) || (establisher&7))return Status::Invalid;
                    handler={r.image.base+routine,r.image.base+tail+4,establisher,r.image.base,
                             r.image.base+fn.begin,r.image.base+fn.end,r.image.base+fn.info};
                }
            }
            if(!machine && !r.Pop(c,c.rip))return Status::Access;return Status::Ok;
        }
        Function parent{};if(!r.Meta(fn.info+4+((h[2]+1u)&~1u)*2,&parent,12))return Status::Access;
        if(!Valid(r.image,parent) || parent.begin>=fn.begin)return Status::Invalid;
        Function table{};bool found=false;const auto status=Lookup(r,r.image.base+parent.begin,table,found);if(status!=Status::Ok)return status;
        if(!found || table.begin!=parent.begin || table.end!=parent.end || table.info!=parent.info)return Status::Invalid;fn=parent;
    }
    return Status::Limit;
}
Status Walk(const Backend& api,void* context,const Stack& stack,Context& c,bool inspect,HandlerType type,Handler& handler) noexcept {
    if(!api.image || !api.read || stack.low>=stack.high || !Span(c.gpr[Sp],8,stack.low,stack.high) || (c.gpr[Sp]&7))return Status::Invalid;
    if(!c.rip)return Status::End;Image image{};if(!api.image(context,c.rip,image))return Status::End;
    if(!image.size || image.base>UINT64_MAX-image.size || !Span(c.rip,1,image.base,image.base+image.size) || image.count>pe::MaxRuntimeFunctions ||
       (image.count && ((image.table&3) || !Span(image.table,uint64_t(image.count)*12,0,image.size))))return Status::Invalid;
    Reader r{api,context,stack,image};Function fn{};bool found=false;auto status=Lookup(r,c.rip,fn,found);if(status!=Status::Ok)return status;
    Context work=c;Handler candidate{};if(!found){if(!r.Pop(work,work.rip))return Status::Access;}
    else {uint8_t h[4]{};if(!r.Meta(fn.info,h,4))return Status::Access;if((h[0]&7)!=1 || !SupportedFlags(h[0]>>3,inspect))return Status::Unsupported;
        if(h[2]>64 || h[1]>fn.end-fn.begin || (!(h[3]&15) && (h[3]>>4)) || ((h[3]&15) && !Nonvolatile(h[3]&15)))return Status::Invalid;
        bool epilog=false;if(c.rip>=image.base+fn.begin+h[1]){status=Epilog(r,fn,h[3]&15,work,epilog);if(status!=Status::Ok)return status;}
        if(!epilog){status=Restore(r,fn,work,inspect,type,candidate);if(status!=Status::Ok)return status;}}
    if(work.gpr[Sp]<=c.gpr[Sp] || !Span(work.gpr[Sp],0,stack.low,stack.high) || (work.gpr[Sp]&7))return Status::Invalid;
    c=work;handler=candidate;return Status::Ok;
}
}
Status Step(const Backend& api,void* context,const Stack& stack,Context& c) noexcept {
    Handler unused{};return Walk(api,context,stack,c,false,HandlerType::Exception,unused);
}
Status InspectHandler(const Backend& api,void* context,const Stack& stack,HandlerType type,Context& c,Handler& handler) noexcept {
    if(!api.executable || (type!=HandlerType::Exception && type!=HandlerType::Termination))return Status::Invalid;
    return Walk(api,context,stack,c,true,type,handler);
}
Status Capture(const Backend& api,void* context,const Stack& stack,const Context& initial,uint32_t skip,uint32_t count,uint64_t* frames,uint32_t& captured,uint32_t& hash,bool handlers) noexcept {
    captured=hash=0;if(skip>MaxSkip || !count || count>MaxFrames || !frames)return Status::Invalid;
    Context c=initial;uint64_t result[MaxFrames]{};uint32_t n=0,sum=0;
    for(uint32_t i=0;i<skip+count;++i){Handler unused{};const auto status=handlers?InspectHandler(api,context,stack,HandlerType::Exception,c,unused):Step(api,context,stack,c);
        if(status==Status::End)break;if(status!=Status::Ok)return status;
        Image image{};if(!c.rip || !api.image(context,c.rip,image))break;
        if(i>=skip){result[n++]=c.rip;sum+=static_cast<uint32_t>(c.rip);}}
    for(uint32_t i=0;i<n;++i)frames[i]=result[i];captured=n;hash=sum;return Status::Ok;
}
}
