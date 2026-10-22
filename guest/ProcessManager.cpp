#include "ProcessManager.h"
#include "guardian/Telemetry.h"
namespace guardian::processes {
namespace {
uint32_t Allowed(Kind k) noexcept {
    const uint32_t common=Wait|Query|Duplicate|ReadControl|WriteDac;
    if(k==Kind::Device)return Query|Modify|Duplicate|ReadMemory|WriteMemory|ReadControl|WriteDac;
    if(k==Kind::Process)return common|ReadMemory|WriteMemory|Terminate|CreateChild;
    if(k==Kind::Token)return Query|Duplicate|ReadControl|WriteDac;
    if(k==Kind::Thread)return common|Modify|Terminate;
    return k!=Kind::Empty?common|Modify:0;
}
bool Member(const Token& t,uint32_t group,bool deny) noexcept {
    if(t.primaryGroup==group)return true;
    for(uint32_t i=0;i<t.groupCount;++i)if(t.groups[i].id==group && (t.groups[i].flags==1 || (deny && t.groups[i].flags==2)))return true;
    return false;
}
bool Match(const Token& t,const Ace& a) noexcept {return a.principal==Principal::Everyone || (a.principal==Principal::User && a.id==t.user) || (a.principal==Principal::Group && Member(t,a.id,a.deny!=0));}
bool ValidSecurity(const Security& s,Kind k) noexcept {
    if(!s.owner || !s.group || s.integrity>2 || s.daclPresent>1 || s.noWriteUp>1 || s.aceCount>MaxAces || (!s.daclPresent && s.aceCount))return false;
    bool allow=false;
    for(uint32_t i=0;i<s.aceCount;++i) {const auto& a=s.aces[i];if(a.principal>Principal::Group || a.deny>1 || (a.rights&~Allowed(k)) || (a.principal!=Principal::Everyone && !a.id) || (a.deny && allow))return false;if(!a.deny)allow=true;}
    return true;
}
bool Equal(const char* a,const char* b) noexcept {for(unsigned i=0;i<64;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
threads::Kind SyncKind(Kind k) noexcept {
    switch(k){case Kind::Mutex:return threads::Kind::Mutex;case Kind::Semaphore:return threads::Kind::Semaphore;case Kind::Timer:return threads::Kind::AutoTimer;case Kind::Critical:return threads::Kind::Critical;case Kind::Srw:return threads::Kind::Srw;case Kind::Process:return threads::Kind::ManualEvent;default:return threads::Kind::AutoEvent;}
}
}
bool Manager::ValidToken(const Token& t) noexcept {
    if(!t.user || !t.primaryGroup || t.integrity>2 || t.groupCount>MaxGroups || (t.privileges&~CreateGlobal))return false;
    for(uint32_t i=0;i<t.groupCount;++i){if(!t.groups[i].id || (t.groups[i].flags!=1 && t.groups[i].flags!=2) || t.groups[i].id==t.primaryGroup)return false;for(uint32_t j=0;j<i;++j)if(t.groups[i].id==t.groups[j].id)return false;}return true;
}
bool Manager::Restricted(const Token& p,const Token& c) noexcept {
    if(!ValidToken(p) || !ValidToken(c) || c.user!=p.user || c.primaryGroup!=p.primaryGroup || c.session!=p.session || c.integrity>p.integrity || (c.privileges&~p.privileges))return false;
    // A deny-only group must never disappear; dropping it could remove a deny.
    for(uint32_t i=0;i<p.groupCount;++i){bool found=false;for(uint32_t j=0;j<c.groupCount;++j)if(c.groups[j].id==p.groups[i].id){found=true;if(p.groups[i].flags==2 && c.groups[j].flags!=2)return false;}if(!found)return false;}
    for(uint32_t i=0;i<c.groupCount;++i){bool found=false;for(uint32_t j=0;j<p.groupCount;++j)if(c.groups[i].id==p.groups[j].id)found=true;if(!found)return false;}return true;
}
Security Manager::DefaultSecurity(const Token& t,Kind k) noexcept {Security s{};s.owner=t.user;s.group=t.primaryGroup;s.integrity=t.integrity;s.aceCount=1;s.aces[0]={Principal::User,t.user,Allowed(k),0};return s;}
uint32_t Manager::Normalize(Kind k,uint32_t desired,uint32_t& out) noexcept {
    const auto allowed=Allowed(k);if(!allowed)return TypeMismatch;uint32_t mask=desired&0x0FFFFFFF;
    if(desired&GenericAll)mask|=allowed;if(desired&GenericRead)mask|=(Query|ReadControl|(allowed&Wait));
    if(desired&GenericWrite)mask|=(allowed&(Modify|WriteMemory|WriteDac));if(desired&GenericExecute)mask|=(allowed&Wait);
    if(mask&~allowed)return Invalid;out=mask;return Success;
}
bool Manager::Access(const Token& t,const Security& s,Kind k,uint32_t rights) noexcept {
    if(!ValidToken(t) || !ValidSecurity(s,k) || (rights&~Allowed(k)))return false;
    const uint32_t writes=Modify|WriteMemory|Terminate|CreateChild|WriteDac|(k==Kind::Process?static_cast<uint32_t>(Duplicate):0u);
    if(s.noWriteUp && t.integrity<s.integrity && (rights&writes))return false;
    uint32_t remaining=rights;if(t.user==s.owner)remaining&=~uint32_t(ReadControl|WriteDac);
    if(!s.daclPresent)return true;
    for(uint32_t i=0;i<s.aceCount;++i)if(Match(t,s.aces[i])){if(s.aces[i].deny && (s.aces[i].rights&remaining))return false;if(!s.aces[i].deny)remaining&=~s.aces[i].rights;}
    return !remaining;
}
Process* Manager::Get(uint32_t id) noexcept {const auto slot=id&255u;if(!slot || slot>MaxProcesses)return nullptr;auto& p=processes_[slot-1];return p.id==id && p.state!=State::Empty?&p:nullptr;}
Object* Manager::ObjectById(uint32_t id) noexcept {const auto slot=id&255u;if(!slot || slot>MaxObjects)return nullptr;auto& o=objects_[slot-1];return o.id==id && o.kind!=Kind::Empty?&o:nullptr;}
uint32_t Manager::NewObject(Kind k,const Security& security,uint32_t raw,uint32_t target) noexcept {
    if(objectBudget_==0)return 0;
    for(uint32_t i=0;i<MaxObjects;++i)if(objects_[i].kind==Kind::Empty){const auto gen=objects_[i].generation+1;if(gen>=0xFFFFFF)continue;
        objects_[i]={};auto& o=objects_[i];o.id=(gen<<8)|(i+1);o.generation=gen;o.refs=1;o.kind=k;o.raw=raw;o.target=target;o.security=security;
        if(objectBudget_>0)--objectBudget_;return o.id;}
    return 0;
}
bool Manager::Dereference(uint32_t id) noexcept {
    auto* o=ObjectById(id);if(!o || !o->refs){consistent_=false;return false;}
    if(--o->refs)return true;
    if(o->handles || (o->raw && scheduler_->Close(o->raw)!=threads::Success)){consistent_=false;return false;}
    o->kind=Kind::Empty;return true;
}
Handle* Manager::Lookup(Process& p,uint64_t value) noexcept {if(!value)return nullptr;for(auto& h:p.handles)if(h.value==value)return &h;return nullptr;}
uint32_t Manager::Insert(Process& p,uint32_t oid,uint32_t rights,uint32_t attributes,uint64_t& out,uint64_t value) noexcept {
    auto* o=ObjectById(oid);if(!o || (attributes&~uint32_t(3)))return Invalid;
    if(handleBudget_==0 || !nextHandle_ || nextHandle_>UINT64_MAX-4 || o->refs==UINT32_MAX || o->handles==UINT32_MAX)return NoMemory;
    for(auto& h:p.handles)if(!h.value){const auto v=value?value:nextHandle_;if(Lookup(p,v))return Collision;h={v,oid,rights,attributes,0};
        if(!value)nextHandle_+=4;++o->refs;++o->handles;if(handleBudget_>0)--handleBudget_;out=v;return Success;}
    return Quota;
}
void Manager::DropHandle(Handle& h) noexcept {auto* o=ObjectById(h.object);if(!o || !o->handles){consistent_=false;return;}--o->handles;const auto id=h.object;h={};Dereference(id);}
uint32_t Manager::Resolve(uint32_t pid,uint64_t value,Kind kind,uint32_t required,Object*& out) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;auto* h=Lookup(*p,value);if(!h)return InvalidHandle;auto* o=ObjectById(h->object);
    if(!o)return InvalidHandle;if(kind!=Kind::Empty && o->kind!=kind)return TypeMismatch;if((h->access&required)!=required)return Denied;out=o;return Success;
}
uint32_t Manager::CreateRoot(const Token& token,uint32_t& pid) noexcept {return Create(token,0,nullptr,0,pid);}
uint32_t Manager::CreateChildProcess(uint32_t parent,const Token& token,const uint64_t* handles,uint32_t count,uint32_t& pid) noexcept {
    auto* p=Get(parent);if(!p || p->state!=State::Live)return Terminated;if(!Restricted(p->token,token))return Denied;return Create(token,parent,handles,count,pid);
}
uint32_t Manager::Create(const Token& token,uint32_t parent,const uint64_t* inherited,uint32_t count,uint32_t& pid) noexcept {
    if(!ValidToken(token) || count>MaxHandles-2 || (count && !inherited))return Invalid;
    auto* source=parent?Get(parent):nullptr;
    if(parent && !source)return Terminated;
    for(uint32_t i=0;i<count;++i){auto* h=Lookup(*source,inherited[i]);if(!h || !(h->attributes&Inherit))return Denied;auto* o=ObjectById(h->object);if(!o || !Access(token,o->security,o->kind,h->access))return Denied;for(uint32_t j=0;j<i;++j)if(inherited[j]==inherited[i])return Invalid;}
    Process* p=nullptr;for(auto& entry:processes_)if(entry.state==State::Empty && entry.generation<0xFFFFFE){p=&entry;break;}if(!p)return Quota;
    const auto slot=static_cast<uint32_t>(p-processes_);const auto gen=p->generation+1;*p={};p->generation=gen;p->id=(gen<<8)|(slot+1);p->state=State::Live;p->parent=parent;p->token=token;
    uint32_t status=NoMemory;
    if(!memory_->Create(p->space)){p->state=State::Empty;return NoMemory;}
    const auto raw=scheduler_->CreateObject(threads::Kind::ManualEvent);
    if(raw) p->processObject=NewObject(Kind::Process,DefaultSecurity(token,Kind::Process),raw,p->id);
    if(raw && !p->processObject)scheduler_->Close(raw);
    if(p->processObject)p->tokenObject=NewObject(Kind::Token,DefaultSecurity(token,Kind::Token),0,p->id);
    if(p->tokenObject){ObjectById(p->tokenObject)->token=token;status=Insert(*p,p->processObject,Allowed(Kind::Process),0,p->self);if(!status)status=Insert(*p,p->tokenObject,Allowed(Kind::Token),0,p->tokenHandle);}
    if(!status)for(uint32_t i=0;i<count;++i){const auto& h=*Lookup(*source,inherited[i]);uint64_t value=0;status=Insert(*p,h.object,h.access,h.attributes,value,h.value);if(status)break;}
    if(status){Kill(p->id,status);Reap(p->id);return status;}
    pid=p->id;return Success;
}
bool Manager::Name(const Token&,const char* name,bool& global,char* leaf) const noexcept {
    if(!name)return false;const char* prefix=nullptr;
    if(name[0]=='\\' && name[1]=='G') {prefix="\\Global\\";global=true;}else{prefix="\\Session\\";global=false;}
    uint32_t at=0;for(;prefix[at];++at)if(name[at]!=prefix[at])return false;
    uint32_t n=0;for(;n<63 && name[at+n];++n){const auto ch=name[at+n];if(ch<'!' || ch>'~' || ch=='\\' || ch=='/')return false;leaf[n]=ch;}
    if(!n || name[at+n])return false;leaf[n]=0;return true;
}
Object* Manager::FindName(const Token& token,const char* name) noexcept {
    char leaf[64]{};bool global=false;if(!Name(token,name,global,leaf))return nullptr;
    for(auto& o:objects_)if(o.kind!=Kind::Empty && o.name[0] && o.global==global && (global || o.session==token.session) && Equal(o.name,leaf))return &o;return nullptr;
}
uint32_t Manager::CreateObject(uint32_t pid,Kind kind,const char* name,const Security* sd,uint32_t desired,uint32_t attrs,uint64_t& handle,uint32_t initial,uint32_t maximum) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;if(kind==Kind::Empty || (kind>=Kind::Process && kind!=Kind::Device) || (attrs&~uint32_t(3)))return Invalid;
    uint32_t access=0;auto status=Normalize(kind,desired,access);if(status)return status;
    char leaf[64]{};bool global=false;
    if(name){if(!Name(p->token,name,global,leaf))return Invalid;auto* old=FindName(p->token,name);if(old){if(old->kind!=kind)return TypeMismatch;if(!Access(p->token,old->security,kind,access))return Denied;return Insert(*p,old->id,access,attrs,handle);}if(global && !(p->token.privileges&CreateGlobal))return Denied;}
    const auto security=sd?*sd:DefaultSecurity(p->token,kind);
    if(!ValidSecurity(security,kind) || security.owner!=p->token.user || !Member(p->token,security.group,false) || security.integrity>p->token.integrity)return Invalid;
    if(!Access(p->token,security,kind,access))return Denied;
    const auto raw=kind==Kind::Device?0:scheduler_->CreateObject(SyncKind(kind),initial,maximum);if(!raw && kind!=Kind::Device)return NoMemory;
    const auto oid=NewObject(kind,security,raw,kind==Kind::Device?initial:0);if(!oid){if(raw)scheduler_->Close(raw);return NoMemory;}
    auto* o=ObjectById(oid);o->global=global;o->session=p->token.session;for(uint32_t i=0;i<64;++i)o->name[i]=leaf[i];
    status=Insert(*p,oid,access,attrs,handle);Dereference(oid);return status;
}
uint32_t Manager::Open(uint32_t pid,Kind kind,const char* name,uint32_t desired,uint64_t& handle) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;char leaf[64]{};bool global=false;if(!Name(p->token,name,global,leaf))return Invalid;
    auto* o=FindName(p->token,name);if(!o)return NotFound;if(o->kind!=kind)return TypeMismatch;
    uint32_t access=0;const auto status=Normalize(kind,desired,access);if(status)return status;if(!Access(p->token,o->security,kind,access))return Denied;return Insert(*p,o->id,access,0,handle);
}
uint32_t Manager::OpenProcess(uint32_t caller,uint32_t target,uint32_t desired,uint64_t& handle) noexcept {
    auto* p=Get(caller);auto* t=Get(target);if(!p || p->state!=State::Live || !t || t->state!=State::Live)return Terminated;
    uint32_t access=0;const auto status=Normalize(Kind::Process,desired,access);if(status)return status;auto* o=ObjectById(t->processObject);
    if(!Access(p->token,o->security,Kind::Process,access))return Denied;return Insert(*p,o->id,access,0,handle);
}
uint32_t Manager::OpenThread(uint32_t caller,uint32_t target,uint32_t desired,uint64_t& handle) noexcept {
    auto* p=Get(caller);auto* owner=Get(Owner(target));if(!p || p->state!=State::Live || !owner)return InvalidHandle;
    Object* object=nullptr;for(uint32_t i=0;i<MaxProcessThreads;++i)if(owner->threads[i]==target)object=ObjectById(owner->threadObjects[i]);
    if(!object)return InvalidHandle;uint32_t access=0;auto status=Normalize(Kind::Thread,desired,access);if(status)return status;
    if(!Access(p->token,object->security,Kind::Thread,access))return Denied;return Insert(*p,object->id,access,0,handle);
}

