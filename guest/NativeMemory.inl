uint32_t Services::Vm(Op op,const uint64_t* a) noexcept {
    if(auto* h=Find(a[0]);h&&h->kind==4){const auto required=op==Op::QueryMemory?(0x400u|0x1000u):8u;if(!(h->access&(required|0x10000000u)))return Denied;}
    uint32_t target=0;auto status=Process(a[0],target,op==Op::QueryMemory?processes::Query:processes::WriteMemory);if(status)return status;if(target!=pid_)return Denied;
    auto& p=*processes_->Get(pid_);
    if(op==Op::QueryMemory){if(a[2])return InvalidClass;if(a[4]<sizeof(memory::Information))return LengthMismatch;
        if(!Probe(a[3],sizeof(memory::Information),true)||(a[5]&&!Probe(a[5],8,true)))return AccessViolation;memory::Information info{};
        status=uint32_t(memory_->Query(p.space,a[1],info));if(!status){Write(a[3],&info,sizeof(info));if(a[5])Output(a[5],sizeof(info));}return status;}
    uint64_t base=0,bytes=0;const auto sizePointer=op==Op::Allocate?a[3]:a[2];
    if(!Read(a[1],&base,8)||!Read(sizePointer,&bytes,8)||!Probe(a[1],8,true)||!Probe(sizePointer,8,true))return AccessViolation;
    Range* range=nullptr;for(auto& r:ranges_)if(r.base&&base>=r.base&&base-r.base<r.bytes)range=&r;
    if(op==Op::Allocate){if(a[2]||!bytes||bytes>(8u<<20)||!Protection(a[5]))return Invalid;
        if(a[4]!=0x1000&&a[4]!=0x2000&&a[4]!=0x3000)return Unsupported;
        if(a[4]&0x2000){Range* slot=nullptr;for(auto& r:ranges_)if(!r.base){slot=&r;break;}if(!slot)return NoMemory;
            const auto requested=base;status=processes_->Reserve(pid_,base,bytes,uint32_t(a[5]));if(status)return status;
            const auto rounded=((requested?requested:base)+bytes+4095)&~4095ull;bytes=rounded-base;
            if(a[4]&0x1000){status=processes_->Commit(pid_,base,bytes,uint32_t(a[5]));if(status){processes_->Release(pid_,base);return status;}}
            *slot={base,bytes,-1};
        }else {if(!range||range->section>=0||bytes>range->bytes-(base-range->base))return Invalid;
            const auto end=(base+bytes+4095)&~4095ull;base&=~4095ull;bytes=end-base;
            status=processes_->Commit(pid_,base,bytes,uint32_t(a[5]));if(status)return status;}
    }else if(op==Op::Free){if(!range||range->section>=0)return Invalid;
        if(a[3]==0x8000){if(bytes||base!=range->base)return Invalid;status=processes_->Release(pid_,base);if(status)return status;bytes=range->bytes;*range={};}
        else if(a[3]==0x4000){if(!bytes){if(base!=range->base)return Invalid;bytes=range->bytes;}
            if(bytes>range->bytes-(base-range->base))return Invalid;const auto end=(base+bytes+4095)&~4095ull;base&=~4095ull;bytes=end-base;
            if(base<range->base||bytes>range->bytes-(base-range->base))return Invalid;uint32_t committed=0;
            for(auto at=base;at<end;at+=4096)if(memory_->Entry(p.space,at)&0x200)++committed;
            status=uint32_t(memory_->DecommitRange(p.space,base,bytes));if(status)return status;p.committed-=committed;
        }else return Invalid;
    }else {if(!range||range->section>=0||!bytes||bytes>range->bytes-(base-range->base)||!Protection(a[3]))return Invalid;
        if(!Probe(a[4],4,true))return AccessViolation;const auto end=(base+bytes+4095)&~4095ull;base&=~4095ull;bytes=end-base;uint32_t old=0;
        status=uint32_t(memory_->ProtectRange(p.space,base,bytes,uint32_t(a[3]),old));if(status)return status;if(!Write(a[4],&old,4))return AccessViolation;}
    return Output(a[1],base)&&Output(sizePointer,bytes)?0:AccessViolation;
}
uint32_t Services::Sections(Op op,const uint64_t* a) noexcept {
    if(op==Op::CreateSection){if(!Probe(a[0],8,true))return AccessViolation;char name[128]{};auto status=ObjectName(a[2],name,1,true);if(status)return status;
        if(a[6]||a[5]!=0x8000000||a[1]&~0xF001Full||(a[4]!=memory::ReadOnly&&a[4]!=memory::ReadWrite))return Unsupported;
        int64_t maximum=0;if(!Read(a[3],&maximum,8))return AccessViolation;if(maximum<=0||maximum>64*4096)return Invalid;
        uint32_t index=0;for(;index<8;++index)if(!sections_[index].references)break;if(index==8)return NoMemory;
        auto* h=New(1,uint32_t(a[1]),0,index);if(!h)return NoMemory;auto& s=sections_[index];s.references=1;s.access=uint32_t(a[1]);s.protection=uint32_t(a[4]);s.pages=uint32_t((maximum+4095)/4096);
        for(uint32_t i=0;i<s.pages;++i)if(!(s.data[i]=memory_->physical.Allocate(memory::Frame::Data))){for(uint32_t j=0;j<i;++j)memory_->physical.Release(s.data[j],memory::Frame::Data);s={};*h={};return NoMemory;}
        Output(a[0],h->id);return 0;
    }
    uint32_t target=0;auto status=Process(a[1],target,processes::WriteMemory);if(op==Op::UnmapSection)status=Process(a[0],target,processes::WriteMemory);if(status)return status;if(target!=pid_)return Denied;
    if(op==Op::UnmapSection){for(auto& r:ranges_)if(r.base&&r.section>=0&&a[1]>=r.base&&a[1]-r.base<r.bytes){status=processes_->Release(pid_,r.base);if(status)return status;DropSection(uint32_t(r.section));r={};return 0;}return 0xC0000019;}
    auto* h=Find(a[0]);if(!h||h->kind!=1)return InvalidHandle;auto& s=sections_[h->index];
    if(!(h->access&4)||(a[9]==memory::ReadWrite&&(!(h->access&2)||s.protection!=memory::ReadWrite)))return Denied;
    if(a[3]||a[4]||a[7]!=2||a[8]||(a[9]!=memory::ReadOnly&&a[9]!=memory::ReadWrite))return Unsupported;
    uint64_t base=0,bytes=0,offset=0;if(!Read(a[2],&base,8)||!Read(a[6],&bytes,8)||!Probe(a[2],8,true)||!Probe(a[6],8,true)||(a[5]&&!Read(a[5],&offset,8)))return AccessViolation;
    if((offset&4095)||offset>=uint64_t(s.pages)*4096)return Invalid;if(!bytes)bytes=uint64_t(s.pages)*4096-offset;
    if(bytes>uint64_t(s.pages)*4096-offset)return Invalid;bytes=(bytes+4095)&~4095ull;Range* range=nullptr;for(auto& r:ranges_)if(!r.base){range=&r;break;}if(!range)return NoMemory;
    status=processes_->Reserve(pid_,base,bytes,uint32_t(a[9]));if(status)return status;auto& p=*processes_->Get(pid_);const auto pages=uint32_t(bytes/4096);
    if(pages>processes::MaxPrivatePages-p.committed)status=NoMemory;else status=uint32_t(memory_->MapReferences(p.space,base,s.data+offset/4096,pages,uint32_t(a[9])));
    if(status){processes_->Release(pid_,base);return status;}p.committed+=pages;*range={base,bytes,int32_t(h->index)};++s.references;Output(a[2],base);Output(a[6],bytes);return 0;
}
