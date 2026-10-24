#include "../boot/KernelBoot.h"
#include "Viewer.h"
#include <iostream>
int main(int argc,char** argv){
    using namespace av::microvm;
    try{
        if(argc<4){std::cerr<<"Uso: guardian-gui-pe KERNEL APP.exe PASTA_NOVA [--interactive | --script ARQUIVO]\n";return 2;}
        gui::Options graphics;std::filesystem::path script,binding,manifest;
        for(int i=4;i<argc;++i){const std::string option=argv[i];if(option=="--interactive")graphics.interactive=true;
            else if(option=="--script"&&i+1<argc)script=argv[++i];
            else if(option=="--nt-binding"&&i+1<argc)binding=argv[++i];
            else if(option=="--nt-manifest"&&i+1<argc)manifest=argv[++i];else throw std::runtime_error("Unsupported GUI option");}
        state::Require(binding.empty()==manifest.empty(),"NT binding and manifest must be supplied together");
        guardian::nt::Info nt;
        if(!binding.empty())nt=compatibility::Read(binding,manifest);
        state::Require(!graphics.interactive||script.empty(),"Select manual input or a guest script");
        const auto image=boot::KernelImage::Read(argv[1]);
        state::Require(image.SupportsExternalGui(),"Kernel does not implement the external GUI contract");
        const auto input=state::ReadFile(argv[2],guardian::externalgui::MaxFile);
        if(!binding.empty())compatibility::Verify(nt,image.FileBytes(),input);
        graphics.output=std::filesystem::absolute(argv[3]);
        state::Require(!std::filesystem::exists(graphics.output),"Select a new output folder");
        std::filesystem::create_directories(graphics.output);
        state::Lease lease(graphics.output/"session.lock");
        if(!binding.empty()){
            const auto manifestBytes=state::ReadFile(manifest,4ull<<20);
            const auto manifestHash=compatibility::Sha256(manifestBytes);
            state::Require(std::memcmp(manifestHash.data(),nt.manifestSha,32)==0,"NT manifest changed before session creation");
            state::AtomicWrite(graphics.output/"nt-manifest.json",manifestBytes);
            state::AtomicWrite(graphics.output/"nt-binding.gnt",{reinterpret_cast<const uint8_t*>(&nt),sizeof(nt)});
            state::AtomicText(graphics.output/"nt-profile.json",compatibility::Json(nt));
        }
        graphics.continuous=true;
        if(!script.empty())graphics.script=gui::ReadScript(script);
        std::unique_ptr<gui::Viewer> viewer;
        if(graphics.interactive){viewer=std::make_unique<gui::Viewer>();graphics.poll=[&]{return viewer->Poll();};graphics.display=[&](const auto& frame,const uint32_t* pixels){viewer->Display(frame,pixels);};}
        boot::CpuOptions cpu{};cpu.enabled=cpu.processes=cpu.graphics=true;
        boot::KernelBootSession session;
        session.Prepare(image,64ull<<20,1,cpu,{}, {},graphics,nullptr,{},nullptr,nullptr,nullptr,0,input,binding.empty()?nullptr:&nt);
        const auto result=session.Run(std::chrono::seconds(30));
        boot::WriteBootReport(graphics.output/"boot.json",{result});
        std::cout<<result.console;
        if(!result.success){std::cerr<<result.error<<'\n';return 3;}
        std::cout<<"External PE executed in guest CPL3; GUI displayed through the bounded viewer\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
