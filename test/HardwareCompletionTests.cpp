#include "../src/microvm/hardware/Package.h"
#include "../src/microvm/boot/KernelBoot.h"
#include "guardian/HardwareProjection.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <set>
using namespace av::microvm;
namespace {
size_t checks=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void Reject(const std::function<void()>& fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}Check(rejected,"Invalid operation accepted");}
void Replace(std::string& s,const std::string& from,const std::string& to){const auto at=s.find(from);Check(at!=s.npos,"Test fixture replacement absent");s.replace(at,from.size(),to);}
hardware::Profile Profile(){auto p=hardware::HostReference();p.nt="win11";p.models={"Guardian selected CPU label","NVIDIA GeForce GT 1030","Requested disk","Selected BIOS 12A","Selected motherboard"};return p;}
void Projection(){
    auto p=Profile();const auto i=hardware::Inventory(p,32ull<<20,"hardware");
    using namespace guardian::hardware;
    Check(std::string(i.devices[0].effectiveModel)==p.models[0],"CPU effective label differs");
    Check(std::string(i.devices[1].effectiveModel)!=p.models[1],"Requested GPU was advertised as emulated");
    RegistryValue v{};std::set<std::string> pairs;
    for(uint32_t n=0;RegistryAt(i,n,v);++n){Check(RegistryKey(i,v.key),"Projected registry key absent");Check(RegistryQuery(i,v.key,v.name)==v.value,"Projected value differs");pairs.insert(std::string(v.key)+v.name);}
    Check(pairs.size()==RegistryCount,"Duplicate registry projection");
    Check(!RegistryQuery(i,"HARDWARE\\absent","ProcessorNameString"),"Unknown registry key accepted");
    Check(std::string(RegistryQuery(i,"hKlM\\hardware\\description\\system\\centralprocessor\\0","processorNAMEstring"))==p.models[0],"Registry normalization failed");
    char brand[49]{};
    for(uint32_t n=0;n<3;++n){uint32_t a=0,b=0,c=0,d=0;CpuIdentity(p.models[0].c_str(),0x80000002+n,0,a,b,c,d);uint32_t values[]{a,b,c,d};std::memcpy(brand+n*16,values,16);}
    Check(brand==p.models[0],"CPUID brand differs");
    uint32_t a=0x12345678,b=0xAABBCCDD,c=0x13579BDF,d=0xFFFFFFFF;
    CpuIdentity(brand,1,0,a,b,c,d);Check(a==0x12345678&&b==0x1CCDD&&c==0x93579BDF&&d==0xEFFFFFFF,"CPUID feature preservation/topology failed");
    a=11;b=22;c=33;d=44;CpuIdentity(brand,7,0,a,b,c,d);Check(a==11&&b==22&&c==33&&d==44,"Unrelated ISA features changed");
    for(uint32_t sub=0;sub<3;++sub){CpuIdentity(brand,0xB,sub,a,b,c,d);Check(!a&&b==(sub<2?1u:0u)&&!d,"Topology is not one vCPU");}
    const auto f=Smbios(i);Check(f.good&&f.size<=sizeof(f.bytes)&&f.bytes[1]==3&&f.bytes[2]==2,"SMBIOS header rejected");
    uint32_t table=0;std::memcpy(&table,f.bytes+4,4);Check(table==f.size-8,"SMBIOS raw table length differs");
    size_t at=8;std::set<uint8_t> types;
    while(at+4<=f.size){const auto type=f.bytes[at],length=f.bytes[at+1];Check(length>=4&&length<=f.size-at,"SMBIOS formatted length invalid");
        size_t end=at+length;while(end+1<f.size&&(f.bytes[end]||f.bytes[end+1]))++end;Check(end+1<f.size,"SMBIOS string terminator absent");
        const auto string=[&](uint8_t index){Check(index!=0,"SMBIOS required index absent");size_t start=at+length;for(uint32_t n=1;n<index;++n){while(start<end&&f.bytes[start])++start;++start;}
            Check(start<=end,"SMBIOS index outside strings");size_t stop=start;while(stop<=end&&f.bytes[stop])++stop;Check(stop<=end,"SMBIOS string outside table");return std::string(reinterpret_cast<const char*>(f.bytes+start),stop-start);};
        if(type==0)Check(string(f.bytes[at+5])==p.models[3],"BIOS firmware differs from profile");
        if(type==1||type==2){Check(string(f.bytes[at+5])==p.models[4],"Board firmware differs from profile");Check(string(f.bytes[at+7])==i.devices[4].syntheticId,"Firmware serial not synthetic profile ID");}
        if(type==4){Check(string(f.bytes[at+16])==brand,"Firmware CPU differs from CPUID");Check(f.bytes[at+35]==1&&f.bytes[at+37]==1,"Firmware CPU topology differs");}
        Check(types.insert(type).second,"Duplicate SMBIOS type");at=end+2;
    }
    Check(at==f.size&&types==std::set<uint8_t>{0,1,2,4,127},"SMBIOS structure set invalid");
}
void Packages(const std::filesystem::path& root,const std::filesystem::path& fixtures){
    const auto p=Profile();const auto inf=state::ReadFile(fixtures/"guardian-hardware-probe.inf",65536);
    const auto original=std::string(inf.begin(),inf.end());const auto image=state::ReadFile(fixtures/"guardian-hardware-probe.sys",guardian::driver::MaxFile);
    state::AtomicWrite(root/"guardian-hardware-probe.sys",image);
    const auto source=root/"probe.inf";
    const auto read=[&](const std::string& text){state::AtomicText(source,text);return hardware::ReadPackage(source,"win11","board");};
    auto package=read(original);Check(package.Compatibility(p).empty()&&package.imports.size()==4,"Owned driver package is not eligible");
    Check(package.hardwareId==guardian::driver::HardwareId&&package.service=="GuardianProbe","INF metadata differs");
    auto data=package.Encode();const auto decoded=hardware::DriverPackage::Decode(data);Check(decoded.image==image&&decoded.inf==inf&&decoded.imports==package.imports,"Package round trip differs");
    for(size_t n=0;n<32;++n)Reject([&]{hardware::DriverPackage::Decode(std::span(data).first(n));});
    data.back()^=1;Reject([&]{hardware::DriverPackage::Decode(data);});
    package.service="ForgedService";Reject([&]{hardware::DriverPackage::Decode(package.Encode());});
    auto other=p;other.nt="win10";Check(!decoded.Compatibility(other).empty(),"Declared NT mismatch accepted");
    auto nt10=decoded;nt10.nt="win10";Check(nt10.Compatibility(other).empty(),"Selected NT 10 package refused");
    nt10.nt=other.nt="win7";Check(nt10.Compatibility(other)=="inf-minimum-nt-not-met","INF minimum NT not enforced");
    auto modified=original;Replace(modified,"ROOT\\GUARDIAN_PROBE","ROOT\\OTHER_DEVICE");Check(!read(modified).Compatibility(p).empty(),"Unsupported device accepted");
    modified=original;Replace(modified,"NTamd64.10.0","NTx86.10.0");Replace(modified,"NTamd64.10.0","NTx86.10.0");Check(!read(modified).Compatibility(p).empty(),"Unsupported INF architecture accepted");
    modified=original;Replace(modified,"CopyFiles=ProbeFiles","CopyFiles=ProbeFiles\nNeeds=External.Service");Check(!read(modified).Compatibility(p).empty(),"External dependency accepted");
    modified=original;Replace(modified,"CopyFiles=ProbeFiles","CopyFiles=ProbeFiles\nMysteryInstallAction=1");Check(!read(modified).Compatibility(p).empty(),"Unknown install directive ignored");
    modified=original;Replace(modified,"guardian-hardware-probe.sys","../escape.sys");Reject([&]{read(modified);});
    modified=original;Replace(modified,"[Version]","[Version]\nCatalogFile=probe.cat");Reject([&]{read(modified);});
    state::AtomicWrite(root/"probe.cat",std::vector<uint8_t>{0x30,0x0B,6,9,0x2A,0x86,0x48,0x86,0xF7,0x0D,1,7,2});
    Reject([&]{read(modified);});
    state::AtomicWrite(root/"probe.cat",std::vector<uint8_t>{0x30,0x23,6,9,0x2A,0x86,0x48,0x86,0xF7,0x0D,1,7,2,0xA0,0x16,0x30,0x14,2,1,1,0x31,0,0x30,0x0B,6,9,0x2A,0x86,0x48,0x86,0xF7,0x0D,1,7,1,0x31,0});
    const auto cat=read(modified);Check(cat.Compatibility(p).find("catalog-trust")!=std::string::npos,"Unverified CAT treated as trusted");
    auto changed=image;const std::string symbol="IoCreateDevice";auto it=std::search(changed.begin(),changed.end(),symbol.begin(),symbol.end());Check(it!=changed.end(),"Reference import absent");*it='X';state::AtomicWrite(root/"guardian-hardware-probe.sys",changed);
    Check(!read(original).Compatibility(p).empty(),"Unsupported kernel import accepted");
    std::filesystem::remove(root/"guardian-hardware-probe.sys");Reject([&]{read(original);});
}
void Drivers(const std::filesystem::path& root,const std::filesystem::path& kernel,const std::filesystem::path& fixtures){
    const auto p=Profile();const auto image=boot::KernelImage::Read(kernel);
    hardware::DriverOptions driver{state::ReadFile(fixtures/"guardian-hardware-probe.sys",guardian::driver::MaxFile),state::ReadFile(fixtures/"ntoskrnl.exe",guardian::driver::MaxFile),"GuardianHardwareProbe"};
    boot::CpuOptions options{};options.driver=true;
    boot::KernelBootSession original;original.Prepare(image,32ull<<20,1201,options,{}, {}, {},&p,driver);original.PauseAtDriverWrite(2);
    const auto paused=original.Run(std::chrono::seconds(10));
    if(!paused.paused){boot::WriteBootReport(root/"pause-failure.json",{paused});}
    Check(paused.paused,"Driver did not pause at actual device exchange");const auto snapshot=original.Capture();
    const auto run=[&](const boot::SessionSnapshot& saved){boot::KernelBootSession candidate;candidate.Prepare(image,32ull<<20,1201,options,{}, {}, {},&p,driver);candidate.Restore(saved);return candidate.Run(std::chrono::seconds(10));};
    const auto result=run(snapshot);boot::WriteBootReport(root/"driver-success.json",{result});
    Check(result.success&&result.driverReportValid&&result.driver.installed&&result.driver.initialized&&result.driver.functional&&result.driver.unloaded&&result.driver.operations==4&&!result.driver.error,"Guest driver lifecycle failed");
    const auto second=run(snapshot);Check(second.success&&second.driver.apiAfter==result.driver.apiAfter,"Snapshot branch lost device or loader state");
    auto changed=snapshot;changed.machine.ram.at(guardian::driver::SourceGpa)^=1;Reject([&]{run(changed);});
    changed=snapshot;changed.machine.ram.at(guardian::driver::ApiGpa)^=1;Reject([&]{run(changed);});
    changed=snapshot;changed.machine.access.at(guardian::driver::SourceGpa/4096)=3;Reject([&]{run(changed);});
    changed=snapshot;changed.machine.ram.at(guardian::driver::BootGpa)^=1;Reject([&]{run(changed);});
    auto different=driver;different.image.back()^=1;boot::KernelBootSession mismatch;mismatch.Prepare(image,32ull<<20,1201,options,{}, {}, {},&p,different);Reject([&]{mismatch.Restore(snapshot);});
    for(unsigned n=0;n<2;++n){auto target=p;target.nt=n?"win11":"win10";const uint64_t ram=n?64ull<<20:16ull<<20;
        boot::KernelBootSession boundary;boundary.Prepare(image,ram,1210+n,options,{}, {}, {},&target,driver);const auto r=boundary.Run(std::chrono::seconds(10));
        boot::WriteBootReport(root/(target.nt+"-driver-boundary.json"),{r});Check(r.success&&r.driverReportValid&&r.driver.functional&&r.driver.unloaded&&r.driver.operations==4,"Selected NT/RAM boundary driver execution failed");}
    driver.image=state::ReadFile(fixtures/"guardian-hardware-probe-failing.sys",guardian::driver::MaxFile);
    boot::KernelBootSession failing;failing.Prepare(image,32ull<<20,1202,options,{}, {}, {},&p,driver);const auto failure=failing.Run(std::chrono::seconds(10));boot::WriteBootReport(root/"driver-entry-failure.json",{failure});
    Check(!failure.success&&failure.driverReportValid&&failure.driver.installed&&!failure.driver.initialized&&!failure.driver.functional&&failure.driver.error==0xC0000001u,"Failed DriverEntry reported as functional");
    // A forged lifecycle packet without device I/O must fail in the host model.
    boot::KernelBootSession forged;forged.Prepare(image,32ull<<20,1203,options,{}, {}, {},&p,driver);auto& vm=forged.Machine();hardware::DriverBackend backend(vm,1203,driver);
    auto* ram=static_cast<uint8_t*>(vm.RamData());guardian::driver::Boot descriptor{};std::memcpy(&descriptor,ram+guardian::driver::BootGpa,sizeof(descriptor));
    const guardian::driver::Report lie{guardian::driver::Magic,1,sizeof(lie),1203,descriptor.packageHash,1,1,1,1,0,4,0x0000000100010101ull,0x0000010101010000ull};
    const guardian::driver::Request request{guardian::driver::Magic,1,sizeof(request),1,guardian::driver::Op::Report,0,0,0,0,0};std::memcpy(ram+guardian::driver::ReportGpa,&lie,sizeof(lie));std::memcpy(ram+guardian::driver::RequestGpa,&request,sizeof(request));
    MicroVm::IoAccess access{};access.write=true;access.bytes=1;access.value=1;backend.Control(access);Check(!backend.Valid(),"Forged success report accepted without device proof");
}
void Runtime(const std::filesystem::path& root,const std::filesystem::path& kernel){
    auto p=Profile();boot::CpuOptions options{};options.enabled=options.processes=options.io=options.storage=true;
    boot::StorageOptions storage{};storage.base=root/"runtime.gvd";storage.overlay=root/"runtime.gvo";storage.createIfMissing=true;
    boot::KernelBootSession session;session.Prepare(boot::KernelImage::Read(kernel),32ull<<20,1204,options,storage,{}, {},&p);
    const auto result=session.Run(std::chrono::seconds(25));boot::WriteBootReport(root/"runtime-firmware.json",{result});
    Check(result.success&&result.ioConsole.find("PE RUNTIME OK")!=std::string::npos,"CPL3 firmware/registry/CPUID consistency checks failed");
}
}
int main(int argc,char** argv){try{
    if(argc<3)throw std::runtime_error("Usage: hardware_completion_tests projection ROOT | packages ROOT FIXTURES | driver ROOT KERNEL FIXTURES | runtime ROOT KERNEL");
    const auto root=std::filesystem::path(argv[2])/state::Token();std::filesystem::create_directories(root);const std::string mode=argv[1];
    if(mode=="projection"&&argc==3)Projection();else if(mode=="packages"&&argc==4)Packages(root,argv[3]);else if(mode=="driver"&&argc==5)Drivers(root,argv[3],argv[4]);else if(mode=="runtime"&&argc==4)Runtime(root,argv[3]);else throw std::runtime_error("Invalid arguments");
    std::cout<<checks<<" phase 12A completion checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<"Phase 12A completion test failed: "<<e.what()<<'\n';return 1;}}
