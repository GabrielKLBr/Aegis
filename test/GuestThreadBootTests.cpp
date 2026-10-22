#include "../src/microvm/boot/KernelBoot.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace av::microvm::boot;
namespace gt=guardian::threads;
namespace {
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Code(KernelBootSession& s,const KernelImage& k,std::initializer_list<uint8_t> code){std::memcpy(static_cast<uint8_t*>(s.Machine().RamData())+k.Entry(),code.begin(),code.size());}
}
int main(int argc,char** argv){
    try{
        Check(argc==3,"Thread image and report required");const auto image=KernelImage::Read(argv[1]);
        if(!WhpAvailable()){std::cout<<"WHP unavailable\n";return 77;}
        std::vector<BootResult> rows,negative;uint64_t id=1;
        for(uint64_t ram:{16ull<<20,64ull<<20})for(unsigned i=0;i<20;++i){
            KernelBootSession s;s.Prepare(image,ram,id++,{true,true,false,true});auto r=s.Run();rows.push_back(r);
            if(!r.success){WriteBootReport(argv[2],rows);throw std::runtime_error("Thread boot: "+r.error);}
            const auto& t=r.threads;
            Check(r.threadReportValid&&t.checks==gt::RequiredChecks&&t.coreChecks>=982&&t.completed==8&&t.idleTicks>0&&t.faults==1&&t.apcs==1,"Thread execution incomplete");
            Check(t.ticks>=4&&t.preemptions>=4&&t.counters[0]>0&&t.counters[1]>0&&t.counters[2]==16&&t.counters[3]==16,"No-syscall progress/context failed");
            Check(t.freeBefore==t.freeAfter+21&&r.cpu.timerTicks==t.ticks&&r.timerAcknowledged==t.ticks&&!r.timedOut&&!r.memoryFault,"Resource or timer mismatch");
            Check(r.cpu.userFaults==1&&r.cpu.faults[0].vector==6&&r.cpu.faults[0].cpl==3,"Faulting lock owner was not contained");
        }
        WriteBootReport(argv[2],rows);
        for(unsigned kind=0;kind<12;++kind){
            KernelBootSession s;s.Prepare(image,16ull<<20,id++,{true,kind!=11,false,true});
            auto* ram=static_cast<uint8_t*>(s.Machine().RamData());
            gt::Report report{};report.magic=gt::Magic;report.version=gt::Version;report.bytes=sizeof(report);report.bootId=id-1;
            if(kind==0)report.magic=0;if(kind==1)++report.version;if(kind==2)report.bytes=0;if(kind==3)++report.bootId;
            if(kind==4)report.reserved[0]=1;if(kind==5)report.checks=UINT64_MAX;
            std::memcpy(ram+gt::ReportGpa,&report,sizeof(report));
            if(kind<=5)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEE,0xF4});
            if(kind==6)Code(s,image,{0xB0,1,0xE6,0xEE,0xF4});
            if(kind==7)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,2,0xE6,0xEE,0xF4});
            if(kind==8)Code(s,image,{0xE4,0xEE,0xF4});
            if(kind==9)Code(s,image,{0xE7,0xEE,0xF4});
            if(kind==10)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEE,0xE6,0xEE,0xF4});
            auto r=s.Run(kind==11?std::chrono::milliseconds(80):std::chrono::seconds(3));negative.push_back(r);
            Check(!r.success&&!r.error.empty(),"Invalid thread boundary accepted");
            if(kind==11)Check(r.timedOut,"Missing timer escaped watchdog");else Check(!r.timedOut,"Malformed report hung");
        }
        auto path=std::filesystem::path(argv[2]);path.replace_extension("negative.json");WriteBootReport(path,negative);
        std::cout<<"40 preemptive guest boots and 12 external failure cases PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
