#include "../src/microvm/boot/KernelBoot.h"
#include <iostream>
using namespace av::microvm;
namespace {
void Check(bool ok,const char* why){state::Require(ok,why);}
template<class F> void Refused(F f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}Check(refused,"Invalid profile/snapshot accepted");}
guardian::nt::Info Binding(const boot::KernelImage& kernel,std::span<const uint8_t> app,guardian::nt::Target t){
    guardian::nt::Info p;p.profile=t.id;p.major=t.major;p.minor=t.minor;p.build=t.build;
    const auto k=compatibility::Sha256(kernel.FileBytes());std::memcpy(p.kernelSha,k.data(),32);
    if(!app.empty()){const auto a=compatibility::Sha256(app);std::memcpy(p.applicationSha,a.data(),32);}
    const auto hash=compatibility::Sha256(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&t),sizeof(t)));
    std::memcpy(p.manifestSha,hash.data(),32);std::memcpy(p.runtimeSha,hash.data(),32);return p;
}
}
int main(int argc,char** argv){try{
    Check(argc==5,"GUI kernel, application, CPU kernel and output required");if(!boot::WhpAvailable())return 77;
    const auto guiKernel=boot::KernelImage::Read(argv[1]),cpuKernel=boot::KernelImage::Read(argv[3]);
    const auto app=state::ReadFile(argv[2]);const auto root=std::filesystem::absolute(argv[4])/state::Token();std::filesystem::create_directories(root);
    std::vector<boot::BootResult> rows;
    for(const auto& t:guardian::nt::Targets){
        auto binding=Binding(guiKernel,app,t);boot::CpuOptions cpu{};cpu.enabled=cpu.processes=cpu.graphics=true;
        gui::Options graphics;graphics.output=root/std::to_string(t.build);graphics.interactive=true;
        std::filesystem::create_directories(graphics.output);
        const guardian::graphics::Event events[]{{guardian::graphics::Input::Key,0,0,0,'G',0,0},
            {guardian::graphics::Input::Button,0,50,65,0,1,0},{guardian::graphics::Input::Close,0,0,0,0,0,0}};
        size_t at=0;bool observed=false;const auto expected="PE NT "+std::to_string(t.build);
        graphics.poll=[&]{return at<3?events[at++]:guardian::graphics::Event{};};
        graphics.display=[&](const auto& frame,const uint32_t*){if(frame.count==1&&std::string(frame.windows[0].title)==expected)observed=true;};
        boot::KernelBootSession session;session.Prepare(guiKernel,32ull<<20,60,cpu,{}, {},graphics,nullptr,{},nullptr,nullptr,nullptr,0,app,&binding);
        // Caller mutation cannot change a prepared session's profile.
        binding.profile=99;
        state::MachineState machine;Check(session.Machine().Capture(machine),"Capture failed");
        Check(machine.access[guardian::nt::InfoGpa/4096]==1,"NT page is not RO/NX");
        guardian::nt::Info guest;std::memcpy(&guest,machine.ram.data()+guardian::nt::InfoGpa,sizeof(guest));
        Check(guardian::nt::Valid(guest,60)&&guest.profile==t.id,"NT binding changed after preparation");
        const auto result=session.Run(std::chrono::seconds(15));rows.push_back(result);boot::WriteBootReport(root/"boots.json",rows);
        Check(result.success&&observed&&!result.memoryFault&&!result.timedOut,"Guest version query/GUI did not use selected profile");
    }
    boot::CpuOptions cpu;cpu.enabled=true;
    const auto selected=Binding(cpuKernel,{},guardian::nt::Find(1));
    boot::KernelBootSession source;source.Prepare(cpuKernel,16ull<<20,61,cpu,{}, {},{},nullptr,{},nullptr,nullptr,nullptr,0,{},&selected);
    const auto saved=source.Capture();
    {boot::KernelBootSession same;same.Prepare(cpuKernel,16ull<<20,61,cpu,{}, {},{},nullptr,{},nullptr,nullptr,nullptr,0,{},&selected);same.Restore(saved);const auto result=same.Run(std::chrono::seconds(10));Check(result.success,"Bound CPU snapshot did not resume");rows.push_back(result);}
    for(unsigned mode=0;mode<5;++mode){
        auto other=selected;
        if(mode==0)other=Binding(cpuKernel,{},guardian::nt::Find(2));
        if(mode==1)other.manifestSha[0]^=1;
        if(mode==2)other.runtimeSha[0]^=1;
        boot::KernelBootSession candidate;candidate.Prepare(cpuKernel,16ull<<20,61,cpu,{}, {},{},nullptr,{},nullptr,nullptr,nullptr,0,{},mode==3?nullptr:&other);
        auto copy=saved;if(mode==4)copy.machine.access[guardian::nt::InfoGpa/4096]=3;
        Refused([&]{candidate.Restore(copy);});
    }
    auto wrong=selected;wrong.kernelSha[0]^=1;Refused([&]{boot::KernelBootSession s;s.Prepare(cpuKernel,16ull<<20,62,cpu,{}, {},{},nullptr,{},nullptr,nullptr,nullptr,0,{},&wrong);});
    hardware::Profile hardware;hardware.nt="win7";hardware.identity="0123456789abcdef0123456789abcdef";
    Refused([&]{boot::KernelBootSession s;s.Prepare(cpuKernel,16ull<<20,62,cpu,{}, {},{},&hardware,{},nullptr,nullptr,nullptr,0,{},&selected);});
    Refused([&]{source.Prepare(cpuKernel,16ull<<20,61,cpu);});
    boot::WriteBootReport(root/"boots.json",rows);
    std::cout<<"Three real guest profiles/queries, RO/NX immutable binding, snapshot round-trip and five mismatch refusals PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
