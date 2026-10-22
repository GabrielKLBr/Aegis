// Executes only the owned kernel and local tiny protocol probes in WHP.
#include "../src/microvm/boot/KernelBoot.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace av::microvm::boot;
namespace gi=guardian::io;
namespace {
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Code(KernelBootSession& s,const KernelImage& image,std::initializer_list<uint8_t> code){std::memcpy(static_cast<uint8_t*>(s.Machine().RamData())+image.Entry(),code.begin(),code.size());}
}
int main(int argc,char** argv){try{
    Check(argc==3,"I/O image and report required");const auto image=KernelImage::Read(argv[1]);
    for(CpuOptions bad:{CpuOptions{false,true,false,false,false,true},CpuOptions{true,true,false,false,false,true},CpuOptions{true,true,true,false,true,true}}){KernelBootSession s;bool rejected=false;try{s.Prepare(image,16ull<<20,1,bad);}catch(const std::runtime_error&){rejected=true;}Check(rejected&&!s.Machine().RamData(),"Invalid I/O contract allocated VM");}
    if(!WhpAvailable()){std::cout<<"WHP unavailable\n";return 77;}
    std::vector<BootResult> rows,negative;uint64_t id=1;
    for(uint64_t ram:{16ull<<20,64ull<<20})for(unsigned n=0;n<20;++n){
        KernelBootSession s;s.Prepare(image,ram,id++,{true,true,false,false,true,true});auto r=s.Run();rows.push_back(r);
        if(!r.success){WriteBootReport(argv[2],rows);throw std::runtime_error("I/O boot: "+r.error);}const auto& x=r.io;
        Check(r.ioReportValid&&x.checks==gi::RequiredChecks&&x.coreChecks==630&&x.loads==3&&x.unloads==3&&!x.liveDrivers&&!x.liveRequests&&!x.livePins,"Driver lifecycle/core tests incomplete");
        Check(x.submitted>=6&&x.submitted==x.completed&&x.cancelled>=1&&!x.failed&&x.consoleBytes==6&&x.channelBytes==16&&x.blockBytes>=1024,"Asynchronous I/O paths incomplete");
        Check(x.consoleBytes==r.ioHostConsole&&x.channelBytes==r.ioHostChannel&&x.blockBytes==r.ioHostBlock&&r.ioConsole=="IO OK\n"&&r.ioHostRequests>=5,"Host/guest device accounting mismatch");
        Check(r.processReportValid&&r.processes.checks==32767&&r.processes.completed==2&&r.processes.faulted==1&&r.processes.freeBefore==r.processes.freeAfter+21&&!r.processes.liveObjects&&!r.processes.liveReferences&&!r.memoryFault,"I/O integration broke process isolation/cleanup");
        Check(x.ticks>=1&&x.ticks==r.cpu.timerTicks&&r.cpu.userFaults==1&&!r.cpu.fatal,"I/O completion did not run on guest timer");
    }WriteBootReport(argv[2],rows);
    // Protocol negatives include all request framing and report state boundaries.
    for(unsigned kind=0;kind<22;++kind){
        KernelBootSession s;s.Prepare(image,16ull<<20,id++,{true,kind!=21,false,false,true,true});
        auto* ram=static_cast<uint8_t*>(s.Machine().RamData());
        if(kind<12){
            gi::Packet p{gi::Magic,gi::Version,sizeof(p),id-1,1,1,3,gi::Op::Read,0,512,0,0,0,{}};
            switch(kind){case 0:p.magic=0;break;case 1:++p.version;break;case 2:--p.bytes;break;case 3:++p.bootId;break;case 4:++p.sequence;break;case 5:p.request=0;break;case 6:p.device=4;break;case 7:p.op=static_cast<gi::Op>(4);break;case 8:p.length=513;break;case 9:p.status=1;break;case 10:p.transferred=1;break;default:p.reserved=1;break;}
            std::memcpy(ram+gi::PacketGpa,&p,sizeof(p));
            // Seal code, arm timer, then make one bounded request.
            Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEC,0xE6,0xF0,0xF4});
        }else if(kind<17){
            guardian::processes::Report pr{};pr.magic=guardian::processes::Magic;pr.version=guardian::processes::Version;pr.bytes=sizeof(pr);pr.bootId=id-1;std::memcpy(ram+guardian::processes::ReportGpa,&pr,sizeof(pr));
            gi::Report r{};r.magic=gi::Magic;r.version=gi::Version;r.bytes=sizeof(r);r.bootId=id-1;
            switch(kind){case 12:r.magic=0;break;case 13:++r.bootId;break;case 14:r.reserved[0]=1;break;case 15:r.checks=UINT64_MAX;break;default:++r.version;break;}std::memcpy(ram+gi::ReportGpa,&r,sizeof(r));
            Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEF,0xB0,2,0xE6,0xF0,0xF4});
        }else if(kind==17)Code(s,image,{0xB0,2,0xE6,0xF0,0xF4});
        else if(kind==18)Code(s,image,{0xE4,0xF0,0xF4});
        else if(kind==19)Code(s,image,{0xE7,0xF0,0xF4});
        else if(kind==20)s.FailIoDevice(3);
        auto r=s.Run(kind==21?std::chrono::milliseconds(100):std::chrono::seconds(3));negative.push_back(r);
        Check(!r.success&&!r.error.empty(),"Invalid I/O accepted");Check(r.timedOut==(kind==21),"Protocol/device fault hung or missing-timer boot escaped watchdog");
        if(kind<12)Check(r.ioHostRequests==0&&!r.ioHostBlock,"Malformed I/O mutated device");
        if(kind==20)Check(r.cpuReportValid&&r.ioReportValid&&r.stopStatus&&!r.cpu.fatal&&!r.memoryFault&&!r.ioHostBlock,"Removed block device caused host/kernel fault or write");
    }
    auto path=std::filesystem::path(argv[2]);path.replace_extension("negative.json");WriteBootReport(path,negative);
    std::cout<<"40 asynchronous I/O boots and 22 external failure cases PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
