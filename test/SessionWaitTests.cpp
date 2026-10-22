#include "../src/microvm/boot/KernelBoot.h"
#include "../src/microvm/state/Files.h"
#include <iostream>
#include <thread>
using namespace av::microvm;
int main(int argc,char** argv) {
    try {
        state::Require(argc==3,"Session kernel and output required");
        const auto image=boot::KernelImage::Read(argv[1]);const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
        const auto root=output/("run-"+state::Token());std::filesystem::create_directory(root);
        boot::CpuOptions cpu{};cpu.enabled=true;cpu.processes=true;cpu.io=true;cpu.storage=true;
        state::AtomicWrite(root/"base.gvd",std::vector<uint8_t>(65536));
        uint32_t checks=0;const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Wait check "+std::to_string(checks));};
        std::vector<boot::BootResult> results;
        boot::SessionSnapshot pending;std::vector<uint8_t> rebootDisk;
        {
            boot::KernelBootSession first;first.Prepare(image,32ull<<20,121,cpu,{root/"base.gvd",root/"first.gvo",true});
            first.PauseAtIoConsole("12D WAIT 0\n");auto result=first.Run(std::chrono::seconds(10));results.push_back(result);
            check(result.paused && result.ioConsole.find("12D WAIT 0")!=std::string::npos);
            rebootDisk=first.DiskSnapshot();
            result=first.Run(std::chrono::milliseconds(200),true);results.push_back(result);
            check(result.paused && !result.timedOut && result.ioConsole.find("12D DONE")==std::string::npos);
            pending=first.Capture();state::AtomicWrite(root/"pending.gvs",pending.Encode());
        }
        // Offline host time must not complete the guest's pending Sleep.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        pending=boot::SessionSnapshot::Decode(state::ReadFile(root/"pending.gvs"));
        state::AtomicWrite(root/"resume.gvo",pending.disk);
        double resumedMs=0;
        {
            boot::KernelBootSession resumed;resumed.Prepare(image,32ull<<20,121,cpu,{root/"base.gvd",root/"resume.gvo",true});resumed.Restore(pending);
            const auto start=std::chrono::steady_clock::now();const auto result=resumed.Run(std::chrono::seconds(10));
            resumedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();results.push_back(result);
            check(result.success && result.ioConsole.find("12D DONE 2")!=std::string::npos);
            check(resumedMs>=650 && resumed.ElapsedNs()>=1000000000);
        }
        state::AtomicWrite(root/"cold.gvo",rebootDisk);
        {
            boot::KernelBootSession cold;cold.Prepare(image,32ull<<20,122,cpu,{root/"base.gvd",root/"cold.gvo",true});const auto result=cold.Run(std::chrono::seconds(10));results.push_back(result);
            check(result.success && result.ioConsole.find("12D RECOVER 1")!=std::string::npos && result.ioConsole.find("12D DONE 3")!=std::string::npos);
        }
        boot::WriteBootReport(output/"boots.json",results);
        state::AtomicText(output/"wait.json","{\"status\":\"PASS\",\"checks\":"+std::to_string(checks)+",\"sleepMs\":1000,\"offlineMs\":250,\"resumedWallMs\":"+std::to_string(resumedMs)+",\"coldRebootProgressRecovered\":true}\n");
        std::cout<<checks<<" actual CPL3 Sleep, durable worker restore, offline freeze and application progress after cold reboot checks PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
