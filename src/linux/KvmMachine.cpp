#include "KvmMachine.h"
#include "guardian/HardwareProjection.h"
#include "guardian/CpuTopology.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace av::microvm {
namespace {
int WakeSignal(){return SIGRTMIN+5;}
void WakeHandler(int){}
uint64_t CpuModel(const kvm_cpuid2& table){
    state::Writer w;
    for(uint32_t i=0;i<table.nent;++i){const auto& e=table.entries[i];
        if(e.function==0){w.Pod(e.ebx);w.Pod(e.ecx);w.Pod(e.edx);}
        if(e.function==1){w.Pod(e.eax);w.Pod(e.ecx&~(1u<<27));w.Pod(e.edx);}
        if((e.function==7 && !e.index) || e.function==0x80000001){w.Pod(e.function);w.Pod(e.ebx*(e.function==7));w.Pod(e.ecx);w.Pod(e.edx);}
        if(e.function==0xD && !e.index){w.Pod(e.eax);w.Pod(e.edx);}
    }return state::Hash(w.data);
}
// One process-wide reserved wake handler; each run unblocks only its own thread.
std::once_flag signalOnce;
bool signalReady=false;
}
bool MicroVm::Available(){
    const int fd=open("/dev/kvm",O_RDWR|O_CLOEXEC);if(fd<0)return false;
    const bool ok=ioctl(fd,KVM_GET_API_VERSION,0)==KVM_API_VERSION &&
        ioctl(fd,KVM_CHECK_EXTENSION,KVM_CAP_USER_MEMORY)>0 &&
        ioctl(fd,KVM_CHECK_EXTENSION,KVM_CAP_READONLY_MEM)>0 &&
        ioctl(fd,KVM_CHECK_EXTENSION,KVM_CAP_IMMEDIATE_EXIT)>0;
    close(fd);return ok;
}
bool MicroVm::SetPortInput(uint32_t value,uint8_t bytes){
    if(!run_||!ioPending_||run_->io.direction!=KVM_EXIT_IO_IN||bytes!=4||run_->io.size!=bytes||run_->io.count!=1||
        run_->io.data_offset>runBytes_||bytes>runBytes_-run_->io.data_offset)return false;
    std::memcpy(reinterpret_cast<uint8_t*>(run_)+run_->io.data_offset,&value,bytes);return true;
}
MicroVm::~MicroVm(){
    if(run_)munmap(run_,runBytes_);
    if(cpu_>=0)close(cpu_);if(vm_>=0)close(vm_);if(kvm_>=0)close(kvm_);
    if(ram_)munmap(ram_,bytes_);

}
bool MicroVm::Fail(const char* operation){
    const auto text=std::string(operation)+": "+std::strerror(errno);
    FailExecution(std::wstring(text.begin(),text.end()));return false;
}
bool MicroVm::Create(size_t bytes,uint64_t exceptionBitmap){
    if(kvm_>=0 || exceptionBitmap || bytes<16*1024*1024 || bytes>(1ull<<30) || (bytes&4095)){
        FailExecution(L"Invalid KVM creation parameters");return false;
    }
    kvm_=open("/dev/kvm",O_RDWR|O_CLOEXEC);if(kvm_<0)return Fail("open KVM");
    if(ioctl(kvm_,KVM_GET_API_VERSION,0)!=KVM_API_VERSION ||
       ioctl(kvm_,KVM_CHECK_EXTENSION,KVM_CAP_READONLY_MEM)<=0 ||
       ioctl(kvm_,KVM_CHECK_EXTENSION,KVM_CAP_IMMEDIATE_EXIT)<=0){FailExecution(L"Required KVM API capabilities unavailable");return false;}
    const int slots=ioctl(kvm_,KVM_CHECK_EXTENSION,KVM_CAP_NR_MEMSLOTS);
    if(slots<32){FailExecution(L"Insufficient KVM memory slots");return false;}maxSlots_=static_cast<uint32_t>(slots);
    vm_=ioctl(kvm_,KVM_CREATE_VM,0);if(vm_<0)return Fail("create KVM VM");
    cpu_=ioctl(vm_,KVM_CREATE_VCPU,0);if(cpu_<0)return Fail("create KVM vCPU");
    const int mapBytes=ioctl(kvm_,KVM_GET_VCPU_MMAP_SIZE,0);if(mapBytes<int(sizeof(kvm_run)))return Fail("query KVM run size");
    runBytes_=static_cast<size_t>(mapBytes);
    auto* mapping=mmap(nullptr,runBytes_,PROT_READ|PROT_WRITE,MAP_SHARED,cpu_,0);
    if(mapping==MAP_FAILED)return Fail("map KVM run");run_=static_cast<kvm_run*>(mapping);
    mapping=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(mapping==MAP_FAILED)return Fail("allocate guest RAM");ram_=static_cast<uint8_t*>(mapping);bytes_=bytes;
    // CPUID is supplied explicitly, so the guest can check NX and FXSR.
    std::vector<uint8_t> cpuidBytes(sizeof(kvm_cpuid2)+256*sizeof(kvm_cpuid_entry2));
    auto* cpuid=reinterpret_cast<kvm_cpuid2*>(cpuidBytes.data());cpuid->nent=256;
    if(ioctl(kvm_,KVM_GET_SUPPORTED_CPUID,cpuid)<0)return Fail("query guest CPUID");
    bool nx=false;
    for(uint32_t i=0;i<cpuid->nent;++i){auto& e=cpuid->entries[i];
        if(e.function==0x80000001)nx=(e.edx&(1u<<20))!=0;
        if(e.function==1)e.ecx|=1u<<31;
        if(!cpuIdentityBrand_.empty())guardian::hardware::CpuIdentity(cpuIdentityBrand_.c_str(),e.function,e.index,e.eax,e.ebx,e.ecx,e.edx);
        if(topologyEnabled_)guardian::hardware::CpuTopology(topologyCpus_,0,e.function,e.index,e.eax,e.ebx,e.ecx,e.edx);
    }
    if(topologyEnabled_){
        uint32_t maxLeaf=0;for(uint32_t i=0;i<cpuid->nent;++i)if(cpuid->entries[i].function==0)maxLeaf=cpuid->entries[i].eax;
        if(maxLeaf<0xB){FailExecution(L"CPU topology leaves unavailable");return false;}
        for(const auto leaf:{0xBu,0x1Fu}){
            if(leaf>maxLeaf)continue;
            for(uint32_t sub=0;sub<3;++sub){kvm_cpuid_entry2* target=nullptr;
                for(uint32_t i=0;i<cpuid->nent;++i)if(cpuid->entries[i].function==leaf&&cpuid->entries[i].index==sub)target=&cpuid->entries[i];
                if(!target){if(cpuid->nent==256)return Fail("CPUID table full");target=&cpuid->entries[cpuid->nent++];*target={};target->function=leaf;target->index=sub;}
                target->flags=KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
                guardian::hardware::CpuTopology(topologyCpus_,0,leaf,sub,target->eax,target->ebx,target->ecx,target->edx);
            }
        }
    }
    if(!nx){FailExecution(L"KVM does not expose NX");return false;}
    cpuidIdentity_=CpuModel(*cpuid);
    if(!cpuIdentityBrand_.empty()){state::Writer w;w.Pod(cpuidIdentity_);w.Text(cpuIdentityBrand_);if(topologyCpus_>1)w.Pod(topologyCpus_);cpuidIdentity_=state::Hash(w.data);}
    if(ioctl(cpu_,KVM_SET_CPUID2,cpuid)<0)return Fail("set guest CPUID");
    std::call_once(signalOnce,[]{struct sigaction action{};action.sa_handler=WakeHandler;sigemptyset(&action.sa_mask);signalReady=sigaction(WakeSignal(),&action,nullptr)==0;});
    if(!signalReady)return Fail("install KVM wake signal");
    owner_=pthread_self();signalInstalled_=true;
    access_.assign(bytes/4096,0);
    return SetMemoryAccess(0,bytes,3);
}
bool MicroVm::CanAccessMemory(uint64_t gpa,uint64_t bytes,uint8_t access) const {
    if(fatal_||!ram_||gpa>bytes_||bytes>bytes_-gpa||(access&~uint8_t(7)))return false;
    if(!bytes)return true;
    const auto first=static_cast<size_t>(gpa/4096),last=static_cast<size_t>((gpa+bytes-1)/4096);
    if(last>=access_.size())return false;
    for(size_t n=first;n<=last;++n)if((access_[n]&access)!=access)return false;
    return true;
}
bool MicroVm::SetMemoryAccess(uint64_t gpa,uint64_t bytes,uint8_t access){
    if(fatal_ || !bytes || (gpa&4095) || (bytes&4095) || gpa>=bytes_ || bytes>bytes_-gpa ||
       (access!=0 && access!=1 && access!=3 && access!=5)){FailExecution(L"Invalid physical memory permission");return false;}
    auto desired=access_;std::fill(desired.begin()+gpa/4096,desired.begin()+(gpa+bytes)/4096,access);
    std::vector<Slot> ranges;
    for(size_t i=0;i<desired.size();){
        const auto start=i;const auto kind=desired[i]?((desired[i]&2)?3:1):0;
        while(i<desired.size() && (desired[i]?((desired[i]&2)?3:1):0)==kind)++i;
        if(kind)ranges.push_back({start*4096,(i-start)*4096,UINT32_MAX,kind==1});
    }
    if(ranges.size()>maxSlots_){FailExecution(L"KVM physical memory slot limit");return false;}
    std::vector<bool> used(maxSlots_,false);
    for(auto& r:ranges)for(const auto& s:slots_)if(r.gpa==s.gpa && r.bytes==s.bytes && r.readOnly==s.readOnly){r.id=s.id;used[s.id]=true;break;}
    // Remove old overlapping ranges before adding any replacement.
    for(const auto& s:slots_)if(!used[s.id]){kvm_userspace_memory_region region{};region.slot=s.id;
        if(ioctl(vm_,KVM_SET_USER_MEMORY_REGION,&region)<0)return Fail("remove KVM memory slot");}
    for(auto& r:ranges)if(r.id==UINT32_MAX){
        const auto it=std::find(used.begin(),used.end(),false);r.id=static_cast<uint32_t>(it-used.begin());used[r.id]=true;
        kvm_userspace_memory_region region{};region.slot=r.id;region.flags=r.readOnly?KVM_MEM_READONLY:0;
        region.guest_phys_addr=r.gpa;region.memory_size=r.bytes;region.userspace_addr=reinterpret_cast<uint64_t>(ram_+r.gpa);
        if(ioctl(vm_,KVM_SET_USER_MEMORY_REGION,&region)<0)return Fail("set KVM memory slot");
    }
    access_=std::move(desired);slots_=std::move(ranges);return true;
}
bool MicroVm::SetupLongMode(uint64_t entry,uint64_t stack){
    auto* pml4=reinterpret_cast<uint64_t*>(ram_+0x70000);
    auto* pdpt=reinterpret_cast<uint64_t*>(ram_+0x71000);
    auto* pd=reinterpret_cast<uint64_t*>(ram_+0x72000);
    pml4[0]=0x71003;pdpt[0]=0x72003;
    for(unsigned i=0;i<512;++i)pd[i]=(uint64_t(i)<<21)|0x83;
    kvm_sregs s{};if(ioctl(cpu_,KVM_GET_SREGS,&s)<0)return Fail("read KVM special registers");
    s.cr0=0x80000033;s.cr3=0x70000;s.cr4=0x620;s.efer=0x500;
    kvm_segment code{};code.base=0;code.limit=0xFFFFFFFF;code.selector=8;code.type=11;
    code.present=1;code.s=1;code.l=1;code.g=1;
    kvm_segment data=code;data.selector=16;data.type=3;data.l=0;data.db=1;
    s.cs=code;s.ds=s.es=s.ss=s.fs=s.gs=data;
    s.tr.base=0;s.tr.limit=0xFFFF;s.tr.selector=0;s.tr.type=11;s.tr.present=1;s.tr.s=0;
    s.ldt.unusable=1;s.gdt.base=0;s.gdt.limit=0;s.idt.base=0;s.idt.limit=0;
    if(ioctl(cpu_,KVM_SET_SREGS,&s)<0)return Fail("set KVM long mode");
    kvm_regs r{};r.rip=entry;r.rsp=stack;r.rflags=2;
    if(ioctl(cpu_,KVM_SET_REGS,&r)<0)return Fail("set KVM entry registers");
    kvm_fpu fpu{};fpu.fcw=0x37F;fpu.mxcsr=0x1F80;
    if(ioctl(cpu_,KVM_SET_FPU,&fpu)<0)return Fail("set KVM floating point state");
    return true;
}
bool MicroVm::BootArguments(uint64_t boot,uint64_t cpu){
    kvm_regs r{};if(ioctl(cpu_,KVM_GET_REGS,&r)<0)return Fail("read KVM registers");
    r.rcx=boot;r.rdx=cpu;r.r8=r.r9=0;
    if(ioctl(cpu_,KVM_SET_REGS,&r)<0)return Fail("set KVM boot arguments");return true;
}
bool MicroVm::CanInjectInterrupt() const{return run_ && run_->ready_for_interrupt_injection;}
bool MicroVm::InterruptWindow(bool requested){run_->request_interrupt_window=requested;return true;}
bool MicroVm::TimerInterrupt(bool pending){
    if(pending){kvm_interrupt irq{0x40};if(ioctl(cpu_,KVM_INTERRUPT,&irq)<0)return Fail("inject KVM timer");}
    else {
        kvm_sregs s{};if(ioctl(cpu_,KVM_GET_SREGS,&s)<0)return Fail("read pending KVM timer");
        s.interrupt_bitmap[1]&=~uint64_t(1);
        if(ioctl(cpu_,KVM_SET_SREGS,&s)<0)return Fail("cancel pending KVM timer");
    }return true;
}
bool MicroVm::ReadCpl(uint32_t& cpl){kvm_sregs s{};if(ioctl(cpu_,KVM_GET_SREGS,&s)<0)return Fail("read KVM CS");cpl=s.cs.selector&3;return true;}
bool MicroVm::Kick(){
    if(!signalInstalled_)return false;
    __atomic_store_n(&run_->immediate_exit,1,__ATOMIC_RELAXED);
    return pthread_kill(owner_.load(),WakeSignal())==0;
}
bool MicroVm::Cancel(){cancel_=true;return Kick();}
bool MicroVm::WakeForService(){wake_=true;return Kick();}
bool MicroVm::RequestPause(){pause_=true;return Kick();}
bool MicroVm::CompleteIo(){
    if(!ioPending_)return true;
    __atomic_store_n(&run_->immediate_exit,1,__ATOMIC_RELAXED);
    const int result=ioctl(cpu_,KVM_RUN,0);
    if(result>=0 || errno!=EINTR)return Fail("complete pending KVM I/O");
    ioPending_=false;return true;
}
bool MicroVm::Run(uint64_t budget){
    if(fatal_)return false;
    struct SignalScope {sigset_t previous{};SignalScope(){sigset_t s;sigemptyset(&s);sigaddset(&s,WakeSignal());pthread_sigmask(SIG_UNBLOCK,&s,&previous);}~SignalScope(){pthread_sigmask(SIG_SETMASK,&previous,nullptr);}} signalScope;
    owner_=pthread_self();
    stop_=false;cancel_=false;paused_=false;pause_=false;
    const auto first=exits_;
    while(!stop_ && !cancel_ && !pause_ && exits_-first<budget){
        __atomic_store_n(&run_->immediate_exit,0,__ATOMIC_RELAXED);
        wake_.exchange(false);
        if(service_)service_(*this);
        if(stop_ || cancel_)break;
        hasRun_=true;const int status=ioctl(cpu_,KVM_RUN,0);
        if(status<0){if(errno==EINTR)continue;return Fail("KVM_RUN");}
        ioPending_=false;++exits_;
        switch(run_->exit_reason){
        case KVM_EXIT_IO:{
            ioPending_=true;
            const auto& io=run_->io;
            // Repeated/string I/O is outside the fixed doorbell ABI.
            if((io.direction!=KVM_EXIT_IO_OUT&&io.direction!=KVM_EXIT_IO_IN)||(io.size!=1&&io.size!=2&&io.size!=4)||io.count!=1||
               io.data_offset>runBytes_||io.size>runBytes_-io.data_offset){FailExecution(L"KVM I/O shape rejected");break;}
            const auto it=traps_.find(io.port);
            if(it==traps_.end()){FailExecution(L"Unregistered KVM I/O port");break;}
            uint32_t value=0;auto* data=reinterpret_cast<uint8_t*>(run_)+io.data_offset;
            if(io.direction==KVM_EXIT_IO_OUT)std::memcpy(&value,data,io.size);else std::memset(data,0,io.size);
            it->second(*this,{io.port,io.size,io.direction==KVM_EXIT_IO_OUT,false,false,value,0});break;
        }
        case KVM_EXIT_IRQ_WINDOW_OPEN:break;
        case KVM_EXIT_HLT:usleep(1000);break;
        case KVM_EXIT_INTR:break;
        case KVM_EXIT_MMIO:memoryFault_=true;faultGpa_=run_->mmio.phys_addr;FailExecution(L"KVM access to unmapped or read-only physical memory");break;
        case KVM_EXIT_SHUTDOWN:FailExecution(L"KVM guest shutdown/triple fault");break;
        case KVM_EXIT_FAIL_ENTRY:FailExecution(L"KVM invalid guest entry state");break;
        default:FailExecution(L"Unsupported KVM exit "+std::to_wstring(run_->exit_reason));break;
        }
    }
    if(!CompleteIo())return false;
    kvm_regs r{};if(ioctl(cpu_,KVM_GET_REGS,&r)==0)rip_=r.rip;
    if(pause_ && !stop_ && !cancel_){paused_=true;return true;}
    if(cancel_){FailExecution(L"KVM execution deadline expired");return false;}
    if(!stop_)FailExecution(L"KVM exit budget exhausted");
    return stop_ && !fatal_;
}
}

