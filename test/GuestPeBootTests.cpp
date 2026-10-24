// Executes only the owned kernel and its embedded, compiler-produced PE inputs.
#include "../src/microvm/boot/KernelBoot.h"
#include <iostream>
#include <stdexcept>
using namespace av::microvm::boot;
namespace {void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
int main(int argc,char** argv){try{
    Check(argc==3,"Loader kernel and report required");const auto image=KernelImage::Read(argv[1]);const bool complete=std::filesystem::path(argv[1]).filename()=="guardian-kernel-complete.gk";const bool lifetime=complete || std::filesystem::path(argv[1]).filename()=="guardian-kernel-lifetime.gk";const bool runtime=lifetime || std::filesystem::path(argv[1]).filename()=="guardian-kernel-runtime.gk";
    if(!WhpAvailable()){std::cout<<"WHP unavailable\n";return 77;}
    const auto report=std::filesystem::absolute(argv[2]);
    const auto directory=report.parent_path()/(report.stem().string()+"-volumes")/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(directory);
    std::vector<BootResult> rows;uint64_t id=1;
    auto bootTimeout=complete?std::chrono::seconds(10):std::chrono::seconds(3);
#ifdef _WIN32
    // Functional probes include synchronous host disk flushes. Windows runs on
    // 2026-10-24 observed 12.7-15.7 s stalls at different I/O stages, while the
    // same inputs completed on retry. Keep bounded headroom for persistence;
    // dedicated CPU/clock/watchdog probes retain their independent deadlines.
    bootTimeout=std::chrono::seconds(30);
#endif
    for(uint64_t ram:{16ull<<20,64ull<<20})for(uint32_t run=0;run<20;++run){
        StorageOptions disk{directory/"base.gvd",directory/(std::to_string(runtime?(id-1)/2:id)+".gvo"),true};
        KernelBootSession session;session.Prepare(image,ram,id++,{true,true,false,false,true,true,true},disk);auto r=session.Run(bootTimeout);rows.push_back(r);
        if(!r.success){WriteBootReport(report,rows);throw std::runtime_error("Guest PE mapping: "+r.error);}
        Check(r.console.find("Guardian kernel 0.8.0 PE MAPPING\n")!=std::string::npos && r.console.find("PE loader mapping checks PASS\n")!=std::string::npos,"Guest mapping checks missing");
        Check(r.console.find("PE console TLS DLL execution PASS\n")!=std::string::npos,"Guest PE console/TLS/DLL execution missing");
        if(runtime)Check(r.console.find("PE runtime services heap locks PASS\n")!=std::string::npos && r.ioConsole=="PE RUNTIME OK\nIO OK\n" && r.io.consoleBytes==20 && r.storage.registryChanges==3,"PE runtime API behavior missing");
        if(runtime)Check(r.console.find("PE private TEB TLS thread lifecycle contention PASS\n")!=std::string::npos,"PE thread lifecycle missing");
        if(lifetime)Check(r.console.find("PE child processes explicit exits last-thread lifetime PASS\n")!=std::string::npos,"PE child/lifetime behavior missing");
        if(complete)Check(r.console.find("PE x64 exception dispatch recovery nonlocal unwind PASS\n")!=std::string::npos && r.console.find("PE dynamic DLL TLS references rollback unload PASS\n")!=std::string::npos,"Phase 8 final behavior missing");
        if(lifetime)Check(r.console.find("PE x64 virtual stack capture PASS\n")!=std::string::npos,"PE stack capture missing");
        Check(r.processes.coreChecks==693 && r.processes.completed==2 && !r.processes.liveReferences && r.processes.freeBefore==r.processes.freeAfter+21,"PE mappings damaged process lifecycle");
        const auto before=runtime?uint64_t(run%2)*3:0ull;
        Check(r.storageReportValid && r.storage.coreChecks==784 && r.storage.sequenceBefore==before && r.storage.sequenceAfter==before+(runtime?3u:2u) && !r.storage.liveHandles && !r.storage.liveWatches,"PE mapping broke persistent storage");
        Check(!r.cpu.fatal && !r.memoryFault && !r.timedOut,"PE mapping faulted or hung");
    }
    WriteBootReport(report,rows);std::cout<<(runtime?"40 runtime API boots (20 persistence pairs), heap/locks, console/file/registry PASS\n":"40 guest PE boots, 26 mapping checks/boot, console/TLS/DLL execution and process/I/O/storage regressions PASS\n");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
