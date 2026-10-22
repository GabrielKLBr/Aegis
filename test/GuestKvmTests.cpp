#include "../src/microvm/boot/KernelBoot.h"
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace av::microvm::boot;
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value)throw std::runtime_error("KVM check "+std::to_string(checks));}
template<class F>void Reject(F fn){bool bad=false;try{fn();}catch(const std::runtime_error&){bad=true;}Check(bad);}
}
int main(int argc,char** argv){try{
    Check(argc==3 && av::microvm::MicroVm::Available());
    const auto basic=KernelImage::Read(argv[1]),cpu=KernelImage::Read(argv[2]);
    {KernelBootSession s;Reject([&]{s.Run();});}
    for(const auto ram:{uint64_t(0),uint64_t(4096),uint64_t(16*1024*1024+1),uint64_t((1ull<<30)+4096)}){
        KernelBootSession s;Reject([&]{s.Prepare(basic,ram,1);});
    }
    for(const auto ram:{uint64_t(16<<20),uint64_t(32<<20),uint64_t(64<<20)}){
        KernelBootSession s;s.Prepare(basic,ram,0x1234);
        Reject([&]{s.Prepare(basic,ram,0x1234);});
        const auto r=s.Run();Check(r.success && !r.timedOut && r.cpl==0 && r.stopStatus==0);
        Reject([&]{s.Run();});
    }
    for(unsigned kind=0;kind<12;++kind){
        KernelBootSession s;s.Prepare(basic,32<<20,0x3456);
        auto* ram=static_cast<uint8_t*>(s.Machine().RamData());guardian::abi::BootInfo b{};
        std::memcpy(&b,ram+BootGpa,sizeof(b));
        switch(kind){
        case 0:b.magic=0;break;
        case 1:b.major=0;break;
        case 2:b.bytes=0;break;
        case 3:b.ramBytes=0;break;
        case 4:b.memoryMapGpa=UINT64_MAX;break;
        case 5:b.memoryMapCount=UINT32_MAX;break;
        case 6:b.devicesGpa=UINT64_MAX;break;
        case 7:b.deviceCount=UINT32_MAX;break;
        case 8:b.messageGpa=UINT64_MAX;break;
        case 9:b.bootId++;break; // Valid guest envelope, wrong host session.
        case 10:b.reserved[0]=1;break;
        case 11:b.stackBytes=0;break;
        }
        std::memcpy(ram+BootGpa,&b,sizeof(b));const auto r=s.Run(std::chrono::milliseconds(20));
        Check(!r.success && !r.error.empty());
    }
    {KernelBootSession s;CpuOptions options{};options.enabled=true;s.Prepare(cpu,32<<20,99,options);
        const auto r=s.Run();Check(r.success && r.cpu.checks==guardian::cpu::RequiredChecks && r.timerQueued==r.timerAcknowledged);}
    {KernelBootSession s;CpuOptions options{};options.enabled=true;options.driveTimer=false;s.Prepare(cpu,32<<20,100,options);
        const auto r=s.Run(std::chrono::milliseconds(80));Check(!r.success && r.timedOut);}
    {KernelBootSession s;s.Prepare(basic,32<<20,1);
        s.Machine().MapIoTrapDetailed(guardian::abi::DebugPort,[](auto& vm,const auto&){vm.FailExecution(L"injected device refusal");});
        const auto r=s.Run();Check(!r.success && r.error=="injected device refusal");}
    {KernelBootSession s;s.Prepare(basic,32<<20,1);Check(!s.Machine().Run(1));}
    {KernelBootSession s;s.Prepare(basic,32<<20,1);Check(!s.Machine().SetMemoryAccess(UINT64_MAX,4096,3));}
    {KernelBootSession s;s.Prepare(basic,32<<20,1);Check(!s.Machine().SetMemoryAccess(0,4096,7));}
    std::cout<<checks<<" KVM real-guest boot, deadline and refusal checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
