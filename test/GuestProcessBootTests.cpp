// Only the project-built kernel and tiny local instructions execute in WHP.
#include "../src/microvm/boot/KernelBoot.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace av::microvm::boot;
namespace gp=guardian::processes;
namespace {
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Code(KernelBootSession& s,const KernelImage& image,std::initializer_list<uint8_t> code){std::memcpy(static_cast<uint8_t*>(s.Machine().RamData())+image.Entry(),code.begin(),code.size());}
}
int main(int argc,char** argv){
    try{
        Check(argc==3,"Process image and report path required");const auto image=KernelImage::Read(argv[1]);
        for(CpuOptions invalid:{CpuOptions{false,true,false,false,true},CpuOptions{true,true,true,false,true},CpuOptions{true,true,false,true,true}}){
            KernelBootSession s;bool rejected=false;try{s.Prepare(image,16ull<<20,1,invalid);}catch(const std::runtime_error&){rejected=true;}
            Check(rejected&&!s.Machine().RamData(),"Mixed process contract allocated a VM");
        }
        if(!WhpAvailable()){std::cout<<"WHP unavailable\n";return 77;}
        uint64_t id=1;std::vector<BootResult> rows,negative;
        for(uint64_t ram:{16ull<<20,64ull<<20})for(unsigned n=0;n<20;++n){
            KernelBootSession s;s.Prepare(image,ram,id++,{true,true,false,false,true});auto r=s.Run();rows.push_back(r);
            if(!r.success){WriteBootReport(argv[2],rows);throw std::runtime_error("Process boot: "+r.error);}
            const auto& p=r.processes;
            Check(r.processReportValid&&p.checks==gp::RequiredChecks&&p.coreChecks>=693&&p.completed==2&&p.faulted==1&&p.denied>=6&&p.inheritance==1&&p.remoteCopies==2,"Process/security paths incomplete");
            Check(p.ticks>=4&&r.timerAcknowledged==p.ticks&&r.cpu.timerTicks==p.ticks&&p.switches>=15&&!r.timedOut&&!r.memoryFault,"Preemption/CPU boundary mismatch");
            Check(p.freeBefore==p.freeAfter+21&&!p.liveProcesses&&!p.liveObjects&&!p.liveHandles&&!p.liveReferences,"Process teardown leaked resources");
            Check(r.cpu.fatal==0&&r.cpu.userFaults==1&&r.cpu.faults[0].vector==14&&r.cpu.faults[0].error==5&&r.cpu.faults[0].cpl==3,"Memory isolation did not produce a contained guest page fault");
        }
        WriteBootReport(argv[2],rows);
        for(unsigned kind=0;kind<12;++kind){
            KernelBootSession s;s.Prepare(image,16ull<<20,id++,{true,kind!=11,false,false,true});
            gp::Report report{};report.magic=gp::Magic;report.version=gp::Version;report.bytes=sizeof(report);report.bootId=id-1;
            if(kind==0)report.magic=0;if(kind==1)++report.version;if(kind==2)report.bytes=0;if(kind==3)++report.bootId;if(kind==4)report.reserved[0]=1;if(kind==5)report.checks=UINT64_MAX;
            std::memcpy(static_cast<uint8_t*>(s.Machine().RamData())+gp::ReportGpa,&report,sizeof(report));
            if(kind<=5)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEF,0xF4});
            if(kind==6)Code(s,image,{0xB0,1,0xE6,0xEF,0xF4});
            if(kind==7)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,2,0xE6,0xEF,0xF4});
            if(kind==8)Code(s,image,{0xE4,0xEF,0xF4});
            if(kind==9)Code(s,image,{0xE7,0xEF,0xF4});
            if(kind==10)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEF,0xE6,0xEF,0xF4});
            auto r=s.Run(kind==11?std::chrono::milliseconds(100):std::chrono::seconds(3));negative.push_back(r);
            Check(!r.success&&!r.error.empty(),"Malformed process report accepted");Check(r.timedOut==(kind==11),"Invalid report/watchdog behavior");
        }
        auto path=std::filesystem::path(argv[2]);path.replace_extension("negative.json");WriteBootReport(path,negative);
        std::cout<<"40 isolated-process boots and 12 external failure cases PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
