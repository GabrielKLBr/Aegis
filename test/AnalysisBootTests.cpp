#include "../guest/Volume.h"
#include "../src/microvm/boot/KernelBoot.h"
#include "../src/microvm/environment/Profile.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
using namespace av::microvm;
namespace gt=guardian::telemetry;namespace gs=guardian::storage;namespace fs=std::filesystem;
namespace {
gs::Image DiskImage(const fs::path& base,const fs::path& overlay){
    boot::PersistentBlockStore store({base,overlay,true});gs::Image images[2]{};
    for(uint32_t slot=0;slot<2;++slot)for(uint32_t n=0;n<sizeof(gs::Image);n+=512)state::Require(store.Read((slot?gs::SecondSlot:gs::FirstSlot)+n,reinterpret_cast<uint8_t*>(&images[slot])+n),"Read analyzed volume failed");
    const bool a=gs::Volume::Validate(images[0]),b=gs::Volume::Validate(images[1]);state::Require(a||b,"Analyzed volume invalid");return !a||(b&&images[1].header.sequence>images[0].header.sequence)?images[1]:images[0];
}
const gs::Node* Find(const gs::Image& image,const char* name){for(const auto& n:image.nodes)if(n.kind==gs::Kind::File&&std::strcmp(n.name,name)==0)return &n;return nullptr;}
}
int main(int argc,char** argv){try{
    state::Require(argc==3,"Kernel and output required");const auto image=boot::KernelImage::Read(argv[1]);const auto output=fs::absolute(argv[2]);fs::create_directories(output);const auto root=output/("run-"+state::Token());fs::create_directory(root);
    uint32_t checks=0;const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Analysis boot check "+std::to_string(checks));};
    const auto reject=[&](auto action){bool bad=false;try{action();}catch(const std::exception&){bad=true;}check(bad);};
    const std::string canary="Guardian-13-host-private-control-432a351c-2b67-40d0";state::AtomicText(root/"host-private.txt",canary);
    boot::CpuOptions cpu{};cpu.enabled=cpu.processes=cpu.io=cpu.storage=cpu.network=true;std::vector<boot::BootResult> boots;uint64_t bootId=130000;
    check(image.SupportsTelemetry()&&image.SupportsAnalysisFixture());
    check(!boot::KernelImage::Read(fs::path(argv[1]).parent_path()/"guardian-kernel-cpu.gk").SupportsTelemetry());
    check(!boot::KernelImage::Read(fs::path(argv[1]).parent_path()/"guardian-kernel-storage.gk").SupportsTelemetry());
    for(const std::string name:{"allow","deny","simulate","benign","overflow"}){
        const auto dir=root/name;fs::create_directory(dir);state::AtomicWrite(dir/"base.gvd",std::vector<uint8_t>(65536));gt::Policy policy{};
        if(name=="deny")policy.denyMask=gt::Bit(gt::Domain::File);if(name=="simulate")policy.simulateMask=gt::Bit(gt::Domain::File);if(name=="overflow")policy.eventLimit=16;
        net::Options netOptions;netOptions.policy.mode=guardian::net::Mode::Simulated;netOptions.capture=dir/"net";fs::create_directory(netOptions.capture);boot::KernelBootSession session;session.Prepare(image,32ull<<20,++bootId,cpu,{dir/"base.gvd",dir/"disk.gvo",true},netOptions,{},nullptr,{},nullptr,nullptr,&policy,name=="benign"?1:0);
        const auto result=session.Run(std::chrono::seconds(15));boots.push_back(result);boot::WriteBootReport(output/(name+"-boot.json"),{result});const auto* trace=session.Analysis();check(trace!=nullptr);
        const auto json=trace->Json();state::AtomicText(output/(name+".json"),json);state::AtomicText(output/(name+".html"),trace->Html());
        if(name=="overflow"){check(!trace->Coherent()&&trace->Dropped()>0&&json.find("inconclusive")!=std::string::npos);continue;}
        check(result.success);check(trace->Coherent());check(result.ioConsole.find("13 DONE\n")!=std::string::npos);
        std::set<uint32_t> domains;uint64_t sequence=0;for(const auto& row:trace->Records()){check(row.event.sequence==++sequence);domains.insert(uint32_t(row.event.domain));if(row.event.domain==gt::Domain::File&&row.event.stage==gt::Stage::Result)check(row.event.actorPid==row.event.pid);}
        check(domains.count(uint32_t(gt::Domain::Network))&&domains.count(uint32_t(gt::Domain::File))&&domains.count(uint32_t(gt::Domain::Module))&&domains.count(uint32_t(gt::Domain::Thread))&&domains.count(uint32_t(gt::Domain::Process))&&domains.count(uint32_t(gt::Domain::Driver)));
        if(name=="allow")check(domains.size()>=9);
        if(name=="benign")check(json.find("\"classification\":\"legitimate\"")!=std::string::npos);
        else {check(json.find("persistence-registry-v1")!=std::string::npos);check(json.find("owned-service-start-v1")!=std::string::npos);check(result.ioConsole.find("13 CHILD EXECUTED\n")!=std::string::npos);}
        state::AtomicWrite(dir/"inspect.gvo",session.DiskSnapshot());const auto disk=DiskImage(dir/"base.gvd",dir/"inspect.gvo");
        const auto* file=Find(disk,name=="deny"||name=="benign"?"a.txt":"a.txt.locked");check(file!=nullptr);
        const std::string data(reinterpret_cast<const char*>(file->data),file->size);check(data==(name=="deny"?"Original synthetic document\n":"Rewritten inert synthetic document\n"));
        if(name=="deny"){check(json.find("bulk-rewrite-rename-v1")==std::string::npos);check(json.find("\"decision\":\"deny\"")!=std::string::npos);check(!Find(disk,"a.txt.locked"));}
        else if(name!="benign"){check(json.find("bulk-rewrite-rename-v1")!=std::string::npos);check(Find(disk,"b.txt.locked")&&Find(disk,"c.txt.locked"));if(name=="simulate")check(json.find("\"decision\":\"simulate\"")!=std::string::npos);}
    }
    const auto dir=root/"snapshot";fs::create_directory(dir);state::AtomicWrite(dir/"base.gvd",std::vector<uint8_t>(65536));const auto identity=++bootId;boot::SessionSnapshot point;
    {boot::KernelBootSession session;session.Prepare(image,32ull<<20,identity,cpu,{dir/"base.gvd",dir/"first.gvo",true});session.PauseAtIoConsole("13 READY\n");const auto result=session.Run(std::chrono::seconds(15));boots.push_back(result);check(result.paused);point=session.Capture();}
    check(std::search(point.machine.ram.begin(),point.machine.ram.end(),canary.begin(),canary.end())==point.machine.ram.end());
    point=boot::SessionSnapshot::Decode(point.Encode());state::AtomicWrite(dir/"resume.gvo",point.disk);
    {boot::KernelBootSession session;session.Prepare(image,32ull<<20,identity,cpu,{dir/"base.gvd",dir/"resume.gvo",true});session.Restore(point);const auto result=session.Run(std::chrono::seconds(15));boots.push_back(result);check(result.success&&session.Analysis()->Coherent());state::AtomicText(output/"snapshot.json",session.Analysis()->Json());}
    {gt::Policy changed{};changed.denyMask=gt::Bit(gt::Domain::File);state::AtomicWrite(dir/"changed.gvo",point.disk);boot::KernelBootSession session;session.Prepare(image,32ull<<20,identity,cpu,{dir/"base.gvd",dir/"changed.gvo",true},{},{},nullptr,{},nullptr,nullptr,&changed);reject([&]{session.Restore(point);});}
    // Separate legacy device contracts feed the same bounded timeline protocol.
    {const auto kernel=boot::KernelImage::Read(fs::path(argv[1]).parent_path()/"guardian-kernel-network.gk");boot::CpuOptions options{};options.enabled=options.network=true;net::Options networking;networking.policy.mode=guardian::net::Mode::Simulated;networking.capture=root/"network";fs::create_directory(networking.capture);
        boot::KernelBootSession session;session.Prepare(kernel,32ull<<20,++bootId,options,{},networking);const auto result=session.Run(std::chrono::seconds(15));boots.push_back(result);check(result.success&&session.Analysis()&&session.Analysis()->Coherent());state::AtomicText(output/"network.json",session.Analysis()->Json());check(std::any_of(session.Analysis()->Records().begin(),session.Analysis()->Records().end(),[](const auto& row){return row.event.domain==gt::Domain::Network&&row.event.stage==gt::Stage::Result&&row.event.operation==gt::Operation::Send;}));}
    {const auto parent=fs::path(argv[1]).parent_path();const auto kernel=boot::KernelImage::Read(parent/"guardian-kernel-hardware.gk");hardware::DriverOptions driver;driver.image=state::ReadFile(parent/"drivers/guardian-hardware-probe.sys",guardian::driver::MaxFile);driver.api=state::ReadFile(parent/"drivers/ntoskrnl.exe",guardian::driver::MaxFile);driver.service="GuardianHardwareProbe";
        const auto profile=environment::Hardware(environment::Make(13,"win11"));boot::CpuOptions options{};options.driver=true;boot::KernelBootSession session;session.Prepare(kernel,32ull<<20,++bootId,options,{},{},{},&profile,driver);const auto result=session.Run(std::chrono::seconds(15));boots.push_back(result);check(result.success&&session.Analysis()&&session.Analysis()->Coherent());state::AtomicText(output/"driver.json",session.Analysis()->Json());check(std::any_of(session.Analysis()->Records().begin(),session.Analysis()->Records().end(),[](const auto& row){return row.event.domain==gt::Domain::Driver&&row.event.stage==gt::Stage::Result&&row.event.operation==gt::Operation::Load&&!row.event.status;}));}
    check(state::ReadFile(root/"host-private.txt")==std::vector<uint8_t>(canary.begin(),canary.end()));boot::WriteBootReport(output/"boots.json",boots);
    state::AtomicText(output/"validation.json","{\"status\":\"PASS\",\"phase\":13,\"wine\":false,\"checks\":"+std::to_string(checks)+",\"scenarios\":5,\"snapshotRestored\":true,\"changedPolicyRejected\":true,\"realNetworkEvents\":true,\"combinedDomains\":9,\"realDriverEvents\":true,\"personalCanaryAbsent\":true}\n");std::cout<<checks<<" actual event, policy, state, device, snapshot and isolation checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
