#include "../src/microvm/hardware/Profile.h"
#include "../src/microvm/state/VmManager.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <functional>
using namespace av::microvm;
namespace {
size_t checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void Reject(const std::function<void()>& fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}Check(rejected,"Invalid operation was accepted");}
std::vector<uint8_t> Driver(bool x64=true){
    std::vector<uint8_t> bytes(1024,0);
    const auto put=[&](size_t pos,uint32_t value,unsigned size){for(unsigned n=0;n<size;++n)bytes.at(pos+n)=uint8_t(value>>(8*n));};
    put(0,0x5a4d,2);put(0x3c,0x80,4);put(0x80,0x4550,4);put(0x84,x64?0x8664:0x14c,2);put(0x86,1,2);
    put(0x94,0xf0,2);put(0x98,x64?0x20b:0x10b,2);put(0x98+68,1,2);
    const size_t section=0x98+0xf0;put(section+16,0x200,4);put(section+20,0x200,4);bytes[0x200]=0xc3;
    return bytes;
}
void Core(const std::filesystem::path& root){
    auto p=hardware::HostReference();hardware::Validate(p);
    const auto second=hardware::HostReference();Check(p.identity!=second.identity,"Generated VM identities were reused");
    const auto encoded=hardware::Encode(p);const auto decoded=hardware::Decode(encoded);
    Check(p.models==decoded.models&&p.nt==decoded.nt&&p.identity==decoded.identity,"Profile persistence changed fields");
    for(const std::string nt:{"xp","vista","win7","win8","win81","win10","win11"}){
        p.nt=nt;const auto i=hardware::Inventory(p,32ull<<20,"graphics");
        Check(guardian::hardware::Valid(i,32ull<<20),"NT target inventory failed");
        Check(i.devices[1].support==guardian::hardware::Support::Functional,"Own graphics unavailable");
        Check(i.devices[2].support==guardian::hardware::Support::MetadataOnly,"Inactive disk advertised as functional");
    }
    p.nt="win11";p.models[1]="NVIDIA GeForce GT 1030";
    auto info=hardware::Inventory(p,32ull<<20,"graphics");
    const auto json=hardware::Json(info);
    Check(json.find("NVIDIA GeForce GT 1030")!=json.npos&&json.find("Guardian BGRA software compositor")!=json.npos,
        "Requested model concealed effective graphics capability");
    Check(json.find("\"windowsCompatibility\":\"unsupported\"")!=json.npos,"Unsupported Windows compatibility was advertised");
    auto bad=info;bad.devices[1].syntheticId[0]='x';Check(!guardian::hardware::Valid(bad,bad.ramBytes),"Non-synthetic ID accepted");
    bad=info;std::memcpy(bad.devices[1].syntheticId,bad.devices[0].syntheticId,sizeof(bad.devices[1].syntheticId));
    Check(!guardian::hardware::Valid(bad,bad.ramBytes),"Duplicate IDs accepted");
    bad=info;bad.vcpus=2;Check(!guardian::hardware::Valid(bad,bad.ramBytes),"Unimplemented SMP advertised");
    bad=info;bad.devices[0].effectiveModel[95]='x';Check(!guardian::hardware::Valid(bad,bad.ramBytes),"Unterminated model accepted");
    bad=info;bad.devices[0].requestedModel[2]='\n';Check(!guardian::hardware::Valid(bad,bad.ramBytes),"Control character accepted");
    for(size_t n=0;n<encoded.size();++n)Reject([&]{hardware::Decode(std::span(encoded).first(n));});
    auto corrupt=encoded;corrupt.back()^=1;Reject([&]{hardware::Decode(corrupt);});
    auto invalid=p;invalid.models[2]=std::string(96,'a');Reject([&]{hardware::Encode(invalid);});
    invalid=p;invalid.identity="host-serial-number";Reject([&]{hardware::Encode(invalid);});
    invalid=p;invalid.nt="win99";Reject([&]{hardware::Encode(invalid);});
    const auto file=root/"owned-driver.sys";state::AtomicWrite(file,Driver());
    auto bundle=hardware::ImportDriver(p,file,"win11","graphics");
    auto driver=hardware::DriverJson(bundle,p);
    Check(driver.find("windows-kernel-driver-abi-not-implemented")!=driver.npos&&driver.find("\"installed\":false")!=driver.npos,
        "Import incorrectly claimed a driver was installed");
    auto other=p;other.nt="win10";Check(hardware::DriverJson(bundle,other).find("requested-nt-mismatch")!=std::string::npos,"NT mismatch missed");
    state::AtomicWrite(file,Driver(false));bundle=hardware::ImportDriver(p,file,"win11","graphics");
    Check(hardware::DriverJson(bundle,p).find("guest-architecture-mismatch")!=std::string::npos,"Architecture mismatch missed");
    state::AtomicWrite(file,Driver());Reject([&]{hardware::ImportDriver(p,file,"win11","../host");});
    auto image=Driver();image[0x98+68]=2;state::AtomicWrite(file,image);Reject([&]{hardware::ImportDriver(p,file,"win11","graphics");});
    image=Driver();image[0x3c]=0xff;image[0x3d]=0xff;state::AtomicWrite(file,image);Reject([&]{hardware::ImportDriver(p,file,"win11","graphics");});
    image=Driver();image[0x98+0xf0+20]=0xff;image[0x98+0xf0+21]=0xff;state::AtomicWrite(file,image);Reject([&]{hardware::ImportDriver(p,file,"win11","graphics");});
    bundle.back()^=1;Reject([&]{hardware::DriverJson(bundle,p);});
    std::cout<<checks<<" hardware profile and inert driver import checks PASS\n";
}
void Boot(const std::filesystem::path& root,const std::filesystem::path& basic,const std::filesystem::path& cpu){
    auto p=hardware::HostReference();p.models[1]="NVIDIA GeForce GT 1030";
    auto image=boot::KernelImage::Read(basic);
    for(unsigned n=0;n<2;++n){
        const auto guest=n?boot::KernelImage::Read(cpu):image;
        boot::CpuOptions options{};options.enabled=n!=0;
        boot::KernelBootSession session;session.Prepare(guest,32ull<<20,100+n,options,{}, {}, {},&p);
        const auto expected=hardware::Inventory(p,32ull<<20,"basic");
        auto snap=session.Capture();
        Check(snap.machine.access.at(guardian::hardware::InfoGpa/4096)==1,"Hardware page is writable in second-stage mappings");
        boot::KernelBootSession resumed;resumed.Prepare(guest,32ull<<20,100+n,options,{}, {}, {},&p);resumed.Restore(snap);
        const auto result=resumed.Run(std::chrono::seconds(10));
        char hash[17]{};std::snprintf(hash,sizeof(hash),"%016llx",static_cast<unsigned long long>(guardian::hardware::Digest(expected)));
        Check(result.success,"Guest did not boot with the hardware extension");
        Check(result.console.find(std::string("hardware inventory v1 PASS ")+hash)!=result.console.npos,"Guest did not independently validate the exact host inventory");
        auto changed=p;changed.models[1]="Other requested GPU";
        boot::KernelBootSession mismatch;mismatch.Prepare(guest,32ull<<20,100+n,options,{}, {}, {},&changed);Reject([&]{mismatch.Restore(snap);});
        auto permissions=snap;permissions.machine.access.at(guardian::hardware::InfoGpa/4096)=3;
        boot::KernelBootSession writeable;writeable.Prepare(guest,32ull<<20,100+n,options,{}, {}, {},&p);Reject([&]{writeable.Restore(permissions);});
        boot::KernelBootSession missing;missing.Prepare(guest,32ull<<20,100+n,options);Reject([&]{missing.Restore(snap);});
    }
    {
        boot::KernelBootSession invalid;invalid.Prepare(image,32ull<<20,200,{}, {}, {}, {},&p);
        auto* bytes=static_cast<uint8_t*>(invalid.Machine().RamData());
        auto* info=reinterpret_cast<guardian::hardware::Info*>(bytes+guardian::hardware::InfoGpa);info->vcpus=2;
        const auto failed=invalid.Run(std::chrono::seconds(1));Check(!failed.success,"Guest accepted a corrupt hardware inventory");
        Check(failed.console.find("invalid hardware inventory")!=failed.console.npos,"Guest did not diagnose invalid inventory");
    }
    const auto catalog=root/"catalog";
    state::VmManager manager(catalog);state::VmConfig config;config.hostReserveMiB=1024;config.id="hardware";config.contract="basic";config.hardware=p;
    manager.Create(config,basic);const auto before=manager.Hardware(config.id);
    Check(before.find("NVIDIA GeForce GT 1030")!=before.npos,"VM manager lost hardware profile");
    auto edit=manager.HardwareProfile(config.id);edit.models[4]="Custom motherboard";manager.ConfigureHardware(config.id,edit);
    Check(manager.Hardware(config.id).find("Custom motherboard")!=std::string::npos,"VM hardware edit was not persisted");
    const auto file=root/"catalog-driver.sys";state::AtomicWrite(file,Driver());manager.ImportDriver(config.id,"display",file,"win11","graphics");
    Check(manager.Drivers(config.id).find("\"installed\":false")!=std::string::npos,"VM driver audit advertised installation");
    Reject([&]{manager.ImportDriver(config.id,"display",file,"win11","graphics");});
    Reject([&]{manager.ImportDriver(config.id,"../escape",file,"win11","graphics");});
    manager.Clone(config.id,"copy");
    Check(manager.Hardware("copy")==manager.Hardware(config.id),"Clone changed checkpoint-compatible identity");
    Check(manager.Drivers("copy")==manager.Drivers(config.id),"Clone lost inert driver packages");
    for(unsigned n=0;n<15;++n)manager.ImportDriver(config.id,"package"+std::to_string(n),file,"win11","graphics");
    Reject([&]{manager.ImportDriver(config.id,"overflow",file,"win11","graphics");});
    state::AtomicText(catalog/config.id/"snapshots"/"owned.gvs","owned test marker");
    Reject([&]{manager.ConfigureHardware(config.id,edit);});
    std::cout<<checks<<" hardware WHP/KVM guest and catalog checks PASS\n";
}
}
int main(int argc,char** argv){
    try{
        if(argc!=3&&argc!=5)throw std::runtime_error("Usage: hardware_tests core ROOT | boot ROOT BASIC.gk CPU.gk");
        const auto root=std::filesystem::path(argv[2])/state::Token();
        Check(!std::filesystem::exists(root),"Test evidence directory already exists");std::filesystem::create_directories(root);
        if(std::string(argv[1])=="core"&&argc==3)Core(root);
        else if(std::string(argv[1])=="boot"&&argc==5)Boot(root,argv[3],argv[4]);
        else throw std::runtime_error("Invalid test arguments");
        return 0;
    }catch(const std::exception& e){std::cerr<<"Hardware test failed: "<<e.what()<<'\n';return 1;}
}