uint32_t Manager::Close(uint32_t pid,uint64_t value) noexcept {auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;auto* h=Lookup(*p,value);if(!h)return InvalidHandle;if(h->attributes&ProtectClose)return NotClosable;Poll();DropHandle(*h);return Success;}
uint32_t Manager::Flags(uint32_t pid,uint64_t value,uint32_t attrs) noexcept {auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;auto* h=Lookup(*p,value);if(!h)return InvalidHandle;if(attrs&~uint32_t(3))return Invalid;h->attributes=attrs;return Success;}
uint32_t Manager::DuplicateHandle(uint32_t source,uint64_t value,uint64_t targetProcess,uint32_t desired,uint32_t attrs,uint64_t& out) noexcept {
    auto* p=Get(source);if(!p || p->state!=State::Live)return Terminated;auto* h=Lookup(*p,value);if(!h)return InvalidHandle;if(!(h->access&Duplicate))return Denied;
    Object* process=nullptr;auto status=Resolve(source,targetProcess,Kind::Process,Duplicate,process);if(status)return status;
    auto* target=Get(process->target);if(!target || target->state!=State::Live)return Terminated;
    auto* object=ObjectById(h->object);uint32_t access=0;status=Normalize(object->kind,desired,access);if(status)return status;
    if((access&~h->access) || !Access(target->token,object->security,object->kind,access))return Denied;return Insert(*target,object->id,access,attrs,out);
}
uint32_t Manager::QueryHandle(uint32_t pid,uint64_t handle,Information& info) noexcept {
    Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Empty,Query,o);if(status)return status;auto* h=Lookup(*Get(pid),handle);
    info={static_cast<uint32_t>(o->kind),h->access,h->attributes,o->handles,o->refs,o->security.owner,o->security.integrity,0};return Success;
}
uint32_t Manager::SetDacl(uint32_t pid,uint64_t handle,const Security& sd) noexcept {
    Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Empty,WriteDac,o);if(status)return status;
    if(!ValidSecurity(sd,o->kind) || sd.owner!=o->security.owner || sd.group!=o->security.group || sd.integrity!=o->security.integrity || sd.noWriteUp!=o->security.noWriteUp)return Invalid;o->security=sd;return Success;
}
uint32_t Manager::QuerySecurity(uint32_t pid,uint64_t handle,Security& sd) noexcept {Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Empty,ReadControl,o);if(!status)sd=o->security;return status;}
uint32_t Manager::QueryToken(uint32_t pid,uint64_t handle,Token& token) noexcept {Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Token,Query,o);if(!status)token=o->token;return status;}
uint32_t Manager::Reference(uint32_t pid,uint64_t handle,uint32_t rights,uint32_t& oid) noexcept {Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Empty,rights,o);if(status)return status;if(o->refs==UINT32_MAX)return NoMemory;++o->refs;oid=o->id;return Success;}
uint32_t Manager::Attach(uint32_t pid,uint32_t tid,uint64_t& handle) noexcept {
    auto* p=Get(pid);auto* t=scheduler_->Get(tid);if(!p || p->state!=State::Live || !t || Owner(tid))return Invalid;if(p->threadCount==MaxProcessThreads)return Quota;
    const auto oid=NewObject(Kind::Thread,DefaultSecurity(p->token,Kind::Thread),t->completion,tid);if(!oid)return NoMemory;
    auto status=Insert(*p,oid,Allowed(Kind::Thread),0,handle);
    if(status){ObjectById(oid)->raw=0;Dereference(oid);return status;}
    for(uint32_t i=0;i<MaxProcessThreads;++i)if(!p->threads[i]){p->threads[i]=tid;p->threadObjects[i]=oid;++p->threadCount;return Success;}return Invalid;
}
uint32_t Manager::Owner(uint32_t tid) const noexcept {for(const auto& p:processes_)if(p.state!=State::Empty)for(auto t:p.threads)if(t==tid)return p.id;return 0;}
uint32_t Manager::ThreadExitRaw(uint32_t tid,uint32_t code) noexcept {
    auto* p=Get(Owner(tid));if(!p)return Invalid;
    for(uint32_t i=0;i<MaxProcessThreads;++i)if(p->threads[i]==tid){auto* o=ObjectById(p->threadObjects[i]);o->exited=true;o->exitCode=code;scheduler_->Terminate(tid,code);Poll();return Success;}return Invalid;
}
uint32_t Manager::ThreadExit(uint32_t tid,uint32_t code) noexcept {
    const auto pid=Owner(tid);const auto* process=Get(pid);auto e=telemetry::Begin(telemetry::Domain::Thread,telemetry::Operation::Exit,pid,process?process->token.user:0,"thread exit","",tid);
    telemetry::Before(e);return telemetry::End(e,ThreadExitRaw(tid,code));
}
bool Manager::ReapThread(uint32_t tid) noexcept {
    auto* p=Get(Owner(tid));auto* t=scheduler_->Get(tid);
    if(!p || p->state!=State::Live || !t || t->state!=threads::State::Dead)return false;
    Poll();for(uint32_t i=0;i<MaxProcessThreads;++i)if(p->threads[i]==tid){
        // The completion object survives slot reuse while handles or waits reference it.
        if(!scheduler_->Reap(tid,true) || !Dereference(p->threadObjects[i]))return false;
        p->threads[i]=p->threadObjects[i]=0;--p->threadCount;return true;
    }
    return false;
}
void Manager::Poll() noexcept {
    // Ownership and pending waits hold references even after the final handle
    // closes. Capture a handoff before releasing a completed wait's reference.
    for(auto& o:objects_)if(o.kind!=Kind::Empty && o.raw){const auto* raw=scheduler_->GetObject(o.raw);const bool held=raw && (raw->owner || raw->readers);
        if(held && !o.ownership){o.ownership=true;++o.refs;}else if(!held && o.ownership){o.ownership=false;Dereference(o.id);}}
    for(auto& w:waits_)if(w.count){const auto* t=scheduler_->Get(w.tid);if(!t || t->state!=threads::State::Waiting){for(uint32_t i=0;i<w.count;++i)Dereference(w.objects[i]);w={};}}
}
uint32_t Manager::WaitHandles(uint32_t pid,uint32_t tid,const uint64_t* handles,uint32_t count,bool all,uint64_t deadline) noexcept {
    if(Owner(tid)!=pid || !handles || !count || count>threads::MaxWait)return Invalid;
    auto& record=waits_[(tid&255u)-1];if(record.count)return Invalid;uint32_t raw[threads::MaxWait]{};uint32_t objects[threads::MaxWait]{};
    for(uint32_t i=0;i<count;++i){Object* o=nullptr;const auto status=Resolve(pid,handles[i],Kind::Empty,Wait,o);if(status)return status;if(!o->raw)return TypeMismatch;raw[i]=o->raw;objects[i]=o->id;}
    for(uint32_t i=0;i<count;++i){auto* o=ObjectById(objects[i]);if(o->refs==UINT32_MAX){for(uint32_t j=0;j<i;++j)Dereference(objects[j]);return NoMemory;}++o->refs;}
    const auto status=scheduler_->Wait(tid,raw,count,all,deadline);
    if(status==threads::Pending){record.tid=tid;record.pid=pid;record.count=count;for(uint32_t i=0;i<count;++i)record.objects[i]=objects[i];}
    else {Poll();for(uint32_t i=0;i<count;++i)Dereference(objects[i]);}return status;
}
uint32_t Manager::Signal(uint32_t pid,uint64_t handle,uint32_t amount) noexcept {Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Empty,Modify,o);if(status)return status;const auto result=scheduler_->Signal(o->raw,amount);Poll();return result;}
bool Manager::OwnsMutex(uint32_t pid,uint64_t handle,uint32_t tid) noexcept {
    Object* o=nullptr;if(Owner(tid)!=pid || Resolve(pid,handle,Kind::Mutex,Modify,o))return false;
    const auto* raw=scheduler_->GetObject(o->raw);return raw && raw->owner==tid && raw->depth;
}
uint32_t Manager::Unlock(uint32_t pid,uint64_t handle,uint32_t tid,bool shared) noexcept {if(Owner(tid)!=pid)return Denied;Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Empty,Modify,o);if(status)return status;const auto result=scheduler_->Unlock(tid,o->raw,shared);Poll();return result;}
uint32_t Manager::ArmTimer(uint32_t pid,uint64_t handle,uint64_t due,uint64_t period) noexcept {Object* o=nullptr;const auto status=Resolve(pid,handle,Kind::Timer,Modify,o);return status?status:scheduler_->Arm(o->raw,due,period);}
uint32_t Manager::Kill(uint32_t pid,uint32_t code) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;p->state=State::Dying;p->exitCode=code;
    for(auto tid:p->threads)if(tid)ThreadExit(tid,code);Poll();
    if(auto* o=ObjectById(p->processObject)){o->exited=true;o->exitCode=code;scheduler_->Signal(o->raw);Poll();}
    for(auto& h:p->handles)if(h.value)DropHandle(h);return Success;
}
bool Manager::Reap(uint32_t pid) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Dying)return false;Poll();
    for(uint32_t i=0;i<MaxProcessThreads;++i)if(p->threads[i]) {
        if(!scheduler_->Reap(p->threads[i],true))return false;
        if(!Dereference(p->threadObjects[i]))return false;p->threads[i]=p->threadObjects[i]=0;--p->threadCount;
    }
    if(!memory_->Destroy(p->space))return false;
    if(p->tokenObject && !Dereference(p->tokenObject))return false;if(p->processObject && !Dereference(p->processObject))return false;
    p->state=State::Empty;return true;
}
uint32_t Manager::Reserve(uint32_t pid,uint64_t& base,uint64_t bytes,uint32_t protection) noexcept {auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;return static_cast<uint32_t>(memory_->ReserveRange(p->space,base,bytes,protection));}
uint32_t Manager::Commit(uint32_t pid,uint64_t base,uint64_t bytes,uint32_t protection) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;if(!bytes || base<memory::Arena || base>=memory::Arena+memory::ArenaBytes || bytes>memory::Arena+memory::ArenaBytes-base)return Invalid;
    const auto end=(base+bytes+4095)&~4095ull;uint32_t count=0;for(auto at=base&~4095ull;at<end;at+=4096)if(!(memory_->Entry(p->space,at)&0x200))++count;
    if(count>MaxPrivatePages-p->committed)return Quota;const auto status=memory_->CommitRange(p->space,base,bytes,protection);if(status==memory::Status::Success)p->committed+=count;return static_cast<uint32_t>(status);
}
uint32_t Manager::Release(uint32_t pid,uint64_t base) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Live)return Terminated;memory::Information info{};
    if(memory_->Query(p->space,base,info)!=memory::Status::Success || info.allocationBase!=base)return Invalid;
    uint32_t count=0;for(const auto& r:p->space.regions)if(r.used && r.base==base)for(uint64_t at=r.base;at<r.base+r.bytes;at+=4096)if(memory_->Entry(p->space,at)&0x200)++count;
    const auto status=memory_->ReleaseRange(p->space,base);if(status==memory::Status::Success)p->committed-=count;return static_cast<uint32_t>(status);
}
uint64_t Manager::Translate(uint32_t pid,uint64_t at,bool write) noexcept {
    auto* p=Get(pid);if(!p || p->state!=State::Live)return 0;
    if(at>=cpu::UserData && at<cpu::UserData+4096)return p->space.privatePages[15]+at-cpu::UserData;
    const auto e=memory_->Entry(p->space,at);if((e&5)!=5 || (write && !(e&2)))return 0;return memory_->Translate(p->space,at);
}
uint32_t Manager::CopyProcess(uint32_t caller,uint64_t process,uint64_t remote,uint64_t local,uint32_t bytes,bool write) noexcept {
    Object* o=nullptr;auto status=Resolve(caller,process,Kind::Process,write?WriteMemory:ReadMemory,o);if(status)return status;
    if(!Get(o->target) || Get(o->target)->state!=State::Live)return Terminated;
    if(!bytes || bytes>sizeof(scratch_) || remote>UINT64_MAX-bytes || local>UINT64_MAX-bytes)return Invalid;
    for(uint32_t i=0;i<bytes;++i)if(!Translate(o->target,remote+i,write) || !Translate(caller,local+i,!write))return Denied;
    for(uint32_t i=0;i<bytes;++i)scratch_[i]=*static_cast<const uint8_t*>(memory_->physical.At(Translate(write?caller:o->target,(write?local:remote)+i,false)));
    for(uint32_t i=0;i<bytes;++i)*static_cast<uint8_t*>(memory_->physical.At(Translate(write?o->target:caller,(write?remote:local)+i,true)))=scratch_[i];return Success;
}
uint32_t Manager::TerminateProcess(uint32_t caller,uint64_t handle,uint32_t code) noexcept {Object* o=nullptr;const auto status=Resolve(caller,handle,Kind::Process,Terminate,o);return status?status:Kill(o->target,code);}
Statistics Manager::Counts() const noexcept {
    Statistics s{};s.consistent=consistent_;for(const auto& p:processes_)if(p.state!=State::Empty){++s.processes;s.committed+=p.committed;s.threads+=p.threadCount;for(const auto& h:p.handles)if(h.value)++s.handles;}
    for(const auto& o:objects_)if(o.kind!=Kind::Empty){++s.objects;s.references+=o.refs;}return s;
}
}
