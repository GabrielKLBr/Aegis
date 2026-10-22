#include "../boot/KernelBoot.h"
#include "Viewer.h"
#include <iostream>
#include <sstream>
int main(int argc, char **argv) {
    using namespace av::microvm;
    try {
        if (argc == 2 && std::string(argv[1]) == "--viewer-test") {
            gui::Viewer viewer; viewer.SmokeTest();
            std::cout << "Native software viewer own-window event filter PASS\n"; return 0;
        }
        if (argc < 3) {
            std::cerr << "Uso: guardian-gui kernel-graphics.gk OUTPUT [--interactive | --script FILE] [--duration-ms N] [--checkpoint-ms N] [--resume] [--time-mode real]\n";
            return 2;
        }
        gui::Options options;
        options.output = std::filesystem::absolute(argv[2]);
        bool resume=false, sessionMode=false;
        uint32_t duration=30000, checkpoint=1000;
        std::filesystem::path script;
        auto number=[](const std::string& value){
            state::Require(!value.empty() && value.find_first_not_of("0123456789")==value.npos,"Invalid session number");
            const auto n=std::stoull(value);state::Require(n<=UINT32_MAX,"Session number outside bounds");return uint32_t(n);
        };
        for(int i=3;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--interactive") {options.interactive=true;sessionMode=true;continue;}
            if(option=="--resume") {resume=true;sessionMode=true;continue;}
            state::Require(i+1<argc,"Missing graphics session option");const std::string value=argv[++i];
            if(option=="--script") {script=value;sessionMode=true;}
            else if(option=="--duration-ms") {duration=number(value);sessionMode=true;}
            else if(option=="--checkpoint-ms") {checkpoint=number(value);sessionMode=true;}
            else if(option=="--time-mode") state::Require(value=="real","Only real guest time is implemented; virtual acceleration is unavailable");
            else throw std::runtime_error("Unknown graphics option");
        }
        state::Require(!options.interactive || script.empty(),"Select manual input or a guest script");
        state::Require(guardian::session::ValidDuration(duration,checkpoint),"Session duration/checkpoint bounds rejected");
        std::filesystem::create_directories(options.output);
        // One native viewer/runner owns this directory's checkpoint at a time.
        state::Lease lease(options.output/"session.lock");
        options.continuous=sessionMode;
        if(!script.empty()) options.script=gui::ReadScript(script);
        std::unique_ptr<gui::Viewer> viewer;
        if(options.interactive) {
            viewer=std::make_unique<gui::Viewer>();
            options.poll=[&]{return viewer->Poll();};
            options.display=[&](const auto& frame,const uint32_t* pixels){viewer->Display(frame,pixels);};
        } else if(!sessionMode) options.input=gui::ReferenceInput();
        const auto image=boot::KernelImage::Read(argv[1]);
        boot::CpuOptions cpu{};cpu.enabled=true;cpu.graphics=true;
        boot::SessionSnapshot point;
        if(resume) point=boot::SessionSnapshot::Decode(state::ReadFile(options.output/"session.gvs",96ull<<20));
        boot::KernelBootSession session;
        session.Prepare(image,32ull<<20,resume?point.BootId():11,cpu,{},{},options);
        if(resume) session.Restore(point);
        uint64_t activeMs=0, activeNs=0, checkpoints=0;
        const auto start=std::chrono::steady_clock::now();
        boot::BootResult result;
        do {
            const auto beforeNs=session.ElapsedNs();
            const auto slice=sessionMode?std::min<uint64_t>(checkpoint,duration>activeMs?duration-activeMs:1):10000;
            result=session.Run(std::chrono::milliseconds(slice),sessionMode);
            activeNs+=session.ElapsedNs()-beforeNs;activeMs=activeNs/1000000;
            if(result.paused) {
                state::AtomicWrite(options.output/"session.gvs",session.Capture().Encode()); ++checkpoints;
            }
        } while(sessionMode && result.paused && activeMs<duration);
        if(!result.paused && std::filesystem::exists(options.output/"session.gvs")) std::filesystem::remove(options.output/"session.gvs");
        boot::WriteBootReport(options.output/"boot.json",{result});
        std::ostringstream audit;audit << std::boolalpha;
        audit << "{\"phase\":\"12D\",\"timeMode\":\"real\",\"offlineTime\":\"frozen\",\"resumed\":" << resume
              << ",\"activeMs\":" << activeMs << ",\"wallMs\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()
              << ",\"checkpoints\":" << checkpoints << ",\"paused\":" << result.paused << ",\"completed\":" << result.success << "}\n";
        state::AtomicText(options.output/"session.json",audit.str());
        std::cout << result.console << result.diagnostic << "\n" << result.error << "\n";
        return result.success || result.paused ? 0 : 3;
    } catch(const std::exception& e) { std::cerr << "Falha: " << e.what() << '\n'; return 2; }
}
