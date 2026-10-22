#include "../microvm/boot/KernelBoot.h"
#include <iostream>
#include <string_view>
using namespace av::microvm::boot;
int GuardianKvmCli(int argc,char** argv){
    try {
        if(argc==2 && std::string_view(argv[1])=="--probe"){
            const bool ok=av::microvm::MicroVm::Available();
            std::cout<<"{\"backend\":\"KVM\",\"available\":"<<(ok?"true":"false")<<",\"wine\":false}\n";
            return ok?0:3;
        }
        if(argc<4){std::cerr<<"Uso: guardian-kvm <kernel.gk> <basic|cpu|memory|threads|processes|io|storage|network> <relatorio.json> [--ram-mib 16..64] [--timeout-ms 1..30000] [--boot-id N] [--disk base overlay] [--create-disk] [--network offline|simulated] [--capture DIRECTORY]\n";return 2;}
        const auto image=KernelImage::Read(argv[1]);const std::string mode=argv[2];
        av::microvm::net::Options networking;CpuOptions cpu{};uint64_t ram=32ull<<20,id=1;int timeout=5000;StorageOptions storage{};
        if(mode!="basic")cpu.enabled=true;
        if(mode=="network")cpu.network=true;
        else if(mode=="memory")cpu.memory=true;
        else if(mode=="threads")cpu.threads=true;
        else if(mode=="processes")cpu.processes=true;
        else if(mode=="io"){cpu.processes=true;cpu.io=true;}
        else if(mode=="storage"){cpu.processes=true;cpu.io=true;cpu.storage=true;}
        else if(mode!="basic" && mode!="cpu")throw std::invalid_argument("Unknown boot contract");
        const auto integer=[](const char* p){size_t used=0;const std::string_view input=p;if(input.empty() || input.find_first_not_of("0123456789")!=input.npos)throw std::invalid_argument("Invalid integer");const auto n=std::stoull(p,&used);if(used!=std::string_view(p).size())throw std::invalid_argument("Invalid integer");return n;};
        for(int i=4;i<argc;++i){const std::string_view a=argv[i];
            if(a=="--network" && i+1<argc){const std::string value=argv[++i];if(value=="offline")networking.policy.mode=guardian::net::Mode::Offline;else if(value=="simulated")networking.policy.mode=guardian::net::Mode::Simulated;else throw std::invalid_argument("Use guardian-vm for lab/proxy policies");continue;}
            if(a=="--capture" && i+1<argc){networking.capture=std::filesystem::absolute(argv[++i]);std::filesystem::create_directories(networking.capture);continue;}
            if(a=="--create-disk"){storage.createIfMissing=true;continue;}
            if(a=="--disk" && i+2<argc){storage.base=std::filesystem::absolute(argv[++i]);storage.overlay=std::filesystem::absolute(argv[++i]);continue;}
            if(i+1>=argc)throw std::invalid_argument("Missing option argument");
            const auto n=integer(argv[++i]);
            if(a=="--ram-mib"){if(n<16 || n>64)throw std::invalid_argument("RAM outside bounds");ram=n<<20;}
            else if(a=="--timeout-ms"){if(!n || n>30000)throw std::invalid_argument("Timeout outside bounds");timeout=static_cast<int>(n);}
            else if(a=="--boot-id"){if(!n)throw std::invalid_argument("Boot ID must be nonzero");id=n;}
            else throw std::invalid_argument("Unknown option");
        }
        KernelBootSession session;session.Prepare(image,ram,id,cpu,storage,networking);
        const auto result=session.Run(std::chrono::milliseconds(timeout));WriteBootReport(argv[3],{result});
        std::cout<<result.console;
        if(!result.ioConsole.empty())std::cout<<result.ioConsole;
        std::cout<<"KVM: "<<(result.success?"PASS":"FAIL")<<" | "<<result.diagnostic<<" | "<<result.error<<'\n';
        return result.success?0:3;
    }catch(const std::exception& e){std::cerr<<"Falha: "<<e.what()<<'\n';return 2;}
}
