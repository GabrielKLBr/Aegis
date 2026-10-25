uint32_t Services::FlushSection(Section& s) noexcept {
    if(!s.file||s.protection!=memory::ReadWrite)return 0;
    storage::Command c;c.op=storage::Op::Write;c.handle=s.file;c.length=s.length;Copy(c.data,memory_->physical.At(s.data[0]),s.length);
    return Files().Execute(pid_,processes_->Get(pid_)->token,c);
}
uint32_t Services::Win32(uint32_t op,uint64_t* a) noexcept {
    if(!memory_||!a)return Denied;const auto& token=processes_->Get(pid_)->token;
    if(op==4){for(auto& r:ranges_)if(r.base&&r.section>=0&&a[0]>=r.base&&a[0]-r.base<r.bytes){if(a[1]>r.bytes-(a[0]-r.base))return Invalid;return FlushSection(sections_[r.section]);}return Invalid;}
    if((op>=5&&op<=7)||op==9){storage::Command c;auto status=ObjectName(a[0],c.path,2);if(status)return status;
        if(op!=5){storage::Kind kind{};status=Files().QueryKind(token,c.path,kind);if(status)return status;
            if((op==6||op==9)?kind!=storage::Kind::Directory:kind!=storage::Kind::File)return storage::TypeMismatch;if(op==9)return 0;}
        c.op=op==5?storage::Op::Mkdir:storage::Op::Unlink;return Files().Execute(pid_,token,c);}
    if(op==8){const auto counts=memory_->physical.Statistics();a[0]=memory_->physical.Bytes();a[1]=counts.free*4096;a[2]=memory::ArenaBytes;a[3]=memory::ArenaBytes;
        for(const auto& r:processes_->Get(pid_)->space.regions)if(r.used)a[3]-=r.bytes;return 0;}
    if(op!=1&&op!=2)return NotImplemented;
    auto* h=Find(a[0]);if(!h)return InvalidHandle;
#ifdef GUARDIAN_DESKTOP_NATIVE
    if(h->kind==5){if(op==2)return Denied;if(a[2]>2)return Invalid;const uint64_t start=a[2]==0?0:a[2]==1?h->position:h->index;const auto distance=int64_t(a[1]);if((distance<0&&(distance==INT64_MIN||uint64_t(-distance)>start))||(distance>=0&&uint64_t(distance)>INT64_MAX-start))return Invalid;h->position=distance<0?start-uint64_t(-distance):start+uint64_t(distance);a[3]=h->position;return 0;}
#endif
    if(h->kind!=2)return InvalidHandle;
    if(op==1){storage::Command size;size.op=storage::Op::FileSize;size.handle=h->raw;const auto status=Files().Execute(pid_,token,size);if(status)return status;
        if(a[2]>2)return Invalid;const uint64_t start=a[2]==0?0:(a[2]==1?h->position:size.transferred);const auto distance=int64_t(a[1]);
        if((distance<0&&(distance==INT64_MIN||uint64_t(-distance)>start))||(distance>=0&&uint64_t(distance)>INT64_MAX-start))return 0xE1000001;
        h->position=distance<0?start-uint64_t(-distance):start+uint64_t(distance);a[3]=h->position;return 0;}
    if(op==2){storage::Command c;c.op=storage::Op::Flush;return Files().Execute(pid_,token,c);}
    return NotImplemented;
}
