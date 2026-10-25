#include "../boot/KernelBoot.h"
#include <iostream>
int main(int argc,char** argv){
    using namespace av::microvm;
    try{
        state::Require(argc==6||argc==7,"Usage: guardian-pe KERNEL VOLUME MANIFEST BINDING NEW_OUTPUT [SECURITY_STATE]");
        const auto kernel=boot::KernelImage::Read(argv[1]);state::Require(kernel.SupportsExternalPe(),"External volume kernel required");
        const auto securityState=argc==7?state::ReadFile(argv[6],65536):std::vector<uint8_t>{};
        state::Require(argc!=7||(kernel.SupportsSecurity()&&securityState.size()==65536),"Security state input rejected");
        const auto volume=state::ReadFile(argv[2],guardian::pevolume::MaxBytes);const auto binding=compatibility::Read(argv[4],argv[3]);
        state::Require(guardian::pevolume::Valid(volume.data(),static_cast<uint32_t>(volume.size())),"Volume rejected");
        const auto& h=*reinterpret_cast<const guardian::pevolume::Header*>(volume.data());const auto& f=h.entries[h.root];
        compatibility::Verify(binding,kernel.FileBytes(),std::span<const uint8_t>(volume).subspan(f.offset,f.bytes));
        const auto digest=compatibility::Sha256(volume);state::Require(std::memcmp(digest.data(),binding.runtimeSha,32)==0&&binding.profile==h.profile,"Volume binding mismatch");
        const auto output=std::filesystem::absolute(argv[5]);state::Require(!std::filesystem::exists(output),"Use a new output directory");
        std::filesystem::create_directories(output);state::Lease lease(output/"session.lock");
        const auto manifest=state::ReadFile(argv[3],4ull<<20);const auto manifestHash=compatibility::Sha256(manifest);
        state::Require(std::memcmp(manifestHash.data(),binding.manifestSha,32)==0,"Manifest changed");state::AtomicWrite(output/"nt-manifest.json",manifest);
        gui::Options graphics;graphics.output=output;
        boot::CpuOptions cpu;cpu.enabled=cpu.processes=cpu.graphics=true;
        boot::KernelBootSession session;session.Prepare(kernel,256ull<<20,1,cpu,{}, {},graphics,nullptr,{},nullptr,nullptr,nullptr,0,{},&binding,volume,securityState);
        const auto result=session.Run(std::chrono::seconds(30));boot::WriteBootReport(output/"boot.json",{result});
        if(const auto* trace=session.Analysis()){state::AtomicText(output/"analysis.json",trace->Json());state::AtomicText(output/"analysis.html",trace->Html());}
        std::cout<<result.console;if(!result.success){std::cerr<<result.error<<'\n';return 3;}
        if(kernel.SupportsSecurity()){const auto* data=static_cast<const uint8_t*>(session.Machine().RamData());state::AtomicWrite(output/"security-state.bin",std::span<const uint8_t>(data+0x5000000,65536));}
        std::cout<<"External volume executed inside MiniNT\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
