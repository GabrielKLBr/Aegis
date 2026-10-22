#include "../src/microvm/boot/KernelBoot.h"
#include "guardian/Conformance.h"
#include "guardian/SystemClock.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace av::microvm;
using namespace av::microvm::boot;
namespace {
void Check(bool value,const std::string& why) {if(!value)throw std::runtime_error(why);}
std::string ReadCases(const BootResult& result) {
    using namespace guardian::conformance;
    std::istringstream input(result.ioConsole);std::string line,json="[";uint32_t count=0;
    while(std::getline(input,line)) {
        if(!line.starts_with("12C "))continue;
        uint32_t id=UINT32_MAX;uint64_t actual=UINT64_MAX;std::istringstream row(line.substr(4));std::string extra;
        Check(bool(row>>id>>actual)&&!(row>>extra)&&id==count&&id<CaseCount,"Malformed, missing or duplicate observation");
        if(count)json+=',';
        json+="{\"id\":"+std::to_string(id)+",\"case\":\""+Cases[id].name+"\",\"domain\":\""+Cases[id].domain+
              "\",\"actual\":"+std::to_string(actual)+",\"expected\":"+std::to_string(Cases[id].expected)+"}";
        Check(actual==Cases[id].expected,std::string("Conformance mismatch: ")+Cases[id].name+" got "+std::to_string(actual));++count;
    }
    Check(count==CaseCount,"Conformance observations incomplete: "+std::to_string(count));return json+"]";
}
guardian::cpu::Clock Clock(KernelBootSession& session) {
    guardian::cpu::Clock clock{};std::memcpy(&clock,static_cast<const uint8_t*>(session.Machine().RamData())+guardian::cpu::ClockGpa,sizeof(clock));return clock;
}
}
int main(int argc,char** argv) {
    std::vector<BootResult> results;
    try {
        Check(argc==3,"Conformance image and fresh evidence directory required");Check(WhpAvailable(),"Native hypervisor unavailable");
        const auto image=KernelImage::Read(argv[1]);const auto root=std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(root);const auto owned=root/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Check(std::filesystem::create_directory(owned),"Evidence directory already exists");
        std::ofstream matrix(root/"matrix.json",std::ios::trunc);Check(bool(matrix),"Cannot create matrix");
        matrix<<"{\"schema\":1,\"architecture\":\"x64\",\"backend\":\"";
#if defined(__linux__)
        matrix<<"KVM";
#else
        matrix<<"WHP";
#endif
        matrix<<"\",\"programHash\":\""<<GUARDIAN_CONFORMANCE_PROGRAM_ID<<"\",\"referenceCompared\":false,\"runs\":[";bool first=true;uint64_t id=0;
        const CpuOptions options{true,true,false,false,true,true,true};
        const auto record=[&](const BootResult& result,const hardware::Profile& profile,uint64_t ram,bool restored) {
            results.push_back(result);WriteBootReport(root/"boots.json",results);
            Check(result.success,"Conformance guest failed: "+result.error+" console: "+result.ioConsole+" kernel: "+result.console);
            Check(!result.cpu.fatal && !result.memoryFault && !result.timedOut && result.storageReportValid &&
                  !result.processes.liveReferences && !result.storage.liveHandles,"Conformance lifecycle/fault regression");
            const auto observations=ReadCases(result);const auto inventory=hardware::Inventory(profile,ram,"complete");
            if(!first)matrix<<',';
            first=false;
            matrix<<"{\"profile\":\""<<profile.nt<<"\",\"nt\":\""<<inventory.ntMajor<<'.'<<inventory.ntMinor<<'.'<<inventory.ntBuild
                  <<"\",\"ramBytes\":"<<ram<<",\"restored\":"<<(restored?"true":"false")<<",\"observations\":"<<observations<<'}'<<std::flush;
        };
        for(const auto* nt:{"win7","win10","win11"}) {
            auto profile=hardware::HostReference();profile.nt=nt;
            for(uint64_t ram:{16ull<<20,64ull<<20})for(uint32_t repeat=0;repeat<2;++repeat) {
                ++id;const StorageOptions disk{owned/(std::to_string(id)+".gvd"),owned/(std::to_string(id)+".gvo"),true};
                KernelBootSession session;session.Prepare(image,ram,id,options,disk,{}, {},&profile);
                const auto result=session.Run(std::chrono::seconds(15));record(result,profile,ram,false);
            }
            const uint64_t ram=16ull<<20;++id;
            const StorageOptions disk{owned/(std::to_string(id)+".gvd"),owned/(std::to_string(id)+".gvo"),true};
            SessionSnapshot saved;guardian::cpu::Clock before{};
            {
                KernelBootSession session;session.Prepare(image,ram,id,options,disk,{}, {},&profile);
                session.PauseAtIoConsole("12C 4 1\n");const auto paused=session.Run(std::chrono::seconds(15));
                Check(paused.paused && !paused.success,"Conformance did not pause at its own console marker");
                before=Clock(session);saved=session.Capture();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for(uint32_t field=0;field<4;++field) {
                auto damaged=saved;auto clock=before;
                if(field==0)clock.performanceFrequency=1;
                if(field==1)clock.utcEpoch100ns=1;
                if(field==2)clock.sequence|=1;
                if(field==3){clock.utcEpoch100ns=0;clock.performanceFrequency=0;}
                std::memcpy(damaged.machine.ram.data()+guardian::cpu::ClockGpa,&clock,sizeof(clock));
                bool rejected=false;
                try {KernelBootSession candidate;candidate.Prepare(image,ram,id,options,disk,{}, {},&profile);candidate.Restore(damaged);}
                catch(const std::exception&){rejected=true;}
                Check(rejected,"Malformed snapshot clock was accepted");
            }
            KernelBootSession restored;restored.Prepare(image,ram,id,options,disk,{}, {},&profile);restored.Restore(saved);
            Check(Clock(restored).utcEpoch100ns==before.utcEpoch100ns,"Restore changed UTC origin");
            const auto result=restored.Run(std::chrono::seconds(15));record(result,profile,ram,true);
            const auto after=Clock(restored);Check(after.utcEpoch100ns==before.utcEpoch100ns && after.monotonicNs>=before.monotonicNs,"Restored clocks diverged");
        }
        matrix<<"],\"status\":\"PASS\",\"caseCount\":"<<guardian::conformance::CaseCount<<",\"completeRuns\":"<<results.size()<<"}\n";
        Check(bool(matrix),"Conformance evidence write failed");
        std::cout<<results.size()<<" real guest runs, "<<guardian::conformance::CaseCount<<" common cases/run, three NT profiles, restore and invalid-clock rejection PASS\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
