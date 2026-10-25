#include "NativeServices.h"
#include "guardian/Telemetry.h"
namespace guardian::native {
namespace {
void Copy(void* to,const void* from,uint32_t n) noexcept {for(uint32_t i=0;i<n;++i)static_cast<uint8_t*>(to)[i]=static_cast<const uint8_t*>(from)[i];}
bool Equal(const char* a,const char* b) noexcept {for(uint32_t i=0;i<128;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
bool Prefix(const char* a,const char* b) noexcept {while(*b)if(*a++!=*b++)return false;return true;}
bool Protection(uint64_t p) noexcept {return p==memory::NoAccess||p==memory::ReadOnly||p==memory::ReadWrite||p==memory::ExecuteRead;}
}
bool Services::Initialize(memory::Manager& m,processes::Manager& p,threads::Scheduler& s,uint32_t pid,nt::Target target,uint64_t peb,Bridge bridge,storage::Volume* shared) noexcept {
    if(memory_||!p.Get(pid)||!target.id)return false;memory_=&m;processes_=&p;scheduler_=&s;pid_=pid;target_=target;peb_=peb;bridge_=bridge;
    calls_=unknown_=utc_=0;shared_=shared;cleanupOk_=true;if(shared_)return true;for(auto& byte:disk_)byte=0;
    volume_.Configure(this,Block,s);return volume_.Mount()==0;
}
bool Services::Probe(uint64_t at,uint32_t n,bool write) noexcept {
    if(!at||at>UINT64_MAX-n||n>(8u<<20))return false;
    for(uint32_t i=0;i<n;){if(!processes_->Translate(pid_,at+i,write))return false;auto bytes=4096u-uint32_t((at+i)&4095);i+=bytes<n-i?bytes:n-i;}return true;
}
bool Services::Read(uint64_t at,void* to,uint32_t n) noexcept {if(!Probe(at,n,false))return false;for(uint32_t i=0;i<n;){auto bytes=4096u-uint32_t((at+i)&4095);if(bytes>n-i)bytes=n-i;Copy(static_cast<uint8_t*>(to)+i,memory_->physical.At(processes_->Translate(pid_,at+i)),bytes);i+=bytes;}return true;}
bool Services::Write(uint64_t at,const void* from,uint32_t n) noexcept {if(!Probe(at,n,true))return false;for(uint32_t i=0;i<n;){auto bytes=4096u-uint32_t((at+i)&4095);if(bytes>n-i)bytes=n-i;Copy(memory_->physical.At(processes_->Translate(pid_,at+i,true)),static_cast<const uint8_t*>(from)+i,bytes);i+=bytes;}return true;}
bool Services::Output(uint64_t at,uint64_t value) noexcept {return Write(at,&value,8);}
bool Services::Text(uint64_t address,char* out,uint32_t capacity) noexcept {
    Unicode s{};if(!Read(address,&s,sizeof(s))||s.padding||(s.length&1)||s.length>s.maximum||uint32_t(s.length)/2>=capacity||(s.length&&!s.buffer))return false;
    for(uint32_t i=0;i<uint32_t(s.length)/2;++i){uint16_t ch=0;if(!Read(s.buffer+i*2,&ch,2)||ch<32||ch>126)return false;out[i]=char(ch);}out[s.length/2]=0;return true;
}
uint32_t Services::ObjectName(uint64_t address,char* out,uint32_t kind,bool optional) noexcept {
    out[0]=0;if(!address)return optional?0:Invalid;Attributes a{};
    if(!Read(address,&a,sizeof(a)))return AccessViolation;
    if(a.length!=sizeof(a)||a.padding||a.reserved||(a.attributes&~0x40u)||a.security||a.qos)return Unsupported;
    char text[128]{};if(a.name&&!Text(a.name,text,sizeof(text)))return AccessViolation;
    if(a.root){auto* h=Find(a.root);if(!h||h->kind!=kind)return InvalidHandle;
#ifdef GUARDIAN_SECURITY_NATIVE
        if(kind==3){uint32_t id=0;const auto st=Files().CheckAccess(processes_->Get(pid_)->token,h->path,0,&id);if(st==storage::NotFound||(!st&&id!=h->index))return 0xC000017C;if(st)return st;}
#endif
        uint32_t n=0;while(h->path[n]){out[n]=h->path[n];++n;}
        if(n&&out[n-1]!='\\'){if(n>=127)return Invalid;out[n++]='\\';}for(uint32_t i=0;text[i];++i){if(n>=127)return Invalid;out[n++]=text[i];}out[n]=0;return 0;}
    const char* source=text;
    if(kind==2&&Prefix(source,"\\??\\"))source+=4;
    if(kind==3){if(Prefix(source,"\\Registry\\User\\CurrentUser\\")){Copy(out,"HKCU\\",5);source+=27;}
        else if(Prefix(source,"\\Registry\\Machine\\")){Copy(out,"HKLM\\",5);source+=18;}
        else if(!Prefix(source,"HKCU")&&!Prefix(source,"HKLM"))return Unsupported;}
    uint32_t n=out[0]?5:0;for(uint32_t i=0;source[i];++i){if(n>=127)return Invalid;out[n++]=source[i];}out[n]=0;
    if(kind==1&&out[0])return Unsupported;return 0;
}
Services::Handle* Services::Find(uint64_t id) noexcept {for(auto& h:handles_)if(h.id==id&&id)return &h;return nullptr;}
Services::Handle* Services::New(uint32_t kind,uint32_t access,uint64_t raw,uint32_t index) noexcept {if(nextHandle_==UINT64_MAX)return nullptr;for(auto& h:handles_)if(!h.id){h={};h.id=nextHandle_++;h.kind=kind;h.access=access;h.raw=raw;h.index=index;return &h;}return nullptr;}
void Services::DropSection(uint32_t index) noexcept {auto& s=sections_[index];if(s.references&&!--s.references){
#ifdef GUARDIAN_WIN32_NATIVE
    if(s.file){cleanupOk_=!FlushSection(s)&&cleanupOk_;storage::Command c;c.op=storage::Op::Close;c.handle=s.file;cleanupOk_=!Files().Execute(pid_,processes_->Get(pid_)->token,c)&&cleanupOk_;}
#endif
    for(uint32_t i=0;i<s.pages;++i)memory_->physical.Release(s.data[i],memory::Frame::Data);s={};}}
uint32_t Services::Close(uint64_t id) noexcept {
    auto* h=Find(id);if(!h){if(bridge_.extraClose&&(id&0xFFFFFFFF00000000ull)==0x5365000000000000ull)return bridge_.extraClose(bridge_.context,id);return processes_->Close(pid_,id);}uint32_t result=0;
    if(h->kind==1)DropSection(h->index);
    else if(h->kind==4)result=processes_->Close(pid_,h->raw);
    else if(h->kind==2){storage::Command c;c.op=storage::Op::Close;c.handle=h->raw;result=Files().Execute(pid_,processes_->Get(pid_)->token,c);}
    if(!result)*h={};return result;
}
uint32_t Services::Process(uint64_t handle,uint32_t& pid,uint32_t rights) noexcept {if(handle==CurrentProcess){pid=pid_;return 0;}auto* h=Find(handle);if(h&&h->kind==4)handle=h->raw;processes::Object* p=nullptr;const auto status=processes_->Resolve(pid_,handle,processes::Kind::Process,rights,p);if(!status)pid=p->target;return status;}
bool Services::Deadline(uint64_t pointer,uint64_t now,uint64_t utc,uint64_t& result) noexcept {
    result=threads::Never;if(!pointer)return true;int64_t value=0;if(!Read(pointer,&value,8)||value==INT64_MIN)return false;
    const auto ticks=value<0?uint64_t(-value):(uint64_t(value)>utc?uint64_t(value)-utc:0);
    if(ticks>(threads::Never-now-1)/100)return false;result=now+ticks*100;return true;
}
bool Services::Block(void* raw,io::Op op,uint64_t offset,uint8_t* bytes) noexcept {auto& s=*static_cast<Services*>(raw);if(op==io::Op::Flush)return true;
    if(offset>sizeof(s.disk_)-512||(offset&511)||!bytes)return false;
    if(op==io::Op::Read)Copy(bytes,s.disk_+offset,512);else if(op==io::Op::Write)Copy(s.disk_+offset,bytes,512);else return false;return true;
}
uint32_t Services::Execute(Op op,const uint64_t* a,uint32_t tid,uint64_t teb,uint64_t now,uint64_t utc,bool inApc) noexcept {
    if(!memory_||!a||processes_->Owner(tid)!=pid_)return Denied;++calls_;utc_=utc;
    auto domain=telemetry::Domain::Control;auto action=telemetry::Operation::Query;
    if(op>=Op::Allocate&&op<=Op::UnmapSection){domain=telemetry::Domain::Memory;
        action=op==Op::Free||op==Op::UnmapSection?telemetry::Operation::Delete:(op==Op::Protect?telemetry::Operation::Protect:(op==Op::QueryMemory?telemetry::Operation::Query:telemetry::Operation::Create));}
    if(op==Op::CreateThread||op==Op::ResumeThread||op==Op::TerminateThread||op==Op::OpenThread||op==Op::QueryThread||op==Op::QueueApc){domain=telemetry::Domain::Thread;
        action=op==Op::CreateThread?telemetry::Operation::Create:(op==Op::TerminateThread?telemetry::Operation::Exit:telemetry::Operation::Query);}
    if(op==Op::OpenProcess||op==Op::QueryProcess||op==Op::TerminateProcess){domain=telemetry::Domain::Process;action=op==Op::TerminateProcess?telemetry::Operation::Exit:telemetry::Operation::Query;}
    auto e=telemetry::Begin(domain,action,pid_,processes_->Get(pid_)->token.user,"native NT service","",uint32_t(op));
    if(!telemetry::Before(e))return telemetry::End(e,Denied);
    uint32_t status=NotImplemented;
    if(op>=Op::Allocate&&op<=Op::QueryMemory)status=Vm(op,a);
    else if(op>=Op::CreateSection&&op<=Op::UnmapSection)status=Sections(op,a);
    else if(op==Op::Close)status=Close(a[0]);
    else if(op>=Op::CreateEvent&&op<=Op::TestAlert)status=Sync(op,a,tid,now,utc,inApc);
    else if(op>=Op::CreateFile&&op<Op::Count)status=Files(op,a);
    else if(op==Op::QueryTime)status=Output(a[0],utc)?0:AccessViolation;
    else if(op==Op::QueryCounter){const auto frequency=uint64_t(1000000000);status=!Probe(a[0],8,true)||(a[1]&&!Probe(a[1],8,true))?AccessViolation:0;if(!status){Output(a[0],now);if(a[1])Output(a[1],frequency);}}
    else if(op==Op::FlushInstructionCache){uint32_t target=0;status=Process(a[0],target,processes::WriteMemory);if(!status&&target!=pid_)status=Denied;if(!status&&a[2]&&(!a[1]||a[2]>UINT32_MAX||!Probe(a[1],uint32_t(a[2]),false)))status=AccessViolation;}
    else if(op==Op::CreateThread){uint32_t target=0;status=Process(a[3],target,processes::CreateChild);if(!status&&target!=pid_)status=Denied;
        if(!status&&(!Probe(a[0],8,true)||a[2]||a[6]>1||a[7]||a[8]||a[9]||a[10]))status=Invalid;
        if(!status){auto* entry=New(4,uint32_t(a[1]));if(!entry)status=NoMemory;else {uint32_t created=0;uint64_t handle=0;status=bridge_.thread?bridge_.thread(bridge_.context,a[4],a[5],a[6]!=0,created,handle):Unsupported;
            if(!status){entry->raw=handle;Output(a[0],entry->id);}else *entry={};}}}
    else if(op==Op::TerminateThread||op==Op::TerminateProcess){const auto self=op==Op::TerminateProcess?CurrentProcess:CurrentThread;status=a[0]!=self?Denied:(bridge_.exit?bridge_.exit(bridge_.context,uint32_t(a[1]),op==Op::TerminateProcess):Unsupported);}
    else if(op==Op::ResumeThread){auto* entry=Find(a[0]);processes::Object* object=nullptr;status=!entry||entry->kind!=4?InvalidHandle:((entry->access&(2|0x10000000))?processes_->Resolve(pid_,entry->raw,processes::Kind::Thread,processes::Modify,object):Denied);
        if(!status&&a[1]&&!Probe(a[1],4,true))status=AccessViolation;if(!status){uint32_t previous=0;status=scheduler_->Resume(object->target,previous);if(!status&&a[1])Write(a[1],&previous,4);}}
    else status=Information(op,a,tid,teb);
    if(status==NotImplemented)++unknown_;return telemetry::End(e,status);
}
bool Services::Exit() noexcept {
    if(!memory_)return true;
    bool ok=true;for(auto& range:ranges_)if(range.base){const auto section=range.section;ok=processes_->Release(pid_,range.base)==0&&ok;if(section>=0)DropSection(uint32_t(section));range={};}
    for(auto& h:handles_)if(h.id)ok=Close(h.id)==0&&ok;Files().Exit(pid_);if(!shared_)ok=volume_.Unmount()==0&&ok;ok=cleanupOk_&&ok;
    if(ok){memory_=nullptr;processes_=nullptr;scheduler_=nullptr;pid_=0;peb_=0;bridge_={};}return ok;
}
#include "NativeMemory.inl"
#include "NativeObjects.inl"
#include "NativeFiles.inl"
#ifdef GUARDIAN_WIN32_NATIVE
#include "NativeWin32.inl"
#ifdef GUARDIAN_DESKTOP_NATIVE
#include "DesktopNative.inl"
#endif
#endif
}
