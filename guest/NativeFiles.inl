uint32_t Services::Files(Op op,const uint64_t* a) noexcept {
    const auto& token=processes_->Get(pid_)->token;storage::Command c;
    if(op==Op::CreateFile||op==Op::OpenFile){if(!Probe(a[0],8,true)||!Probe(a[3],sizeof(IoStatus),true))return AccessViolation;
        if((op==Op::CreateFile&&(a[4]||a[9]||a[10]))||(op==Op::CreateFile&&a[7]>5))return Unsupported;
        const auto options=op==Op::CreateFile?a[8]:a[5];if(options&~0x60u)return Unsupported;
        auto status=ObjectName(a[2],c.path,2);if(status)return status;
        uint32_t access=0;if(a[1]&(0x80000000ull|1))access|=storage::Read;if(a[1]&(0x40000000ull|2|4))access|=storage::Write;
        if(a[1]&0x10000)access|=storage::Delete;if(!access)return Denied;
        c.access=access;c.share=uint32_t(op==Op::CreateFile?a[6]:a[4]);if(c.share>7)return Invalid;
        const auto disposition=op==Op::CreateFile?a[7]:1;if(!disposition)return Unsupported;c.disposition=disposition==2?1u:((disposition==3||disposition==5)?2u:0u);
        if(disposition>=4&&!(access&storage::Write))return Denied;
#ifdef GUARDIAN_DESKTOP_NATIVE
        if(!(access&(storage::Write|storage::Delete))&&(disposition==1||disposition==3)&&bridge_.catalog){storage::Kind kind{};const auto exists=Files().QueryKind(token,c.path,kind);if(exists==storage::NotFound){uint32_t read=0,total=0;if(bridge_.catalog(bridge_.context,c.path,0,nullptr,0,read,total)){auto audit=telemetry::Begin(telemetry::Domain::File,telemetry::Operation::Open,pid_,token.user,c.path);if(!telemetry::Before(audit))return telemetry::End(audit,Denied);auto* immutable=New(5,uint32_t(a[1]),0,total);if(!immutable)return telemetry::End(audit,NoMemory);Copy(immutable->path,c.path,128);Output(a[0],immutable->id);IoStatus io{0,1};Write(a[3],&io,sizeof(io));audit.related=immutable->id;return telemetry::End(audit,0);}}}
#endif
        auto* h=New(2,uint32_t(a[1]));if(!h)return NoMemory;
        storage::Command exists;exists.op=storage::Op::QueryAcl;exists.length=sizeof(processes::Security);Copy(exists.path,c.path,128);const auto existence=Files().Execute(pid_,token,exists);const bool existed=existence==0;
        if(existence&&existence!=storage::NotFound){*h={};return existence;}
        c.op=storage::Op::Open;status=Files().Execute(pid_,token,c);if(status){*h={};IoStatus io{status,0};Write(a[3],&io,sizeof(io));return status;}h->raw=c.handle;
        Copy(h->path,c.path,128);if(disposition>=4){storage::Command truncate;truncate.op=storage::Op::Truncate;truncate.handle=h->raw;status=Files().Execute(pid_,token,truncate);if(status){Close(h->id);return status;}}
        Output(a[0],h->id);IoStatus io{0,existed?(disposition>=4?3ull:1ull):2ull};Write(a[3],&io,sizeof(io));return 0;
    }
    if(op==Op::CreateKey||op==Op::OpenKey){if(!Probe(a[0],8,true)||(op==Op::CreateKey&&a[6]&&!Probe(a[6],4,true)))return AccessViolation;
#ifdef GUARDIAN_SECURITY_NATIVE
        if(op==Op::CreateKey){Attributes attributes{};if(!Read(a[2],&attributes,sizeof(attributes)))return AccessViolation;if(attributes.root){const auto* parent=Find(attributes.root);if(!parent||parent->kind!=3)return InvalidHandle;if(!(parent->access&4))return Denied;}}
#endif
        if(op==Op::CreateKey&&(a[3]||a[4]||a[5]))return Unsupported;auto status=ObjectName(a[2],c.path,3);if(status)return status;
        // QueryAcl checks existence without creating a key in the open path.
#ifdef GUARDIAN_SECURITY_NATIVE
        const auto desired=uint32_t(a[1]);if(a[1]!=desired||desired&~0xF003Fu)return Invalid;
        uint32_t rights=0;if(desired&1)rights|=storage::Read;if(desired&2)rights|=storage::Write;if(desired&4)rights|=storage::CreateChild;if(desired&8)rights|=storage::List;if(desired&16)rights|=storage::Read;
        if(desired&0x10000)rights|=storage::Delete;if(desired&0x20000)rights|=storage::ReadAcl;if(desired&0xC0000)rights|=storage::WriteAcl;
        uint32_t identity=0;status=Files().CheckAccess(token,c.path,rights,&identity);const bool existed=!status;
        if(status&&(op==Op::OpenKey||status!=storage::NotFound))return status;
#else
        c.op=storage::Op::QueryAcl;c.length=sizeof(processes::Security);status=Files().Execute(pid_,token,c);const bool existed=!status;
        if(status&&op==Op::OpenKey)return status;
#endif
        auto* h=New(3,uint32_t(a[1]));if(!h)return NoMemory;
        if(!existed){c.op=storage::Op::RegCreate;c.status=0;c.transferred=0;c.length=0;status=Files().Execute(pid_,token,c);if(status){*h={};return status;}}
#ifdef GUARDIAN_SECURITY_NATIVE
        status=Files().CheckAccess(token,c.path,rights,&identity);if(status){*h={};return status;}h->index=identity;
#endif
        Copy(h->path,c.path,128);Output(a[0],h->id);
        if(op==Op::CreateKey&&a[6]){const uint32_t disposition=existed?2u:1u;Write(a[6],&disposition,4);}return 0;
    }
    auto* h=Find(a[0]);if(!h)return InvalidHandle;
#ifdef GUARDIAN_DESKTOP_NATIVE
    if(h->kind==5){auto audit=telemetry::Begin(telemetry::Domain::File,op==Op::ReadFile?telemetry::Operation::Read:op==Op::WriteFile?telemetry::Operation::Write:telemetry::Operation::Query,pid_,token.user,h->path,"",h->id);if(!telemetry::Before(audit))return telemetry::End(audit,Denied);const auto operation=[&]() noexcept ->uint32_t {if(op==Op::WriteFile)return Denied;if(op==Op::ReadFile){if(a[1]||a[2]||a[3]||a[8]||a[6]>65536)return Unsupported;if(!Probe(a[4],sizeof(IoStatus),true)||(a[6]&&!Probe(a[5],uint32_t(a[6]),true)))return AccessViolation;int64_t offset=int64_t(h->position);if(a[7]&&!Read(a[7],&offset,8))return AccessViolation;if(offset<0)return Invalid;uint32_t total=0,done=0;const auto available=uint64_t(offset)>=h->index?0ull:uint64_t(h->index)-uint64_t(offset);const auto count=uint32_t(a[6]<available?a[6]:available);
            while(done<count){const auto chunk=count-done>512?512u:count-done;uint32_t read=0;if(!bridge_.catalog(bridge_.context,h->path,uint64_t(offset)+done,c.data,chunk,read,total)||total!=h->index||read!=chunk)return Invalid;if(!Write(a[5]+done,c.data,read))return AccessViolation;done+=read;}h->position=uint64_t(offset)+done;IoStatus io{0,done};Write(a[4],&io,sizeof(io));return 0;}
        if(op==Op::QueryFile){if(a[4]!=5)return InvalidClass;if(a[3]<24||!Probe(a[1],sizeof(IoStatus),true)||!Probe(a[2],24,true))return AccessViolation;struct Standard {uint64_t allocated,end;uint32_t links;uint8_t deleted,directory,pad[2];} info{(uint64_t(h->index)+511)&~511ull,h->index,1,0,0,{0,0}};Write(a[2],&info,sizeof(info));IoStatus io{0,sizeof(info)};Write(a[1],&io,sizeof(io));return 0;}return Unsupported;};return telemetry::End(audit,operation());
    }
#endif
    if(op==Op::ReadFile||op==Op::WriteFile){if(h->kind!=2)return 0xC0000024;if(a[1]||a[2]||a[3]||a[8])return Unsupported;

#ifdef GUARDIAN_DESKTOP_NATIVE
        if(a[6]>(op==Op::ReadFile?65536u:512u))return Unsupported;
#else
        if(a[6]>512)return Unsupported;
#endif
        if(!Probe(a[4],sizeof(IoStatus),true)||(a[6]&&!Probe(a[5],uint32_t(a[6]),op==Op::ReadFile)))return AccessViolation;
        int64_t offset=int64_t(h->position);if(a[7]&&!Read(a[7],&offset,8))return AccessViolation;if(offset<0)return Unsupported;
        if(op==Op::WriteFile&&!Read(a[5],c.data,uint32_t(a[6])))return AccessViolation;
        c.op=op==Op::ReadFile?storage::Op::Read:storage::Op::Write;c.handle=h->raw;c.offset=uint64_t(offset);c.length=uint32_t(a[6]);
#ifdef GUARDIAN_DESKTOP_NATIVE
        if(c.length>512)c.length=512;
        if(op==Op::ReadFile){storage::Command size;size.op=storage::Op::FileSize;size.handle=h->raw;const auto status=Files().Execute(pid_,token,size);if(status)return status;if(uint64_t(offset)>=size.transferred){IoStatus io{0,0};Write(a[4],&io,sizeof(io));return 0;}if(c.length>size.transferred-uint64_t(offset))c.length=uint32_t(size.transferred-uint64_t(offset));}
#endif
        const auto status=Files().Execute(pid_,token,c);
        if(!status){if(op==Op::ReadFile)Write(a[5],c.data,c.transferred);h->position=c.offset+c.transferred;}
        IoStatus io{status,status?0ull:uint64_t(c.transferred)};Write(a[4],&io,sizeof(io));return status;
    }
    if(op==Op::QueryFile){if(h->kind!=2)return 0xC0000024;if(a[4]!=5)return InvalidClass;if(a[3]<24)return LengthMismatch;
        if(!Probe(a[1],sizeof(IoStatus),true)||!Probe(a[2],24,true))return AccessViolation;c.op=storage::Op::FileSize;c.handle=h->raw;const auto status=Files().Execute(pid_,token,c);if(status)return status;
        struct Standard {uint64_t allocated=0,end=0;uint32_t links=1;uint8_t deleted=0,directory=0,pad[2]{};} info;
        info.end=c.transferred;info.allocated=c.transferred?512:0;Write(a[2],&info,sizeof(info));IoStatus io{0,sizeof(info)};Write(a[1],&io,sizeof(io));return 0;
    }
    if(h->kind!=3)return 0xC0000024;Copy(c.path,h->path,128);
#ifdef GUARDIAN_SECURITY_NATIVE
    uint32_t identity=0;const auto keyStatus=Files().CheckAccess(token,h->path,0,&identity);if(keyStatus==storage::NotFound||(!keyStatus&&identity!=h->index))return 0xC000017C;if(keyStatus)return keyStatus;
#endif
    if(op==Op::DeleteKey){if(!(h->access&0x10000))return Denied;c.op=storage::Op::RegDeleteKey;return Files().Execute(pid_,token,c);}
    if(!Text(a[1],c.name,sizeof(c.name)))return AccessViolation;
    if(op==Op::SetValue){if(!(h->access&2))return Denied;if(a[2]||a[5]>512)return Invalid;if(!Read(a[4],c.data,uint32_t(a[5])))return AccessViolation;
        c.op=storage::Op::RegSet;c.type=uint32_t(a[3]);c.length=uint32_t(a[5]);return Files().Execute(pid_,token,c);}
    if(op==Op::DeleteValue){if(!(h->access&2))return Denied;c.op=storage::Op::RegDeleteValue;return Files().Execute(pid_,token,c);}
    if(op==Op::QueryValue){if(!(h->access&1))return Denied;if(a[2]!=2)return InvalidClass;if(!Probe(a[5],4,true))return AccessViolation;
        c.op=storage::Op::RegGet;c.length=512;const auto status=Files().Execute(pid_,token,c);if(status)return status;const uint32_t size=12+c.transferred;Write(a[5],&size,4);
        if(a[4]<size)return a[4]>=12?0x80000005u:Small;if(!Probe(a[3],size,true))return AccessViolation;uint32_t header[3]{0,c.type,c.transferred};Write(a[3],header,12);Write(a[3]+12,c.data,c.transferred);return 0;}
    return NotImplemented;
}
