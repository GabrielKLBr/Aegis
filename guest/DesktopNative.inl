uint32_t Services::DesktopThread(uint32_t current,uint64_t* a) noexcept {
    const auto op=a[0];if(op==8||op==9){uint32_t process=0;const auto status=Process(a[1],process,op==8?processes::Query:processes::WriteDac);if(status)return status;if(process!=pid_)return Denied;if(op==9&&a[2]!=1)return Invalid;a[2]=a[3]=1;return 0;}
    uint32_t tid=current;if(a[1]!=CurrentThread){auto* h=Find(a[1]);processes::Object* object=nullptr;if(h&&h->kind!=4)return InvalidHandle;const auto raw=h?h->raw:a[1];const auto required=op==3||op==4?processes::Query:processes::Modify;const auto status=processes_->Resolve(pid_,raw,processes::Kind::Thread,required,object);if(status)return status;tid=object->target;}
    if(processes_->Owner(tid)!=pid_)return Denied;auto* thread=scheduler_->Get(tid);if(!thread||thread->state==threads::State::Dead)return InvalidHandle;
    if(op==1){if(tid==current)return Unsupported;uint32_t previous=0;const auto status=scheduler_->Suspend(tid,previous);a[2]=previous;return status;}
    if(op==2){const auto value=int32_t(a[2]);if(value< -2||value>2)return Unsupported;return scheduler_->Priority(tid,uint32_t(value+3));}
    if(op==3){a[2]=uint64_t(int64_t(int32_t(thread->priority)-3));return 0;}
    if(op==4||op==5){if(tid==current||!thread->suspended||!bridge_.threadContext)return Unsupported;exceptions::Context context;if(!Read(a[2],&context,sizeof(context)))return AccessViolation;if((context.flags&0xFFFF0000)!=0x100000||(context.flags&~0x10000Fu))return Unsupported;
        const auto flags=context.flags;const auto status=bridge_.threadContext(bridge_.context,tid,context,op==5);if(status)return status;context.flags=flags;return op==5||Write(a[2],&context,sizeof(context))?0:AccessViolation;
    }return NotImplemented;
}
