// Only the project kernel and small local protocol probes execute in WHP.
#include "../src/microvm/boot/KernelBoot.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <stdexcept>
using namespace av::microvm::boot;
namespace gs=guardian::storage;
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
uint64_t HashFile(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});return gs::Hash(bytes.data(),static_cast<uint32_t>(bytes.size()));}
void Code(KernelBootSession& s,const KernelImage& i,std::initializer_list<uint8_t> code){std::memcpy(static_cast<uint8_t*>(s.Machine().RamData())+i.Entry(),code.begin(),code.size());}
}
int main(int argc,char** argv){try{
    Check(argc==3,"Storage image and report required");const auto image=KernelImage::Read(argv[1]);
    KernelBootSession invalid;bool rejected=false;try{invalid.Prepare(image,16ull<<20,1,{true,true,false,false,true,true,true});}catch(const std::runtime_error&){rejected=true;}Check(rejected&&!invalid.Machine().RamData(),"Missing disk paths allocated VM");
    if(!WhpAvailable()){std::cout<<"WHP unavailable\n";return 77;}
    const auto report=std::filesystem::absolute(argv[2]);const auto directory=report.parent_path()/(report.stem().string()+"-volumes")/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(directory);
    const auto base=directory/"base.gvd";uint64_t id=1,baseHash=0;std::vector<BootResult> positive,negative;
    for(uint64_t ram:{16ull<<20,64ull<<20})for(uint32_t pair=0;pair<10;++pair){
        StorageOptions disk{base,directory/("ram-"+std::to_string(ram)+"-pair-"+std::to_string(pair)+".gvo"),true};uint64_t previous=0;
        for(uint32_t restart=0;restart<2;++restart){KernelBootSession s;s.Prepare(image,ram,id++,{true,true,false,false,true,true,true},disk);auto r=s.Run();positive.push_back(r);
            if(!r.success){WriteBootReport(report,positive);throw std::runtime_error("Persistent boot: "+r.error);}const auto& x=r.storage;
            Check(r.storageReportValid&&x.checks==gs::RequiredChecks&&x.coreChecks==784&&x.sequenceBefore==previous&&x.sequenceAfter==previous+2&&x.recovered==restart,"File/registry restart did not recover prior committed state");previous=x.sequenceAfter;
            Check(x.userCalls>=10&&x.reads&&x.writes&&x.registryChanges==2&&x.denials&&!x.liveHandles&&!x.liveWatches&&r.storageRecords>0,"Storage syscall/cleanup paths incomplete");
            Check(r.ioReportValid&&r.io.checks==4095&&!r.io.failed&&!r.io.liveDrivers&&!r.io.liveRequests&&!r.io.livePins&&r.io.blockBytes==r.ioHostBlock,"Storage bypassed bounded I/O/driver cleanup");
            Check(r.processReportValid&&r.processes.checks==32767&&!r.processes.liveReferences&&!r.processes.liveObjects&&r.processes.freeBefore==r.processes.freeAfter+21&&!r.memoryFault&&!r.cpu.fatal,"Storage broke process/memory isolation");
        }
        const auto now=HashFile(base);if(!baseHash)baseHash=now;Check(now==baseHash,"Base disk changed");
    }WriteBootReport(report,positive);
    for(uint32_t kind=0;kind<18;++kind){
        StorageOptions disk{base,directory/("negative-"+std::to_string(kind)+".gvo"),false};
        if(kind==13){PersistentBlockStore seed(disk);uint8_t bytes[512]{};bytes[0]='x';Check(seed.Write(gs::FirstSlot,bytes)&&seed.Write(gs::SecondSlot,bytes)&&seed.Flush(),"Cannot seed corrupt volume");}
        KernelBootSession s;s.Prepare(image,16ull<<20,id++,{true,kind!=17,false,false,true,true,true},disk);auto* ram=static_cast<uint8_t*>(s.Machine().RamData());
        guardian::processes::Report p{};p.magic=guardian::processes::Magic;p.version=guardian::processes::Version;p.bytes=sizeof(p);p.bootId=id-1;std::memcpy(ram+guardian::processes::ReportGpa,&p,sizeof(p));
        guardian::io::Report io{};io.magic=guardian::io::Magic;io.version=guardian::io::Version;io.bytes=sizeof(io);io.bootId=id-1;std::memcpy(ram+guardian::io::ReportGpa,&io,sizeof(io));
        gs::Report st{};st.magic=gs::Magic;st.version=gs::Version;st.bytes=sizeof(st);st.bootId=id-1;
        switch(kind){case 0:st.magic=0;break;case 1:++st.version;break;case 2:st.bytes=0;break;case 3:++st.bootId;break;case 4:st.reserved[0]=1;break;case 5:st.checks=UINT64_MAX;break;default:break;}std::memcpy(ram+gs::ReportGpa,&st,sizeof(st));
        if(kind<6)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEF,0xB0,2,0xE6,0xF0,0xB0,1,0xE6,0xF1,0xF4});
        if(kind==6)Code(s,image,{0xB0,1,0xE6,0xF1,0xF4});
        if(kind==7)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEF,0xB0,2,0xE6,0xF0,0xB0,1,0xE6,0xF1,0xE6,0xF1,0xF4});
        if(kind==8)Code(s,image,{0xE4,0xF1,0xF4});if(kind==9)Code(s,image,{0xE7,0xF1,0xF4});if(kind==10)Code(s,image,{0xB0,2,0xE6,0xF1,0xF4});
        if(kind==11)Code(s,image,{0xB8,0,0xE0,0x10,0,0xC6,0,0,0xF4});
        if(kind==12)s.FailIoDevice(3);
        if(kind==14)reinterpret_cast<guardian::io::Bus*>(ram+guardian::io::BusGpa)->magic=0;
        if(kind==15){guardian::cpu::Report c{};c.magic=guardian::cpu::ReportMagic;c.version=guardian::cpu::Version;c.bytes=sizeof(c);c.bootId=id-1;std::memcpy(ram+guardian::cpu::ReportGpa,&c,sizeof(c));Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEF,0xB0,2,0xE6,0xF0,0xB0,3,0xE6,0xEC,0xB0,1,0xE6,0xF1,0xF4});}
        if(kind==16)Code(s,image,{0xB0,4,0xE6,0xEC,0xB0,1,0xE6,0xEF,0xB0,2,0xE6,0xF0,0xB0,1,0xE6,0xF1,0xE6,0xF0,0xF4});
        auto r=s.Run(kind==17?std::chrono::milliseconds(100):std::chrono::seconds(3));negative.push_back(r);Check(!r.success&&!r.error.empty(),"Malformed storage boot accepted");Check(r.timedOut==(kind==17),"Storage failure hung or timer watchdog escaped");
        if(kind==12 || kind==13)Check(r.cpuReportValid&&!r.cpu.fatal&&!r.memoryFault&&r.storageReportValid&&r.stopStatus,"Storage failure was not a diagnosed guest stop");
        if(kind==13)Check(r.storageRecords==2,"Corrupt volume was reformatted/overwritten");
    }
    auto path=report;path.replace_extension("negative.json");WriteBootReport(path,negative);Check(HashFile(base)==baseHash,"Negative boot modified base disk");
    std::cout<<"40 persistent boots (20 restart pairs) and 18 external failure cases PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
