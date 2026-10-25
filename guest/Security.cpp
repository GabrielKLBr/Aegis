#include "Security.h"
namespace guardian::security {
namespace {
void Copy(void* out,const void* in,uint32_t n) noexcept {auto* d=static_cast<uint8_t*>(out);const auto* s=static_cast<const uint8_t*>(in);for(uint32_t i=0;i<n;++i)d[i]=s[i];}
bool Text(const char* p,uint32_t capacity,bool empty=false) noexcept {for(uint32_t i=0;i<capacity;++i){if(!p[i])return empty||i!=0;if(p[i]<32||p[i]>126)return false;}return false;}
bool Equal(const char* a,const char* b) noexcept {for(uint32_t i=0;i<128;++i){auto x=a[i],y=b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;if(!x)return true;}return false;}
bool Join(char* out,const char* a,const char* b) noexcept {uint32_t n=0;while(a[n]&&n<127){out[n]=a[n];++n;}if(n==127)return false;if(*b&&n&&out[n-1]!='\\')out[n++]='\\';for(uint32_t i=0;b[i];++i){if(n==127)return false;out[n++]=b[i];}out[n]=0;return true;}
uint32_t Error(uint32_t s) noexcept {switch(s){case 0:return 0;case storage::Denied:return 5;case storage::InvalidHandle:return 6;case storage::NotFound:return 2;case storage::Collision:return 183;case storage::Small:return 234;case storage::NoSpace:return 112;case processes::NoMemory:case storage::Quota:return 8;case storage::Corrupt:return 1015;case storage::NotEmpty:return 145;case storage::Unsupported:return 50;case storage::Invalid:return 87;case storage::TypeMismatch:return 13;default:return 1117;}}
uint32_t Rights(uint32_t a) noexcept {uint32_t r=0;if(a&1)r|=storage::Read;if(a&2)r|=storage::Write;if(a&4)r|=storage::CreateChild;if(a&8)r|=storage::List;if(a&16)r|=storage::Read;if(a&0x10000)r|=storage::Delete;if(a&0x20000)r|=storage::ReadAcl;if(a&0xC0000)r|=storage::WriteAcl;return r;}
bool ServiceName(const char* name) noexcept {if(!Text(name,32))return false;for(uint32_t i=0;name[i];++i)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_'||name[i]=='-'))return false;return true;}
bool Config(const ServiceConfig& c) noexcept {if(c.type!=16||c.start!=3||c.error>3||c.reserved>1||!ServiceName(c.name)||!Text(c.display,64,true)||!Text(c.image,128))return false;uint32_t n=0;for(;c.image[n];++n)if(c.image[n]=='\\'||c.image[n]=='/'||c.image[n]==':'||c.image[n]==' '||c.image[n]=='"')return false;return n>4&&Equal(c.image+n-4,".exe");}
processes::Token Admin() noexcept {processes::Token t;t.user=100;t.primaryGroup=10;t.integrity=2;return t;}
}
Core::Handle* Core::Find(uint32_t pid,uint64_t id,uint32_t kind) noexcept {for(auto& h:handles_)if(h.id&&h.id==id&&h.pid==pid&&(!kind||h.kind==kind))return &h;return nullptr;}
Core::Handle* Core::New(uint32_t pid,uint32_t kind,uint32_t access) noexcept {if(nextHandle_>UINT64_MAX-4)return nullptr;for(auto& h:handles_)if(!h.id){h={};h.id=nextHandle_;nextHandle_+=4;h.pid=pid;h.kind=kind;h.access=access;return &h;}return nullptr;}
uint32_t Core::Handles() const noexcept {uint32_t n=0;for(const auto& h:handles_)if(h.id)++n;return n;}
bool Core::Initialize(processes::Manager& p,threads::Scheduler& s,storage::Volume& v,Bridge b) noexcept {
    if(processes_)return processes_==&p&&volume_==&v;processes_=&p;scheduler_=&s;volume_=&v;bridge_=b;
    if(v.ProvisionUser(Admin())||v.ProvisionUser(LocalService()))return false;
    storage::Command list;list.op=storage::Op::RegEnum;Join(list.path,"HKLM\\System\\CurrentControlSet\\Services","");
    for(uint32_t i=0;i<MaxServices;++i){list.index=i;const auto st=v.Execute(1,Admin(),list);if(st==storage::NotFound)break;if(st||!ServiceName(list.name))return false;
        auto& svc=services_[i];storage::Command read;read.op=storage::Op::RegGet;if(ServicePath(list.name,read.path))return false;Copy(read.name,"Config",7);read.length=512;
        if(v.Execute(1,Admin(),read)||read.type!=3||read.transferred!=sizeof(ServiceConfig))return false;Copy(&svc.config,read.data,sizeof(svc.config));if(!Config(svc.config)||!Equal(svc.config.name,list.name))return false;svc.used=true;svc.deleted=svc.config.reserved!=0;
    }return true;
}
uint32_t Core::Close(uint32_t pid,uint64_t id,uint32_t expected) noexcept {auto* h=Find(pid,id);if(!h||(expected&&h->kind!=expected&&!(expected==4&&h->kind==3)))return 6;uint32_t st=0;if(h->kind==2)st=Error(processes_->Close(pid,h->raw));else if(h->kind==6&&h->raw){storage::Command c;c.op=storage::Op::CloseWatch;c.watch=h->raw;st=Error(volume_->Execute(pid,processes_->Get(pid)->token,c));}if(h->kind==1){for(auto& w:handles_)if(w.id&&w.kind==6&&w.pid==pid&&w.parent==h->id){storage::Command c;c.op=storage::Op::CloseWatch;c.watch=w.raw;volume_->Execute(pid,processes_->Get(pid)->token,c);w.raw=0;w.generation=1;}}const auto index=h->index;const auto kind=h->kind;*h={};if(kind==4)Collect(index);return st;}
void Core::Exit(uint32_t pid) noexcept {for(auto& h:handles_)if(h.id&&h.pid==pid)Close(pid,h.id);}
void Core::Poll() noexcept {for(uint32_t i=0;i<MaxServices;++i){auto& s=services_[i];if(s.object){bool exited=false;uint32_t code=0;if(processes_->ReferencedProcess(s.object,exited,code)&&exited){processes_->Dereference(s.object);s.object=0;s.status.pid=0;s.status.accepted=0;if(s.status.state!=1){s.status.state=1;s.status.exit=1067;s.status.specific=code;}s.control=0;}}Collect(i);}}
bool Core::Finish() noexcept {Poll();if(Handles()||volume_->Watches())return false;for(const auto& s:services_)if(s.object||s.status.pid||(s.used&&s.deleted))return false;return true;}
uint32_t Core::Execute(uint32_t pid,uint32_t tid,Request& r) noexcept {
    (void)tid;const auto* p=processes_?processes_->Get(pid):nullptr;if(!p||p->state!=processes::State::Live)return 6;
    if(r.magic!=Magic||r.version!=Version||r.bytes!=sizeof(r)||r.length>sizeof(r.data))return 87;Poll();r.returned=0;
    const auto op=uint32_t(r.op);if(r.op==Op::Close)return r.flags>6?87:Close(pid,r.handle,r.flags);if(op>=2&&op<=uint32_t(Op::RegPoll))return Registry(pid,r);
    if(op>=20&&op<=uint32_t(Op::Profile))return Tokens(pid,r);if(op>=40&&op<=uint32_t(Op::ServiceNextControl))return Services(pid,r);if(op>=60&&op<=uint32_t(Op::EventRead))return Events(pid,r);return 120;
}
uint32_t Core::Registry(uint32_t pid,Request& r) noexcept {
    const auto token=processes_->Get(pid)->token;storage::Command c;Handle* h=nullptr;const uint32_t root=uint32_t(r.handle);
    if(r.op==Op::RegPoll){auto* watch=Find(pid,r.handle,6);if(!watch)return 6;if(watch->generation){*watch={};return 0;}c.op=storage::Op::RegPoll;c.watch=watch->raw;const auto status=volume_->Execute(pid,token,c);if(status==storage::Pending)return 997;if(status)return Error(status);r.returned=c.transferred;*watch={};return 0;}
    if((root==0x80000001||root==0x80000002)&&(r.handle==root||r.handle==(0xFFFFFFFF00000000ull|root))){Copy(c.path,root==0x80000001?"HKCU":"HKLM",5);}
    else {h=Find(pid,r.handle,1);if(!h)return 6;uint32_t id=0;const auto st=volume_->CheckAccess(token,h->path,0,&id);if(st==storage::NotFound||(!st&&id!=h->generation))return 1018;if(st)return Error(st);Copy(c.path,h->path,128);}
    if(r.op==Op::RegOpen||r.op==Op::RegCreate){if(!Text(r.path,128,true)||r.flags||r.access&~0xF003Fu)return 87;
        if(r.op==Op::RegCreate&&h&&!(h->access&4))return 5;char path[128]{};if(!Join(path,c.path,r.path))return 206;Copy(c.path,path,128);
        storage::Kind kind;auto st=volume_->QueryKind(token,c.path,kind);const bool created=st==storage::NotFound&&r.op==Op::RegCreate;
        if(st&&!created)return Error(st);if(!st&&kind!=storage::Kind::Key)return 13;auto* out=New(pid,1,r.access);if(!out)return 8;
        if(created){c.op=storage::Op::RegCreate;st=volume_->Execute(pid,token,c);}if(st){*out={};return Error(st);}
        uint32_t identity=0;st=volume_->CheckAccess(token,c.path,Rights(r.access),&identity);if(st){*out={};return Error(st);}Copy(out->path,c.path,128);out->generation=identity;r.handle=out->id;r.returned=created?1:2;return 0;
    }
    const auto require=[&](uint32_t access) noexcept {return !h||(h->access&access)==access;};
    if(r.op==Op::RegFlush){if(!require(1))return 5;return Error(volume_->Flush());}
    if(r.op==Op::RegSecurity||r.op==Op::RegSetSecurity){if(!require(r.op==Op::RegSecurity?0x20000:0x40000))return 5;c.op=r.op==Op::RegSecurity?storage::Op::QueryAcl:storage::Op::SetAcl;c.length=sizeof(r.security);if(r.op==Op::RegSetSecurity)Copy(c.data,&r.security,sizeof(r.security));const auto st=volume_->Execute(pid,token,c);if(!st&&r.op==Op::RegSecurity)Copy(&r.security,c.data,sizeof(r.security));return Error(st);}
    if(!Text(r.name,32,true))return 87;Copy(c.name,r.name,32);c.length=r.length;c.type=r.type;Copy(c.data,r.data,512);
    switch(r.op){case Op::RegSet:if(!require(2))return 5;c.op=storage::Op::RegSet;break;
    case Op::RegQuery:if(!require(1))return 5;c.op=storage::Op::RegGet;c.length=512;break;
    case Op::RegDeleteValue:if(!require(2))return 5;c.op=storage::Op::RegDeleteValue;break;
    case Op::RegEnum:if(!require(r.flags?1:8)||r.flags>1)return 5;c.op=storage::Op::RegEnum;c.index=r.index;c.flags=r.flags;c.length=512;break;
    case Op::RegDeleteKey:{if(!Text(r.path,128))return 87;if(!require(0x10000))return 5;char path[128]{};if(!Join(path,c.path,r.path))return 206;Copy(c.path,path,128);c.op=storage::Op::RegDeleteKey;break;}
    case Op::RegWatch:{if(!require(16))return 5;if(r.flags>1||!r.access||r.access&~15u)return 87;auto* out=New(pid,6,0);if(!out)return 8;c.op=storage::Op::RegWatch;c.access=r.access;c.flags=r.flags;const auto status=volume_->Execute(pid,token,c);if(status){*out={};return Error(status);}out->raw=c.watch;out->parent=r.handle;r.other=out->id;return 0;}
    default:return 120;}
    const auto st=volume_->Execute(pid,token,c);r.returned=c.transferred;r.type=c.type;if(st==storage::NotFound&&r.op==Op::RegEnum)return 259;if(st)return Error(st);
    if(r.op==Op::RegEnum&&r.flags){c.op=storage::Op::RegGet;c.status=c.transferred=0;const auto read=volume_->Execute(pid,token,c);if(read)return Error(read);r.returned=c.transferred;r.type=c.type;}
    if(r.op==Op::RegQuery||r.op==Op::RegEnum){Copy(r.data,c.data,512);Copy(r.name,c.name,32);if(r.op==Op::RegEnum&&!r.flags)Copy(r.text,c.other,128);if(r.op==Op::RegQuery&&r.length<r.returned)return 234;}return 0;
}
uint32_t Core::Tokens(uint32_t pid,Request& r) noexcept {
    auto* p=processes_->Get(pid);if(r.op==Op::TokenOpen){if(r.other!=UINT64_MAX&&r.other!=0)return 50;if(!r.access||r.access&~0xBu)return 5;auto* h=New(pid,2,r.access);if(!h)return 8;const auto st=processes_->CloneHandle(pid,p->tokenHandle,h->raw);if(st){*h={};return Error(st);}h->index=1;r.handle=h->id;return 0;}
    Handle* h=nullptr;if(r.handle){h=Find(pid,r.handle,2);if(!h)return 6;}processes::Token token=p->token;if(h){const auto st=processes_->QueryToken(pid,h->raw,token);if(st)return Error(st);}
    if(r.op==Op::TokenDuplicate){if(!h||!(h->access&2))return 5;if(r.flags!=2||r.type<1||r.type>2||r.access&~0xBu)return 87;const auto access=r.access?r.access:h->access;if(access&~h->access)return 5;auto* out=New(pid,2,access);if(!out)return 8;const auto st=processes_->CloneHandle(pid,h->raw,out->raw);if(st){*out={};return Error(st);}out->index=r.type;r.handle=out->id;return 0;}
    if(h&&!(h->access&8))return 5;if(r.op==Op::TokenQuery){if(!h)return 6;r.token=token;r.type=h->index;return 0;}
    if(r.op==Op::Profile){const char* path=token.user==100?"C:\\Users\\MiniNT":token.user==200?"C:\\Users\\LocalService":nullptr;if(!path)return 1332;uint32_t n=0;while(path[n])++n;Copy(r.path,path,n+1);r.returned=n+1;return 0;}
    if(r.op==Op::AccessCheck){if(!h||h->index!=2)return 1309;if(!processes::Manager::Access(token,r.security,processes::Kind::Process,0))return 1338;r.returned=processes::Manager::Access(token,r.security,processes::Kind::Process,r.access)?1:0;return 0;}
    return 120;
}
uint32_t Core::ServicePath(const char* name,char* out) noexcept {if(!ServiceName(name))return 123;return Join(out,"HKLM\\System\\CurrentControlSet\\Services",name)?0:206;}
uint32_t Core::SaveService(uint32_t index,const processes::Token& token,bool remove) noexcept {
    auto& svc=services_[index];char path[128]{};if(ServicePath(svc.config.name,path))return 123;
    return Error(volume_->PersistRegistry(token,path,"Config",&svc.config,sizeof(svc.config),remove));
}
uint32_t Core::Collect(uint32_t index) noexcept {if(index>=MaxServices)return 87;auto& s=services_[index];if(!s.used||!s.deleted||s.object||s.status.pid)return 0;for(const auto& h:handles_)if(h.id&&h.kind==4&&h.index==index)return 0;const auto st=SaveService(index,Admin(),true);if(!st)s={};return st;}
uint32_t Core::Services(uint32_t pid,Request& r) noexcept {
    const auto token=processes_->Get(pid)->token;const bool admin=Administrator(token);
    if(r.op==Op::ScmOpen){if(r.flags||r.access&~0xF003Fu)return 87;if((r.access&~1u)&&!admin)return 5;auto* h=New(pid,3,r.access);if(!h)return 8;r.handle=h->id;return 0;}
    if(r.op==Op::ServiceAssignment||r.op==Op::ServiceStatus||r.op==Op::ServiceNextControl){for(uint32_t i=0;i<MaxServices;++i){auto& s=services_[i];if(!s.used||s.status.pid!=pid)continue;r.handle=i+1;
            if(r.op==Op::ServiceAssignment){Copy(r.data,&s.config,sizeof(s.config));r.length=sizeof(s.config);return 0;}
            if(r.op==Op::ServiceNextControl){r.returned=s.control;s.control=0;return 0;}
            const auto old=s.status.state;const auto next=r.service.state;if(r.service.type!=16||r.service.accepted&~1u||r.service.flags||next<1||next>4||
                !((old==2&&(next==2||next==4||next==1))||(old==4&&(next==4||next==3||next==1))||(old==3&&(next==3||next==1)))||((next==1||next==2||next==3)&&r.service.accepted))return 87;
            s.status=r.service;s.status.pid=pid;if(next==1)s.status.accepted=0;return 0;
        }return 1063;}
    if(r.op==Op::ServiceCreate||r.op==Op::ServiceOpen){auto* scm=Find(pid,r.handle,3);if(!scm)return 6;if(!ServiceName(r.name)||r.access&~0xF01FFu)return 87;if(r.op==Op::ServiceOpen&&!(scm->access&1))return 5;
        uint32_t index=MaxServices;for(uint32_t i=0;i<MaxServices;++i)if(services_[i].used&&Equal(services_[i].config.name,r.name)){index=i;break;}
        Handle* output=nullptr;if(r.op==Op::ServiceCreate){if(!(scm->access&2)||!admin)return 5;if(index!=MaxServices)return services_[index].deleted?1072:1073;
            ServiceConfig config;Copy(&config,r.data,sizeof(config));if(!Config(config)||config.reserved||!Equal(config.name,r.name))return 50;for(uint32_t i=0;i<MaxServices;++i)if(!services_[i].used){index=i;break;}if(index==MaxServices)return 8;
            output=New(pid,4,r.access);if(!output)return 8;auto& svc=services_[index];svc={};svc.used=true;svc.config=config;const auto st=SaveService(index,token,false);if(st){*output={};svc={};return st;}
        }else if(index==MaxServices)return 1060;
        auto& svc=services_[index];if(svc.deleted)return 1072;if(!admin&&(r.access&~5u))return 5;auto* h=output?output:New(pid,4,r.access);if(!h)return 8;h->index=index;r.handle=h->id;return 0;
    }
    auto* h=Find(pid,r.handle,4);if(!h||h->index>=MaxServices||!services_[h->index].used)return 6;auto& svc=services_[h->index];
    if(r.op==Op::ServiceQuery){if(r.flags>1)return 87;if(!(h->access&(r.flags?16u:4u)))return 5;r.service=svc.status;return 0;}
    if(r.op==Op::ServiceConfig){if(!(h->access&1))return 5;Copy(r.data,&svc.config,sizeof(svc.config));r.length=sizeof(svc.config);return 0;}
    if(r.op==Op::ServiceDelete){if(!admin||!(h->access&0x10000))return 5;if(svc.deleted)return 1072;svc.config.reserved=1;const auto st=SaveService(h->index,token,false);if(st){svc.config.reserved=0;return st;}svc.deleted=true;return 0;}
    if(r.op==Op::ServiceStart){if(!admin||!(h->access&16))return 5;if(svc.deleted)return 1072;if(svc.object||svc.status.state!=1)return 1056;if(!bridge_.start)return 50;
        uint32_t child=0;uint64_t process=0;const auto st=bridge_.start(bridge_.context,pid,svc.config.image,child,process);if(st)return Error(st);
        uint32_t object=0;const auto ref=processes_->Reference(pid,process,processes::Query,object);processes_->Close(pid,process);if(ref){if(bridge_.stop)bridge_.stop(bridge_.context,pid,child);return Error(ref);}
        svc.object=object;svc.creator=pid;svc.control=0;svc.status={};svc.status.state=2;svc.status.pid=child;return 0;
    }
    if(r.op==Op::ServiceControl){if(!admin||!(h->access&32))return 5;if(r.flags!=1)return 50;if(svc.status.state==1||!svc.object)return 1062;if(svc.status.state!=4||!(svc.status.accepted&1))return 1061;if(svc.control)return 1052;svc.control=1;r.service=svc.status;return 0;}
    return 120;
}
uint32_t Core::Events(uint32_t pid,Request& r) noexcept {
    if(r.op==Op::EventRegister){if(!ServiceName(r.name))return 87;auto* h=New(pid,5,1);if(!h)return 8;Copy(h->path,r.name,32);r.handle=h->id;return 0;}
    auto* h=Find(pid,r.handle,5);if(!h)return 6;if(r.op==Op::EventWrite){if(!Text(r.text,128,true)||(r.type!=1&&r.type!=2&&r.type!=4)||eventSequence_==UINT32_MAX)return 87;auto& e=events_[eventSequence_%MaxEvents];e={};e.number=++eventSequence_;e.user=processes_->Get(pid)->token.user;e.id=r.index;e.type=r.type;const auto utc=bridge_.clock?bridge_.clock(bridge_.context):0;if(utc>=116444736000000000ull&&(utc-116444736000000000ull)/10000000<=UINT32_MAX)e.time=uint32_t((utc-116444736000000000ull)/10000000);Copy(e.source,h->path,32);Copy(e.text,r.text,128);if(eventCount_<MaxEvents)++eventCount_;return 0;}
    if(r.op==Op::EventRead){if(r.index>=eventCount_)return 38;const auto& e=events_[(eventSequence_-eventCount_+r.index)%MaxEvents];r.index=e.number;r.type=e.type;r.flags=e.user;r.returned=e.id;r.service.hint=e.time;Copy(r.name,e.source,32);Copy(r.text,e.text,128);return 0;}return 120;
}
}
