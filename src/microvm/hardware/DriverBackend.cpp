#include "DriverBackend.h"
#include <cstring>
namespace av::microvm::hardware {
using state::Require;
DriverBackend::DriverBackend(MicroVm& vm,uint64_t boot,const DriverOptions& options):vm_(vm),options_(options){
    using namespace guardian::driver;
    Require(!options.image.empty()&&options.image.size()<=MaxFile&&!options.api.empty()&&options.api.size()<=MaxFile,"Driver source bounds rejected");
    state::Writer w;w.Bytes(options.image);w.Bytes(options.api);
    Require(!options.service.empty()&&options.service.size()<64,"Driver service name rejected");
    boot_={Magic,Version,sizeof(Boot),boot,state::Hash(w.data),static_cast<uint32_t>(options.image.size()),static_cast<uint32_t>(options.api.size()),{0,0,0},{}};
    std::memcpy(boot_.service,options.service.data(),options.service.size());
    auto* ram=static_cast<uint8_t*>(vm_.RamData());std::memcpy(ram+BootGpa,&boot_,sizeof(boot_));
    std::memcpy(ram+SourceGpa,options.image.data(),options.image.size());std::memcpy(ram+ApiGpa,options.api.data(),options.api.size());
    Require(vm_.SetMemoryAccess(BootGpa,4096,1)&&vm_.SetMemoryAccess(SourceGpa,2*MaxFile,1),"Driver source protection failed");
}
void DriverBackend::Control(const MicroVm::IoAccess& a){
    namespace gd=guardian::driver;
    if(!a.write||a.bytes!=1||a.string||a.repeat||a.value!=1||valid_){vm_.FailExecution(L"Driver control I/O rejected");return;}
    gd::Request r{};auto* ram=static_cast<uint8_t*>(vm_.RamData());std::memcpy(&r,ram+gd::RequestGpa,sizeof(r));
    if(r.magic!=gd::Magic||r.version!=gd::Version||r.bytes!=sizeof(r)||r.sequence!=sequence_+1||r.status||r.reserved||++sequence_>256){vm_.FailExecution(L"Driver control packet rejected");return;}
    if(r.op==gd::Op::Protect){
        const bool range=r.span&&!(r.gpa&4095)&&!(r.span&4095)&&r.gpa>=gd::Arena&&r.gpa<gd::ArenaEnd&&r.span<=gd::ArenaEnd-r.gpa;
        const bool access=r.access==0||r.access==1||r.access==3||r.access==5;
        if(!range||!access||!vm_.SetMemoryAccess(r.gpa,r.span,static_cast<uint8_t>(r.access)))r.status=1;
    }else if(r.op==gd::Op::Report){
        if(r.gpa||r.span||r.access){vm_.FailExecution(L"Driver report packet rejected");return;}
        std::memcpy(&report_,ram+gd::ReportGpa,sizeof(report_));
        if(report_.magic!=gd::Magic||report_.version!=gd::Version||report_.bytes!=sizeof(report_)||report_.bootId!=boot_.bootId||report_.packageHash!=boot_.packageHash||
            report_.installed>1||report_.initialized>report_.installed||report_.functional>report_.initialized||report_.unloaded>report_.initialized||report_.operations>4||ready_){vm_.FailExecution(L"Driver lifecycle report rejected");return;}
        if(report_.functional&&(writes_!=4||reads_!=4||report_.operations!=4||report_.apiBefore!=0x0000000100010101ull||report_.apiAfter!=0x0000010101010000ull||!report_.unloaded||report_.error)){
            vm_.FailExecution(L"Driver functional claim lacks independent device/lifecycle proof");return;}
        valid_=true;
    }else {vm_.FailExecution(L"Unknown driver control operation");return;}
    std::memcpy(ram+gd::RequestGpa,&r,sizeof(r));
}
void DriverBackend::Exchange(const MicroVm::IoAccess& a){
    if(a.bytes!=4||a.string||a.repeat||valid_){vm_.FailExecution(L"Driver device I/O shape rejected");return;}
    if(a.write){if(ready_||writes_>=4){vm_.FailExecution(L"Driver device sequence rejected");return;}pending_=guardian::driver::Exchange(a.value);++writes_;ready_=true;}
    else {if(!ready_||!vm_.SetPortInput(pending_,4)){vm_.FailExecution(L"Driver device read rejected");return;}++reads_;ready_=false;}
}
void DriverBackend::Save(state::Writer& w) const {w.Pod(boot_.packageHash);w.Pod(sequence_);w.Pod(writes_);w.Pod(reads_);w.Pod(pending_);w.Pod(uint32_t(ready_));w.Pod(uint32_t(valid_));w.Pod(report_);}
void DriverBackend::Load(state::Reader& r){
    Require(r.Pod<uint64_t>()==boot_.packageHash,"Snapshot driver package mismatch");sequence_=r.Pod<uint64_t>();writes_=r.Pod<uint32_t>();reads_=r.Pod<uint32_t>();pending_=r.Pod<uint32_t>();
    const auto ready=r.Pod<uint32_t>(),valid=r.Pod<uint32_t>();report_=r.Pod<guardian::driver::Report>();
    Require(sequence_<=256&&writes_<=4&&reads_<=writes_&&writes_-reads_<=1&&ready<=1&&valid<=1&&ready==writes_-reads_,"Snapshot driver device state rejected");ready_=ready!=0;valid_=valid!=0;
    Require(!valid_||(report_.magic==guardian::driver::Magic&&report_.version==guardian::driver::Version&&report_.bytes==sizeof(report_)&&
        report_.bootId==boot_.bootId&&report_.packageHash==boot_.packageHash&&report_.installed<=1&&report_.initialized<=report_.installed&&
        report_.functional<=report_.initialized&&report_.unloaded<=report_.initialized&&report_.operations<=4&&!ready_),"Snapshot driver report identity rejected");
    Require(!valid_||!report_.functional||(writes_==4&&reads_==4&&report_.operations==4&&report_.apiBefore==0x0000000100010101ull&&
        report_.apiAfter==0x0000010101010000ull&&report_.unloaded&&!report_.error),"Snapshot driver functional proof rejected");
}
void DriverBackend::ValidateSnapshot(const state::MachineState& s) const {
    using namespace guardian::driver;
    Require(s.ram.size()==vm_.RamSize()&&s.ram.size()>=ApiGpa+MaxFile,"Snapshot driver RAM rejected");
    Require(std::memcmp(s.ram.data()+BootGpa,&boot_,sizeof(boot_))==0&&std::memcmp(s.ram.data()+SourceGpa,options_.image.data(),options_.image.size())==0&&
        std::memcmp(s.ram.data()+ApiGpa,options_.api.data(),options_.api.size())==0,"Snapshot driver sources changed");
    Require(s.access.at(BootGpa/4096)==1,"Snapshot driver descriptor protection rejected");
    for(uint64_t at=SourceGpa;at<ApiGpa+MaxFile;at+=4096)Require(s.access.at(at/4096)==1,"Snapshot driver source protection rejected");
}
}
