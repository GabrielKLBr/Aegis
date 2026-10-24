#include "../src/microvm/boot/KernelBoot.h"
#include <algorithm>
#include <iostream>
using namespace av::microvm;
namespace {void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
int main(int argc,char** argv){try{
    Check(argc==4,"Kernel, external PE and evidence root required");
    if(!boot::WhpAvailable())return 77;
    const auto kernel=boot::KernelImage::Read(argv[1]);const auto app=state::ReadFile(argv[2],guardian::externalgui::MaxFile);
    Check(kernel.SupportsExternalGui(),"Missing external GUI capability");
    const auto root=std::filesystem::absolute(argv[3])/state::Token();std::filesystem::create_directories(root);
    const auto canary="private-host-control-"+state::Token();state::AtomicText(root/"host-control.txt",canary);
    Check(std::search(kernel.FileBytes().begin(),kernel.FileBytes().end(),app.begin(),app.end())==kernel.FileBytes().end(),"Application bytes are embedded in the kernel");
    boot::CpuOptions cpu{};cpu.enabled=cpu.processes=cpu.graphics=true;
    std::vector<boot::BootResult> rows;
    for(uint32_t mib:{16u,32u,64u}){
        const auto folder=root/std::to_string(mib);std::filesystem::create_directories(folder);
        bool firstPaint=false,keyPaint=false,clickPaint=false;
        gui::Options graphics;graphics.output=folder;
        const std::vector<guardian::graphics::Event> input={{guardian::graphics::Input::Key,0,0,0,'G',0,0},
                        {guardian::graphics::Input::Button,0,50,65,0,1,0},
                        {guardian::graphics::Input::Close,0,0,0,0,0,0}};
        size_t inputAt=0;graphics.interactive=true;
        graphics.poll=[&]{return inputAt<input.size()?input[inputAt++]:guardian::graphics::Event{};};
        graphics.display=[&](const auto& frame,const uint32_t* pixels){
            if(frame.count!=1)return;
            const auto k=pixels[75*320+102],c=pixels[100*320+102];
            firstPaint|=k==0xFFFFFFFFu&&c==0xFFFFFFFFu;
            keyPaint|=k==0xFF0078D7u&&c==0xFFFFFFFFu;
            clickPaint|=k==0xFF0078D7u&&c==0xFF0078D7u;
        };
        boot::KernelBootSession session;
        session.Prepare(kernel,uint64_t(mib)<<20,41,cpu,{}, {},graphics,nullptr,{},nullptr,nullptr,nullptr,0,app);
        bool snapshotRefused=false;try{session.Capture();}catch(const std::exception&){snapshotRefused=true;}
        Check(snapshotRefused,"Unsupported external GUI snapshot accepted");
        state::MachineState machine;Check(session.Machine().Capture(machine),"Cannot inspect prepared VM");
        Check(machine.access[guardian::externalgui::InfoGpa/4096]==1&&machine.access[guardian::externalgui::SourceGpa/4096]==1,"Payload source is not read-only/NX");
        const auto begin=machine.ram.begin()+guardian::externalgui::SourceGpa;
        Check(std::equal(app.begin(),app.end(),begin),"PE bytes were modified before loading");
        Check(std::search(machine.ram.begin(),machine.ram.end(),canary.begin(),canary.end())==machine.ram.end(),"Private host control found in guest RAM");
        const auto row=session.Run(std::chrono::seconds(20));rows.push_back(row);boot::WriteBootReport(root/"boot.json",rows);
        if(!row.success)throw std::runtime_error("External PE GUI boot: "+row.error+" console="+row.console);
        Check(firstPaint&&keyPaint&&clickPaint,"Independent framebuffer probes did not observe paint/key/click transitions");
        Check(row.graphicsInputs==3&&row.graphicsFrames>=5&&row.graphics.applications==1,"Missing external GUI activity");
        Check(!row.cpu.fatal&&!row.memoryFault&&!row.timedOut&&!row.processes.liveProcesses&&!row.processes.liveHandles,"External GUI leaked or faulted");
    }
    for(uint32_t mode=0;mode<3;++mode){
        auto bytes=app;if(mode==1)bytes[0]=0;else if(!mode)bytes[0x3C]=0xFF,bytes[0x3D]=0xFF,bytes[0x3E]=0xFF,bytes[0x3F]=0x7F;
        const auto folder=root/("bad-"+std::to_string(mode));std::filesystem::create_directories(folder);gui::Options graphics;graphics.output=folder;
        boot::KernelBootSession session;
        session.Prepare(kernel,16ull<<20,42,cpu,{}, {},graphics,nullptr,{},nullptr,nullptr,nullptr,0,bytes);
        if(mode==2)static_cast<uint8_t*>(session.Machine().RamData())[guardian::externalgui::SourceGpa+64]^=1;
        const auto row=session.Run(std::chrono::seconds(15));rows.push_back(row);boot::WriteBootReport(root/"boot.json",rows);
        Check(!row.success&&!row.timedOut&&!row.memoryFault&&row.graphicsFrames==0,"Malformed PE was executed or escaped source validation");
    }
    const auto control=state::ReadFile(root/"host-control.txt");Check(std::string(control.begin(),control.end())==canary,"Host control file changed");
    std::cout<<"External USER32 PE: three RAM profiles, real paint/key/click/close, read-only payload, pointer/bridge probes, snapshot refusal, malformed PE/tamper refusals and host control PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
