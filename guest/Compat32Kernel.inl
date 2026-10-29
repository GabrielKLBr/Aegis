#include "guardian/Compat32.h"
extern "C" const volatile char GuardianCompat32Capability[]="GuardianCompat32/1/pe32";
namespace {
bool CompatProbe(Context& c,uint32_t at,uint32_t bytes,bool write=false) noexcept {
    if(!at||bytes>0x100000||uint64_t(at)+bytes>0x80000000ull)return false;
    for(uint32_t i=0;i<bytes;){if(!c.processes->Translate(c.pid,uint64_t(at)+i,write))return false;auto step=4096-((at+i)&4095);i+=step<bytes-i?step:bytes-i;}return true;
}
bool CompatText(Context& c,uint32_t at,char* out,bool wide=false) noexcept {
    if(!at)return false;for(uint32_t i=0;i<128;++i){uint16_t value=0;if(!Read(&c,uint64_t(at)+i*(wide?2:1),&value,wide?2u:1u)||value>126)return false;out[i]=char(value);if(!value)return true;}return false;
}
}
bool PeCompatDispatch(uint32_t pid,uint32_t tid,uint64_t now,const TrapFrame& f,uint32_t& status) noexcept {
    using namespace guardian;auto* slot=Find(pid);if(!slot)return false;auto& c=slot->context;
    status=87;
    if(f.cs==0x2B&&f.rax==compat32::StartGate&&f.rip>=cpu::UserCode&&f.rip<cpu::UserCode+4096){
        uint64_t address=0;const auto index=slot->catalog.Find("kernel32.dll");
        if(slot->image.Resolve(index,"GuardianCompatEntry",0,address)&&address<0x80000000)status=uint32_t(address);else status=0;return true;
    }
    if(f.cs!=0x1B)return false;
    if(f.rdx>UINT32_MAX||f.rip>UINT32_MAX||f.rsp>UINT32_MAX)return true;
    if(f.rax==0xAB){
        if(!NativeCode(*slot,"win32u.dll",f.rip)||!CompatProbe(c,uint32_t(f.rdx),sizeof(nativeuser::Call),true))return true;
        nativeuser::Call call;if(!Read(&c,f.rdx,&call,sizeof(call))||call.magic!=nativeuser::Magic||call.version!=nativeuser::Version||call.bytes!=sizeof(call)||!nativegui::Empty(&call.reply,sizeof(call.reply)))return true;
        if(call.command.op==nativeuser::Op::Hello||call.command.op==nativeuser::Op::Finish||call.command.op==nativeuser::Op::ThreadExit)return true;
        auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,pid,c.processes->Get(pid)->token.user,"x86 USER32","",uint32_t(call.command.op));
        if(!telemetry::Before(event)){status=telemetry::End(event,5);return true;}
        if(!NativeUserStart()||nativeUserCompleted){status=telemetry::End(event,50);return true;}
        nativeUserPid=pid;nativeUserTid=tid;nativeuser::Packet packet;packet.command=call.command;
        if(!NativeUserExchange(packet)){status=telemetry::End(event,87);return true;}
        call.reply=packet.reply;call.reply.restrictions=call.reply.helperPid=0;for(auto& count:call.reply.calls)count=0;
        status=telemetry::End(event,Write(&c,f.rdx,&call,sizeof(call))?call.reply.status:998);return true;
    }
    if(f.rax!=compat32::Gate){status=50;RuntimeRejected(pid,f.rax,status);return true;}
    if(!NativeCode(*slot,"kernel32.dll",f.rip)||!CompatProbe(c,uint32_t(f.rdx),sizeof(compat32::Request),true))return true;
    compat32::Request r;if(!Read(&c,f.rdx,&r,sizeof(r))||r.magic!=compat32::Magic||r.version!=compat32::Version||r.bytes!=sizeof(r)||r.value||r.error)return true;
    auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,pid,c.processes->Get(pid)->token.user,"x86 Win32 thunk","",uint32_t(r.op));
    if(!telemetry::Before(event)){status=telemetry::End(event,5);return true;}
    r.error=0;
    switch(r.op){
    case compat32::Op::Info:r.args[0]=pid;r.args[1]=tid;r.args[2]=uint32_t(slot->image.Get(0)->base);break;
    case compat32::Op::Clock:{const auto ms=now/1000000;r.args[0]=uint32_t(ms);r.args[1]=uint32_t(ms>>32);break;}
    case compat32::Op::Console:{
        if((r.args[0]!=uint32_t(-11)&&r.args[0]!=uint32_t(-12))||r.args[2]>65536||!CompatProbe(c,r.args[1],r.args[2])){r.error=87;break;}
        uint8_t data[512]{};for(uint32_t at=0;at<r.args[2];){auto count=r.args[2]-at;if(count>512)count=512;uint32_t done=0;
            if(!Read(&c,uint64_t(r.args[1])+at,data,count)||Console(nullptr,pid,data,count,done)||done!=count){r.error=5;break;}at+=count;r.value=at;}break;}
    case compat32::Op::Allocate:{
        if(r.args[0]||r.args[2]!=0x3000||r.args[3]!=memory::ReadWrite||!r.args[1]||r.args[1]>0x100000){r.error=87;break;}
        uint64_t base=0;const auto bytes=(r.args[1]+4095)&~4095u;
        r.error=c.processes->Reserve(pid,base,bytes,memory::ReadWrite);
        if(!r.error){r.error=c.processes->Commit(pid,base,bytes,memory::ReadWrite);if(r.error)c.processes->Release(pid,base);else r.value=uint32_t(base);}break;}
    case compat32::Op::Release:{
        if(r.args[1]||r.args[2]!=0x8000){r.error=87;break;}
        bool image=false;for(uint32_t i=0;i<pe::MaxModules;++i)if(const auto* module=slot->image.Get(i);module&&r.args[0]>=module->base&&r.args[0]<module->base+module->size)image=true;
        // Free only regions created by the x86 allocator, never bootstrap/TEB/stacks.
        uint32_t* owned=nullptr;for(auto& region:slot->compatAllocations)if(region==r.args[0]){owned=&region;break;}
        r.error=image||!owned?87:c.processes->Release(pid,r.args[0]);if(!r.error)*owned=0;break;}
    case compat32::Op::Module:{
        uint32_t index=0;if(r.args[0]){char name[128]{};if(!CompatText(c,r.args[0],name,true)){r.error=87;break;}index=slot->catalog.Find(name);}
        const auto* module=slot->image.Get(index);if(module)r.value=uint32_t(module->base);else r.error=126;break;}
    case compat32::Op::Symbol:{
        uint32_t index=UINT32_MAX;for(uint32_t i=0;i<pe::MaxModules;++i)if(const auto* module=slot->image.Get(i);module&&module->base==r.args[0])index=i;
        char name[128]{};uint64_t address=0;if(index==UINT32_MAX||r.args[1]<65536||!CompatText(c,r.args[1],name)||!slot->image.Resolve(index,name,0,address)||address>UINT32_MAX)r.error=127;else r.value=uint32_t(address);break;}
    case compat32::Op::Exit:telemetry::End(event,0);status=ProcessNativeTerminate(pid,tid,r.args[0],true);return true;
    default:r.error=50;break;
    }
    if(r.op==compat32::Op::Allocate&&!r.error){bool added=false;for(auto& region:slot->compatAllocations)if(!region){region=r.value;added=true;break;}if(!added){c.processes->Release(pid,r.value);r.value=0;r.error=8;}}
    status=Write(&c,f.rdx,&r,sizeof(r))?0:998;telemetry::End(event,r.error?r.error:status);return true;
}
