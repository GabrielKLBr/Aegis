#include "../src/microvm/boot/KernelBoot.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace av::microvm::boot;
namespace gm = guardian::memory;
namespace {
void Check(bool b,const char* why) { if(!b) throw std::runtime_error(why); }
void Code(KernelBootSession& s,const KernelImage& image,std::initializer_list<uint8_t> code) {
    std::memcpy(static_cast<uint8_t*>(s.Machine().RamData())+image.Entry(),code.begin(),code.size());
}
}
int main(int argc,char** argv) {
    try {
        Check(argc==3,"Memory kernel and report path required"); const auto image=KernelImage::Read(argv[1]);
        if(!WhpAvailable()) { std::cout<<"WHP unavailable\n";return 77; }
        std::vector<BootResult> rows,negative; uint64_t id=1;
        for(uint64_t ram:{16ull<<20,64ull<<20}) for(unsigned run=0;run<20;++run) {
            KernelBootSession s; s.Prepare(image,ram,id++,{true,true,true}); const auto r=s.Run(); rows.push_back(r);
            if(!r.success) { WriteBootReport(argv[2],rows); throw std::runtime_error("Memory boot failed: "+r.error); }
            const auto& m=r.memory;
            Check(r.memoryReportValid&&m.checks==gm::RequiredChecks&&m.userFaults==8&&m.guardFaults==1&&m.cycles==128&&m.heapChecks==5,"Missing guest checks");
            Check(m.freeBefore==m.freeAfter+21&&m.peakFrames==m.freeBefore&&m.allocationFailures==30&&m.switches>128&&m.exhaustedFrames==m.freeBefore-42,"Accounting/rollback/CR3 invalid");
            Check(r.cpu.userFaults==8&&r.cpu.userCompleted>=140&&m.faultError[1]==7&&m.faultError[4]==0x15&&m.faultError[5]==5&&m.faultError[6]==5,"Hardware protection paths missing");
            Check(r.physicalRequests==m.physicalRequests&&m.physicalRequests>500&&!r.memoryFault&&!r.timedOut,"Guest/WHP boundary mismatch");
        }
        WriteBootReport(argv[2],rows);
        // Run only tiny owned instructions to exercise malformed host requests.
        for(unsigned kind=0;kind<18;++kind) {
            KernelBootSession s; s.Prepare(image,16ull<<20,id++,{true,true,true});
            auto* ram=static_cast<uint8_t*>(s.Machine().RamData());
            gm::Request request{gm::Magic,gm::Version,sizeof(request),id-1,1,0x800000,4096,3,0};
            if(kind==0) request.magic=0; if(kind==1) ++request.version; if(kind==2) request.bytes=0;
            if(kind==3) ++request.bootId; if(kind==4) request.sequence=0; if(kind==5) request.gpa=UINT64_MAX;
            if(kind==6) request.gpa=0x200000; if(kind==7) request.gpa=0x100000;
            if(kind==8) request.access=7; if(kind==9) request.length=8192; if(kind==10) request.reserved=1;
            std::memcpy(ram+gm::RequestGpa,&request,sizeof(request));
            // EC4 seals the fixed bootstrap code, then ED1 asks for page rights.
            if(kind==11) Code(s,image,{0xB0,4,0xE6,0xEC,0xE4,0xED,0xF4});
            else if(kind==12) Code(s,image,{0xB0,4,0xE6,0xEC,0xB8,1,0,0,0,0xE7,0xED,0xF4});
            else if(kind==13) Code(s,image,{0xB0,1,0xE6,0xED,0xF4});
            else if(kind==14) Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xED,0xE6,0xED,0xF4}); // replay
            else if(kind==15) Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,99,0xE6,0xED,0xF4});
            else if(kind==16) Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,2,0xE6,0xED,0xF4}); // empty report
            else if(kind==17) {
                gm::Report bad{};bad.magic=gm::Magic;bad.version=gm::Version;bad.bytes=sizeof(bad);bad.bootId=id; // wrong session
                std::memcpy(ram+gm::ReportGpa,&bad,sizeof(bad));
                Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,2,0xE6,0xED,0xF4});
            }
            else Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xED,0xF4});
            auto r=s.Run(); negative.push_back(r);
            Check(!r.success&&!r.timedOut&&!r.memoryReportValid&&!r.error.empty()&&r.physicalRequests==(kind==14?1ull:0ull),"Malformed permission request escaped");
        }
        auto path=std::filesystem::path(argv[2]);path.replace_extension("negative.json");WriteBootReport(path,negative);
        std::cout<<"40 real memory-kernel boots; 18 malformed host boundary cases contained\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
