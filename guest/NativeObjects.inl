namespace {
uint32_t SyncAccess(uint64_t access,bool mutant=false) noexcept {
    if(access&0x10000000)return processes::GenericAll;uint32_t out=0;
    if(access&0x100000)out|=processes::Wait;if(access&1)out|=processes::Query;if(access&2)out|=processes::Modify;
    if(mutant&&(access&1))out|=processes::Modify;if(access&0x20000)out|=processes::ReadControl;if(access&0x40000)out|=processes::WriteDac;return out;
}
}
uint32_t Services::Information(Op op,const uint64_t* a,uint32_t tid,uint64_t teb) noexcept {
    if(op==Op::Duplicate){uint32_t source=0,target=0;auto status=Process(a[0],source,processes::Duplicate);if(!status)status=Process(a[2],target,processes::Duplicate);if(status)return status;
        if(source!=pid_||target!=pid_||a[5]||a[6]!=2)return Unsupported;if(!Probe(a[3],8,true))return AccessViolation;
#ifdef GUARDIAN_DESKTOP_NATIVE
        if(a[1]==CurrentThread||a[1]==CurrentProcess){uint64_t raw=0;status=a[1]==CurrentThread?processes_->OpenThread(pid_,tid,processes::GenericAll,raw):processes_->CloneHandle(pid_,processes_->Get(pid_)->self,raw);if(status)return status;auto* next=New(4,0x1FFFFFu,raw);if(!next){processes_->Close(pid_,raw);return NoMemory;}Output(a[3],next->id);return 0;}
#endif
        auto* h=Find(a[1]);if(!h)return InvalidHandle;
        if(h->kind!=1&&h->kind!=4)return Unsupported;uint64_t raw=0;
        if(h->kind==4){status=processes_->CloneHandle(pid_,h->raw,raw);if(status)return status;}
        auto* next=New(h->kind,h->access,raw,h->index);if(!next){if(raw)processes_->Close(pid_,raw);return NoMemory;}if(h->kind==1)++sections_[h->index].references;Output(a[3],next->id);return 0;
    }
    if(op==Op::OpenProcess||op==Op::OpenThread){if(!Probe(a[0],8,true))return AccessViolation;Attributes attrs{};ClientId id{};
        if(!Read(a[2],&attrs,sizeof(attrs))||!Read(a[3],&id,sizeof(id)))return AccessViolation;
        if(attrs.length!=sizeof(attrs)||attrs.name||attrs.root||attrs.attributes||attrs.security||attrs.qos||id.process>UINT32_MAX||id.thread>UINT32_MAX)return Invalid;
        if(op==Op::OpenProcess&&id.thread)return Invalid;
        if(op==Op::OpenThread&&(!id.thread||(id.process&&processes_->Owner(uint32_t(id.thread))!=id.process)))return Invalid;
        uint32_t access=0;if(a[1]&0x10000000)access=processes::GenericAll;else {if(a[1]&0x100000)access|=processes::Wait;if(a[1]&(0x400|0x1000))access|=processes::Query;
            if(a[1]&0x10)access|=processes::ReadMemory;if(a[1]&0x28)access|=processes::WriteMemory;if(a[1]&2)access|=processes::CreateChild;if(a[1]&1)access|=processes::Terminate;}
        #ifdef GUARDIAN_DESKTOP_NATIVE
        if(a[1]&0x40)access|=processes::Duplicate;if(a[1]&0x200)access|=processes::WriteDac;
#endif
        uint32_t threadAccess=0;if(a[1]&0x10000000)threadAccess=processes::GenericAll;else {if(a[1]&0x100000)threadAccess|=processes::Wait;if(a[1]&(0x40|0x800))threadAccess|=processes::Query;if(a[1]&(2|0x10|0x20))threadAccess|=processes::Modify;if(a[1]&1)threadAccess|=processes::Terminate;}
        #ifdef GUARDIAN_DESKTOP_NATIVE
        if(a[1]&8)threadAccess|=processes::Query;
#endif
        uint64_t raw=0;const auto status=op==Op::OpenProcess?processes_->OpenProcess(pid_,uint32_t(id.process),access,raw):processes_->OpenThread(pid_,uint32_t(id.thread),threadAccess,raw);
        if(status)return status;auto* h=New(4,uint32_t(a[1]),raw);if(!h){processes_->Close(pid_,raw);return NoMemory;}Output(a[0],h->id);return 0;
    }
    if(op==Op::QueryObject){auto* h=Find(a[0]);if(!h)return InvalidHandle;if(a[1])return InvalidClass;constexpr uint32_t size=56;
        if(a[4]&&!Write(a[4],&size,4))return AccessViolation;if(a[3]<size)return LengthMismatch;if(!Probe(a[2],size,true))return AccessViolation;
        uint32_t fields[14]{};fields[1]=h->access;fields[2]=1;fields[3]=1;
        if(h->kind==1){fields[2]=0;for(const auto& other:handles_)if(other.id&&other.kind==1&&other.index==h->index)++fields[2];fields[3]=sections_[h->index].references;}
        else if(h->kind==4){processes::Information info{};const auto status=processes_->QueryHandle(pid_,h->raw,info);if(status)return status;fields[2]=info.handleReferences;fields[3]=info.pointerReferences;}
        Write(a[2],fields,size);return 0;}
    if(op==Op::QueryProcess){uint32_t target=0;auto* h=Find(a[0]);const auto raw=h&&h->kind==4?h->raw:a[0];auto status=Process(raw,target,processes::Query);if(status)return status;
        if(a[1])return InvalidClass;constexpr uint32_t size=sizeof(ProcessBasic);if(a[4]&&!Write(a[4],&size,4))return AccessViolation;if(a[3]!=size)return LengthMismatch;
        if(!Probe(a[2],size,true))return AccessViolation;if(target!=pid_)return Denied;ProcessBasic info;info.peb=peb_;info.pid=pid_;info.parent=processes_->Get(pid_)->parent;Write(a[2],&info,size);return 0;}
    if(op==Op::QueryThread){if(a[0]!=CurrentThread)return Unsupported;if(a[1])return InvalidClass;constexpr uint32_t size=sizeof(ThreadBasic);
        if(a[4]&&!Write(a[4],&size,4))return AccessViolation;if(a[3]!=size)return LengthMismatch;ThreadBasic info;info.teb=teb;info.client={pid_,tid};return Write(a[2],&info,size)?0:AccessViolation;}
    if(op==Op::QuerySystem){if(a[0])return InvalidClass;constexpr uint32_t size=64;if(a[3]&&!Write(a[3],&size,4))return AccessViolation;if(a[2]!=size)return LengthMismatch;
        struct Basic {uint32_t reserved=0,resolution=10000,page=4096,pages=0,lowest=0,highest=0,granularity=65536,padding=0;uint64_t minimum=memory::Arena,maximum=memory::Arena+memory::ArenaBytes-1,affinity=1;uint8_t processors=1,pad[7]{};} info;
        const auto counts=memory_->physical.Statistics();info.pages=uint32_t(counts.free+counts.data+counts.tables);info.highest=info.pages?info.pages-1:0;
        return Write(a[1],&info,sizeof(info))?0:AccessViolation;}
    return NotImplemented;
}
uint32_t Services::Sync(Op op,const uint64_t* a,uint32_t tid,uint64_t now,uint64_t utc,bool inApc) noexcept {
    if(op==Op::Delay||op==Op::TestAlert){if(op==Op::Delay&&(a[0]>1||!a[1]))return Invalid;const bool alertable=op==Op::TestAlert||a[0]!=0;if(alertable&&inApc)return Unsupported;
        uint64_t deadline=now;if(op==Op::Delay&&!Deadline(a[1],now,utc,deadline))return AccessViolation;
        if(op==Op::Delay&&deadline==now&&!alertable){scheduler_->Yield();return 0;}const auto status=scheduler_->Delay(tid,deadline,alertable);
        return op==Op::TestAlert&&(status==Timeout||status==UserApc)?0:status;}
    if(op==Op::CreateEvent||op==Op::CreateMutant||op==Op::CreateSemaphore||op==Op::CreateTimer){
        if(!Probe(a[0],8,true))return AccessViolation;char name[128]{};const auto named=ObjectName(a[2],name,1,true);if(named)return named;
        processes::Kind kind=processes::Kind::Event;if(op==Op::CreateMutant)kind=processes::Kind::Mutex;if(op==Op::CreateSemaphore)kind=processes::Kind::Semaphore;if(op==Op::CreateTimer)kind=processes::Kind::Timer;
        const uint32_t specific=op==Op::CreateMutant?1u:3u;if(a[1]&~(uint64_t(0x1F0000|specific)|0x10000000ull))return Denied;
        const uint32_t nativeAccess=(a[1]&0x10000000)?(0x1F0000|specific):uint32_t(a[1]);
        if((op==Op::CreateEvent&&(a[3]>1||a[4]>1))||(op==Op::CreateTimer&&a[3]>1)||(op==Op::CreateMutant&&a[3]>1)||
            (op==Op::CreateSemaphore&&(a[3]>INT32_MAX||!a[4]||a[4]>INT32_MAX||a[3]>a[4])))return Invalid;
        uint64_t raw=0;uint32_t status=processes_->CreateObject(pid_,kind,nullptr,nullptr,SyncAccess(nativeAccess,kind==processes::Kind::Mutex),0,raw,
            op==Op::CreateSemaphore?uint32_t(a[3]):(op==Op::CreateEvent?uint32_t(a[4]):0),op==Op::CreateSemaphore?uint32_t(a[4]):1);
        if(status)return status;processes::Object* object=nullptr;status=processes_->Resolve(pid_,raw,kind,0,object);
        if(!status){auto* sync=scheduler_->GetObject(object->raw);if(op==Op::CreateEvent&&!a[3])sync->kind=threads::Kind::ManualEvent;if(op==Op::CreateTimer&&!a[3])sync->kind=threads::Kind::ManualTimer;
            if(op==Op::CreateMutant&&a[3])status=processes_->WaitHandles(pid_,tid,&raw,1,false,now);}
        if(status){processes_->Close(pid_,raw);return status;}auto* h=New(4,nativeAccess,raw);if(!h){processes_->Close(pid_,raw);return NoMemory;}Output(a[0],h->id);return 0;
    }
    if(op==Op::WaitSingle||op==Op::WaitMultiple){const uint32_t count=op==Op::WaitSingle?1:uint32_t(a[0]);const auto alertable=op==Op::WaitSingle?a[1]:a[3];
        if(!count||count>threads::MaxWait||alertable>1||(alertable&&inApc)||(op==Op::WaitMultiple&&a[2]>1))return Invalid;
        uint64_t handles[threads::MaxWait]{};if(op==Op::WaitSingle)handles[0]=a[0];else if(!Read(a[1],handles,count*8))return AccessViolation;
        for(uint32_t i=0;i<count;++i){auto* h=Find(handles[i]);if(h){if(h->kind!=4)return 0xC0000024;handles[i]=h->raw;}}
        uint64_t deadline=0;if(!Deadline(op==Op::WaitSingle?a[2]:a[4],now,utc,deadline))return AccessViolation;
        return processes_->WaitHandles(pid_,tid,handles,count,op==Op::WaitMultiple&&!a[2],deadline,alertable!=0);
    }
    if(op==Op::QueueApc){uint32_t target=tid;auto* h=Find(a[0]);if(a[0]!=CurrentThread){processes::Object* t=nullptr;const auto status=processes_->Resolve(pid_,h&&h->kind==4?h->raw:a[0],processes::Kind::Thread,processes::Modify,t);if(status)return status;target=t->target;}
        if(h&&h->kind==4&&!(h->access&(0x10|0x10000000)))return Denied;
        if(!bridge_.executable||!bridge_.executable(bridge_.context,a[1]))return AccessViolation;return scheduler_->QueueNativeApc(target,a[1],a[2],a[3],a[4]);}
    auto* h=Find(a[0]);const auto raw=h&&h->kind==4?h->raw:a[0];processes::Object* object=nullptr;auto status=processes_->Resolve(pid_,raw,processes::Kind::Empty,processes::Modify,object);if(status)return status;
    auto* sync=scheduler_->GetObject(object->raw);if(!sync)return InvalidHandle;
    if(op==Op::SetEvent||op==Op::ResetEvent){if(object->kind!=processes::Kind::Event)return 0xC0000024;if(a[1]&&!Probe(a[1],4,true))return AccessViolation;const uint32_t previous=sync->signaled?1:0;
        status=op==Op::SetEvent?processes_->Signal(pid_,raw):scheduler_->Reset(object->raw);if(!status&&a[1])Write(a[1],&previous,4);return status;}
    if(op==Op::ReleaseMutant){if(object->kind!=processes::Kind::Mutex)return 0xC0000024;if(a[1]&&!Probe(a[1],4,true))return AccessViolation;const int32_t previous=1-int32_t(sync->depth);
        status=processes_->Unlock(pid_,raw,tid);if(!status&&a[1])Write(a[1],&previous,4);return status;}
    if(op==Op::ReleaseSemaphore){if(object->kind!=processes::Kind::Semaphore)return 0xC0000024;if(!a[1]||a[1]>INT32_MAX)return Invalid;if(a[2]&&!Probe(a[2],4,true))return AccessViolation;
        const uint32_t previous=sync->count;status=processes_->Signal(pid_,raw,uint32_t(a[1]));if(!status&&a[2])Write(a[2],&previous,4);return status;}
    if(op==Op::SetTimer||op==Op::CancelTimer){if(object->kind!=processes::Kind::Timer)return 0xC0000024;const auto output=op==Op::SetTimer?a[6]:a[1];if(output&&!Probe(output,1,true))return AccessViolation;
        const uint8_t previous=sync->signaled?1:0;if(op==Op::SetTimer){if(a[2]||a[3]||a[4]||a[5]>INT32_MAX)return Unsupported;uint64_t deadline=0;
            if(!a[1]||!Deadline(a[1],now,utc,deadline))return AccessViolation;status=processes_->ArmTimer(pid_,raw,deadline,a[5]*1000000);}
        else status=scheduler_->CancelTimer(object->raw);if(!status&&output)Write(output,&previous,1);return status;}
    return NotImplemented;
}
