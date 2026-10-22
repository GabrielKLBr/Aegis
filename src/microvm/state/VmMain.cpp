#include "VmManager.h"
#include <iostream>
#include <string_view>
#ifdef _WIN32
#include <Windows.h>
#endif
using namespace av::microvm::state;
namespace hardware=av::microvm::hardware;
namespace environment=av::microvm::environment;
namespace {
std::filesystem::path Utf8(const std::string &text) {
    std::u8string out;
    for (unsigned char c : text)
        out += static_cast<char8_t>(c);
    return std::filesystem::path(out);
}
uint32_t Number(const std::string &value) {
    Require(!value.empty() && value.find_first_not_of("0123456789") == value.npos, "Invalid resource number");
    const auto n = std::stoull(value);
    Require(n <= UINT32_MAX, "Resource number outside bounds");
    return static_cast<uint32_t>(n);
}
int Execute(const std::vector<std::string> &args) {
    try {
        if (args.size() == 5 && args[1] == "--worker") {
            VmManager manager(Utf8(args[2]), true);
            return manager.Worker(args[3], args[4]);
        }
        Require(args.size() >= 4 && args[1] == "--root",
                "Usage: guardian-vm --root <catalog> "
                "<list|create|start|pause|resume|snapshot|restore|restart|shutdown|clone|configure|delete|"
                "status|analysis|analysis-policy|analysis-export|environment|create-environment|resources|resource-config|storage-config|disk-config|disk-folder|session-config|suspend|hardware|hardware-config|drivers|driver-import|driver-install> [arguments]");
        VmManager manager(Utf8(args[2]));
        const auto &operation = args[3];
#ifdef _WIN32
        wchar_t module[32768]{};
        const auto n = GetModuleFileNameW(nullptr, module, 32768);
        Require(n && n < 32768, "Cannot locate native worker executable");
        const auto executable = std::filesystem::path(module);
#else
        const auto executable = std::filesystem::canonical("/proc/self/exe");
#endif
        if (operation == "list") {
            Require(args.size() == 4, "Unexpected list argument");
            std::cout << manager.List() << '\n';
            return 0;
        }
        Require(args.size() >= 5, "VM ID required");
        const auto &id = args[4];
        if(operation=="analysis") {Require(args.size()==5,"Unexpected analysis argument");std::cout<<manager.AnalysisReport(id);return 0;
        } else if(operation=="analysis-export") {Require(args.size()==6,"analysis-export requires an output directory");const auto out=Utf8(args[5]);std::filesystem::create_directories(out);Require(std::filesystem::is_directory(out)&&!std::filesystem::is_symlink(out),"Analysis export directory rejected");AtomicText(out/"analysis.json",manager.AnalysisReport(id));AtomicText(out/"analysis.html",manager.AnalysisReport(id,true));std::cout<<"{\"exported\":true}\n";return 0;
        } else if(operation=="analysis-policy") {
            if(args.size()==5){std::cout<<manager.AnalysisPolicy(id)<<'\n';return 0;}
            Require(args.size()>=7,"analysis-policy requires domain and allow|deny|simulate");uint32_t limit=0,scenario=UINT32_MAX;
            for(size_t i=7;i<args.size();++i){Require(i+1<args.size(),"Missing analysis option");const auto option=args[i];const auto value=args[++i];if(option=="--event-limit")limit=Number(value);else if(option=="--scenario"){Require(value=="patterns"||value=="benign","Unknown owned scenario");scenario=value=="benign"?1:0;}else throw std::runtime_error("Unknown analysis option");}
            manager.ConfigureAnalysis(id,args[5],args[6],limit,scenario);std::cout<<manager.Status(id)<<'\n';return 0;
        } else if(operation=="environment") {Require(args.size()==5,"Unexpected environment argument");std::cout<<manager.Environment(id)<<'\n';return 0;
        } else if(operation=="create-environment") {
            Require(args.size()>=6,"create-environment requires a supported kernel image");
            VmConfig c;c.id=id;c.contract="storage";uint64_t seed=0;std::string nt="win11";bool preserve=true;
            for(size_t i=6;i<args.size();++i){const auto option=args[i];if(option=="--disposable"){preserve=false;continue;}if(option=="--persistent"){preserve=true;continue;}
                Require(i+1<args.size(),"Missing synthetic profile option");const auto value=args[++i];
                if(option=="--seed")seed=Number(value);else if(option=="--nt")nt=value;else if(option=="--ram-mib")c.ramMiB=Number(value);
                else if(option=="--host-reserve-mib")c.hostReserveMiB=Number(value);else throw std::runtime_error("Unknown synthetic profile option");}
            c.environment=environment::Make(seed,nt,preserve);manager.Create(c,Utf8(args[5]));std::cout<<manager.Status(id)<<'\n';return 0;
        } else if(operation=="resources") {
            Require(args.size()==5,"Unexpected resources argument");std::cout<<manager.Resources(id)<<'\n';return 0;
        } else if(operation=="resource-config") {
            uint32_t ram=32,vcpus=1,reserve=UINT32_MAX;bool maximum=false,selected=false;
            Require(args.size()>5,"resource-config needs --ram-mib N or --maximum");
            for(size_t i=5;i<args.size();++i){const auto& option=args[i];if(option=="--maximum"){maximum=true;selected=true;continue;}Require(i+1<args.size(),"Missing resource argument");const auto& value=args[++i];if(option=="--ram-mib"){ram=Number(value);selected=true;}else if(option=="--vcpus")vcpus=Number(value);else if(option=="--host-reserve-mib"){reserve=Number(value);Require(reserve!=UINT32_MAX,"Host reserve outside bounds");}else throw std::runtime_error("Unknown resource option");}
            Require(selected,"resource-config requires --ram-mib N or --maximum");
            manager.ConfigureResources(id,ram,vcpus,maximum,reserve);std::cout<<manager.Resources(id)<<'\n';return 0;
        } else if(operation=="session-config") {
            uint32_t duration=0,checkpoint=1000;std::filesystem::path script;
            for(size_t i=5;i<args.size();++i){
                const auto option=args[i];Require(i+1<args.size(),"Missing session option");const auto value=args[++i];
                if(option=="--duration-ms") duration=Number(value);
                else if(option=="--checkpoint-ms") checkpoint=Number(value);
                else if(option=="--script") script=Utf8(value);
                else if(option=="--time-mode") Require(value=="real","Only real guest time is implemented; virtual acceleration is unavailable");
                else throw std::runtime_error("Unknown session option");
            }
            manager.ConfigureSession(id,duration,checkpoint,script);std::cout<<manager.Status(id)<<'\n';return 0;
        } else if(operation=="disk-config") {
            Require((args.size()==9||(args.size()==11&&args[9]=="--storage-limit-mib"))&&args[5]=="--volume-quota-mib"&&args[7]=="--disk-growth-mib","disk-config requires --volume-quota-mib N --disk-growth-mib N");
            manager.ConfigureScalableStorage(id,uint64_t(Number(args[6]))*(1ull<<20),uint64_t(Number(args[8]))*(1ull<<20),args.size()==11?uint64_t(Number(args[10]))*(1ull<<20):0);std::cout<<manager.Resources(id)<<'\n';return 0;
        } else if(operation=="disk-folder") {
            Require(args.size()==6,"disk-folder requires a parent folder");manager.ConfigureDiskFolder(id,Utf8(args[5]));std::cout<<manager.Resources(id)<<'\n';return 0;
        } else if(operation=="storage-config") {
            Require((args.size()==9||(args.size()==11&&args[9]=="--storage-limit-mib"))&&args[5]=="--volume-quota-kib"&&args[7]=="--overlay-limit-kib","storage-config requires --volume-quota-kib N --overlay-limit-kib N");
            manager.ConfigureStorage(id,uint64_t(Number(args[6]))*1024,uint64_t(Number(args[8]))*1024,args.size()==11?uint64_t(Number(args[10]))*(1ull<<20):0);std::cout<<manager.Resources(id)<<'\n';return 0;
        } else if(operation=="hardware") {
            Require(args.size()==5,"Unexpected hardware argument");std::cout<<manager.Hardware(id)<<'\n';return 0;
        } else if(operation=="hardware-config") {
            Require(args.size()>=6,"hardware-config requires --host-reference or model options");
            const bool host=args[5]=="--host-reference";
            auto profile=host?hardware::HostReference():manager.HardwareProfile(id);
            for(size_t i=host?6:5;i<args.size();++i){
                const auto& option=args[i];Require(i+1<args.size(),"Missing hardware argument");const auto& value=args[++i];
                if(option=="--nt")profile.nt=value;
                else if(option=="--cpu")profile.models[0]=value;
                else if(option=="--gpu")profile.models[1]=value;
                else if(option=="--disk")profile.models[2]=value;
                else if(option=="--bios")profile.models[3]=value;
                else if(option=="--board")profile.models[4]=value;
                else throw std::runtime_error("Unknown hardware option");
            }
            manager.ConfigureHardware(id,profile);std::cout<<manager.Hardware(id)<<'\n';return 0;
        } else if(operation=="drivers") {
            Require(args.size()==5,"Unexpected drivers argument");std::cout<<manager.Drivers(id)<<'\n';return 0;
        } else if(operation=="driver-import") {
            Require(args.size()==9,"driver-import requires VM, package name, driver.sys, NT target and device class");
            manager.ImportDriver(id,args[5],Utf8(args[6]),args[7],args[8]);std::cout<<manager.Drivers(id)<<'\n';return 0;
        } else if(operation=="driver-install") {
            Require(args.size()==6,"driver-install requires VM and imported INF package name");
            manager.InstallDriver(id,args[5],executable.parent_path()/"guardian-driver-api.gkd");std::cout<<manager.Drivers(id)<<'\n';return 0;
        } else if (operation == "create") {
            Require(args.size() >= 7, "create requires ID, kernel image and contract");
            VmConfig config;
            config.id = id;
            config.contract = args[6];
            std::filesystem::path base;
            for (size_t i = 7; i < args.size(); ++i) {
                const auto &a = args[i];
                if(a=="--maximum-resources")
                    config.maximumResources=true;
                else if (a == "--persistent")
                    config.persistent = true;
                else if (a == "--disposable")
                    config.persistent = false;
                else {
                    Require(i + 1 < args.size(), "Missing create option argument");
                    const auto &value = args[++i];
                    if (a == "--ram-mib")
                        config.ramMiB = Number(value);
                    else if(a=="--host-reserve-mib")
                        config.hostReserveMiB=Number(value);
                    else if(a=="--disk-folder")
                        config.diskFolder=Utf8(value);
                    else if(a=="--disk-capacity-gib")
                        config.diskCapacityBytes=uint64_t(Number(value))*(1ull<<30);
                    else if(a=="--disk-growth-mib")
                        config.diskGrowthBytes=uint64_t(Number(value))*(1ull<<20);
                    else if(a=="--vcpus")
                        config.requestedVcpus=Number(value);
                    else if(a=="--volume-quota-kib")
                        config.volumeQuotaBytes=uint64_t(Number(value))*1024;
                    else if(a=="--storage-limit-mib")
                        config.storageLimitBytes=uint64_t(Number(value))*(1ull<<20);
                    else if(a=="--overlay-limit-kib")
                        config.overlayLimitBytes=uint64_t(Number(value))*1024;
                    else if (a == "--timeout-ms")
                        config.timeoutMs = Number(value);
                    else if (a == "--base")
                        base = Utf8(value);
                    else
                        throw std::runtime_error("Unknown create option");
                }
            }
            manager.Create(config, Utf8(args[5]), base);
        } else if (operation == "start") {
            Require(args.size() == 5 || (args.size() == 6 && args[5] == "--paused"), "Unknown start option");
            manager.Start(id, executable, args.size() == 6);
        } else if (operation == "snapshot" || operation == "restore" || operation == "drop-snapshot") {
            Require(args.size() == 6, "Snapshot name required");
            manager.Command(id, operation, args[5], executable);
        } else if (operation == "clone") {
            Require(args.size() == 6, "Clone destination ID required");
            manager.Clone(id, args[5]);
            std::cout << manager.Status(args[5]) << '\n';
            return 0;
        } else if(operation=="network") {
            Require(args.size()>=6,"network requires offline, simulated, lab or proxy");
            av::microvm::net::Policy policy;const auto& mode=args[5];using guardian::net::Mode;
            if(mode=="offline")policy.mode=Mode::Offline;else if(mode=="simulated")policy.mode=Mode::Simulated;
            else if(mode=="lab")policy.mode=Mode::Lab;else if(mode=="proxy")policy.mode=Mode::Proxy;else throw std::runtime_error("Unknown network profile");
            for(size_t i=6;i<args.size();++i){const auto& option=args[i];Require(i+1<args.size(),"Missing network argument");const auto& value=args[++i];
                if(option=="--node")policy.node=Number(value);
                else if(option=="--lab")policy.lab=value;
                else if(option=="--peer")policy.peers.push_back(Number(value));
                else if(option=="--port"||option=="--relay-port"){auto portNumber=Number(value);Require(portNumber&&portNumber<=65535,"Port out of range");if(option=="--port")policy.ports.push_back(uint16_t(portNumber));else policy.relayPort=uint16_t(portNumber);}
                else if(option=="--relay")policy.relay=av::microvm::net::ParseAddress(4,value);
                else if(option=="--packet-limit")policy.packetLimit=Number(value);
                else if(option=="--byte-limit")policy.byteLimit=Number(value);
                else if(option=="--allow"){
                    std::vector<std::string> fields;size_t start=0;for(;;){auto end=value.find(',',start);fields.push_back(value.substr(start,end==value.npos?end:end-start));if(end==value.npos)break;start=end+1;}
                    Require(fields.size()==4,"--allow requires name,family,address,port");auto family=Number(fields[1]),port=Number(fields[3]);Require((family==4||family==6)&&port&&port<=65535,"Destination family/port rejected");
                    policy.destinations.push_back({fields[0],uint8_t(family),av::microvm::net::ParseAddress(uint8_t(family),fields[2]),uint16_t(port)});
                }else throw std::runtime_error("Unknown network option");
            }
            manager.ConfigureNetwork(id,policy);
        } else if (operation == "configure") {
            Require(args.size() == 9 && args[5] == "--ram-mib" && args[7] == "--timeout-ms",
                    "configure requires --ram-mib N --timeout-ms N");
            manager.Configure(id, Number(args[6]), Number(args[8]));
        } else if (operation == "delete") {
            Require(args.size() == 5, "Unexpected delete argument");
            manager.Delete(id);
            std::cout << "{\"deleted\":true}\n";
            return 0;
        } else if (operation == "status") {
            Require(args.size() == 5, "Unexpected status argument");
        } else if (operation == "pause" || operation == "resume" || operation == "restart" ||
                   operation == "shutdown" || operation == "suspend") {
            Require(args.size() == 5, "Unexpected lifecycle argument");
            manager.Command(id, operation, {}, executable);
        } else
            throw std::runtime_error("Unknown VM operation");
        std::cout << manager.Status(id) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "VM operation failed: " << e.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0)
            return 2;
        std::string s(static_cast<size_t>(n), char{});
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, s.data(), n, nullptr, nullptr);
        s.pop_back();
        args.push_back(std::move(s));
    }
    return Execute(args);
}
#else
int main(int argc, char **argv) { return Execute({argv, argv + argc}); }
#endif