namespace av::microvm {
namespace {
constexpr uint32_t SavedMsrs[]={0x10,0x174,0x175,0x176,0x277,0x1B,0xC0000081,0xC0000082,0xC0000083,0xC0000084,0xC0000100,0xC0000101,0xC0000102,0xC0000103};
struct KvmProcessor {
    uint64_t magic=0x324D564B39564147ull,cpuidHash=0;int32_t tscKhz=0;uint32_t window=0;
    kvm_regs regs{};kvm_sregs sregs{};kvm_debugregs debug{};kvm_mp_state mp{};
    kvm_vcpu_events events{};kvm_xcrs xcrs{};
};
std::vector<uint8_t> CpuTable(int cpu){
    std::vector<uint8_t> bytes(sizeof(kvm_cpuid2)+256*sizeof(kvm_cpuid_entry2));auto* table=reinterpret_cast<kvm_cpuid2*>(bytes.data());table->nent=256;
    state::Require(ioctl(cpu,KVM_GET_CPUID2,table)==0 && table->nent && table->nent<=256,"Cannot capture virtual CPUID table");bytes.resize(sizeof(kvm_cpuid2)+table->nent*sizeof(kvm_cpuid_entry2));return bytes;
}
std::vector<uint8_t> MsrBuffer(){
    std::vector<uint8_t> bytes(sizeof(kvm_msrs)+std::size(SavedMsrs)*sizeof(kvm_msr_entry));
    auto* p=reinterpret_cast<kvm_msrs*>(bytes.data());p->nmsrs=uint32_t(std::size(SavedMsrs));
    for(uint32_t i=0;i<p->nmsrs;++i)p->entries[i].index=SavedMsrs[i];return bytes;
}
}
bool MicroVm::Capture(state::MachineState& out){
    if(fatal_ || !ram_ || !CompleteIo())return false;
    KvmProcessor p;p.cpuidHash=cpuidIdentity_;p.tscKhz=ioctl(cpu_,KVM_GET_TSC_KHZ,0);p.window=run_->request_interrupt_window;
    if(p.tscKhz<=0 || ioctl(cpu_,KVM_GET_REGS,&p.regs)<0 || ioctl(cpu_,KVM_GET_SREGS,&p.sregs)<0 ||
       ioctl(cpu_,KVM_GET_DEBUGREGS,&p.debug)<0 || ioctl(cpu_,KVM_GET_MP_STATE,&p.mp)<0 ||
       ioctl(cpu_,KVM_GET_VCPU_EVENTS,&p.events)<0 || ioctl(cpu_,KVM_GET_XCRS,&p.xcrs)<0)return Fail("capture KVM processor");
    const int size=ioctl(vm_,KVM_CHECK_EXTENSION,KVM_CAP_XSAVE2);
    if(size<4096 || size>65536){FailExecution(L"KVM XSAVE2 snapshot capability unavailable");return false;}
    std::vector<uint8_t> xsave(size_t(size),uint8_t{});auto msrs=MsrBuffer();
    if(ioctl(cpu_,KVM_GET_XSAVE2,xsave.data())<0 || ioctl(cpu_,KVM_GET_MSRS,msrs.data())!=int(std::size(SavedMsrs)))return Fail("capture KVM XSAVE/MSRs");
    state::Writer w;w.Pod(p);w.Bytes(xsave);w.Bytes(msrs);w.Bytes(CpuTable(cpu_));
    state::MachineState candidate;candidate.backend=1;candidate.exits=exits_;candidate.access=access_;
    candidate.ram.assign(ram_,ram_+bytes_);candidate.processor=std::move(w.data);out=std::move(candidate);return true;
}
bool MicroVm::Restore(const state::MachineState& saved){
    saved.Validate();state::Require(saved.backend==1 && saved.ram.size()==bytes_,"Snapshot backend/RAM mismatch");
    state::Reader r(saved.processor);const auto p=r.Pod<KvmProcessor>();auto xsave=r.Bytes(65536),msrs=r.Bytes(4096),cpuid=r.Bytes(16384);r.End();
    const auto expected=MsrBuffer();
    state::Require(p.magic==0x324D564B39564147ull,"Saved KVM CPU magic rejected");
    if(p.cpuidHash!=cpuidIdentity_)throw std::runtime_error("Saved KVM CPUID mismatch: "+std::to_string(p.cpuidHash)+" / "+std::to_string(cpuidIdentity_));
    state::Require(p.tscKhz==ioctl(cpu_,KVM_GET_TSC_KHZ,0),"Saved KVM TSC frequency mismatch");
    state::Require(p.window<=1 && xsave.size()==size_t(ioctl(vm_,KVM_CHECK_EXTENSION,KVM_CAP_XSAVE2)) && msrs.size()==expected.size(),"Incompatible saved KVM CPU sizes");
    auto* m=reinterpret_cast<kvm_msrs*>(msrs.data());state::Require(m->nmsrs==std::size(SavedMsrs),"Invalid saved MSR count");
    for(uint32_t i=0;i<m->nmsrs;++i)state::Require(m->entries[i].index==SavedMsrs[i] && !m->entries[i].reserved,"Invalid saved MSR");
    state::Require(cpuid.size()>=sizeof(kvm_cpuid2),"Saved CPUID table truncated");auto* table=reinterpret_cast<kvm_cpuid2*>(cpuid.data());
    state::Require(table->nent && table->nent<=256 && !table->padding && cpuid.size()==sizeof(kvm_cpuid2)+table->nent*sizeof(kvm_cpuid_entry2),"Saved CPUID table rejected");
    for(uint32_t i=0;i<table->nent;++i){const auto& entry=table->entries[i];state::Require(!(entry.flags&~7u) && !entry.padding[0] && !entry.padding[1] && !entry.padding[2],"Saved CPUID entry rejected");}
    if(!cpuIdentityBrand_.empty())for(uint32_t i=0;i<table->nent;++i){const auto& e=table->entries[i];auto a=e.eax,b=e.ebx,c=e.ecx,d=e.edx;
        guardian::hardware::CpuIdentity(cpuIdentityBrand_.c_str(),e.function,e.index,a,b,c,d);
        if(topologyEnabled_)guardian::hardware::CpuTopology(topologyCpus_,topologyId_,e.function,e.index,a,b,c,d);
        state::Require(a==e.eax&&b==e.ebx&&c==e.ecx&&d==e.edx,"Saved virtual CPU identity/topology rejected");}
    if(hasRun_)state::Require(CpuTable(cpu_)==cpuid,"Virtual CPU profile changes require a fresh candidate");
    else if(ioctl(cpu_,KVM_SET_CPUID2,table)<0)return Fail("restore virtual CPUID table");
    for(size_t at=0;at<saved.access.size();){const auto first=at;const auto a=saved.access[at];while(at<saved.access.size() && saved.access[at]==a)++at;
        if(!SetMemoryAccess(first*4096,(at-first)*4096,a))return false;}
    std::memcpy(ram_,saved.ram.data(),bytes_);
    if(ioctl(cpu_,KVM_SET_SREGS,&p.sregs)<0 || ioctl(cpu_,KVM_SET_XCRS,&p.xcrs)<0 || ioctl(cpu_,KVM_SET_XSAVE,xsave.data())<0 ||
       ioctl(cpu_,KVM_SET_DEBUGREGS,&p.debug)<0 || ioctl(cpu_,KVM_SET_MSRS,msrs.data())!=int(m->nmsrs) ||
       ioctl(cpu_,KVM_SET_REGS,&p.regs)<0 || ioctl(cpu_,KVM_SET_MP_STATE,&p.mp)<0 || ioctl(cpu_,KVM_SET_VCPU_EVENTS,&p.events)<0)return Fail("restore KVM processor");
    run_->request_interrupt_window=uint8_t(p.window);stop_=false;cancel_=false;pause_=false;paused_=true;ioPending_=false;
    exits_=saved.exits;return true;
}
}
