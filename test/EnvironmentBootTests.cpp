#include "../guest/Volume.h"
#include "../src/microvm/boot/KernelBoot.h"
#include "../src/microvm/environment/Profile.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace av::microvm;
namespace ge=guardian::environment;namespace gs=guardian::storage;namespace fs=std::filesystem;
namespace {
gs::Image DiskImage(const fs::path& base,const fs::path& overlay,const std::vector<uint8_t>& bytes){
    state::AtomicWrite(overlay,bytes);boot::PersistentBlockStore store({base,overlay,true});gs::Image images[2]{};
    for(uint32_t slot=0;slot<2;++slot)for(uint32_t n=0;n<sizeof(gs::Image);n+=512)
        state::Require(store.Read((slot?gs::SecondSlot:gs::FirstSlot)+n,reinterpret_cast<uint8_t*>(&images[slot])+n),"Read profile image failed");
    const bool a=gs::Volume::Validate(images[0]),b=gs::Volume::Validate(images[1]);state::Require(a||b,"Guest profile volume invalid");
    return !a||(b&&images[1].header.sequence>images[0].header.sequence)?images[1]:images[0];
}
std::string Hex(const uint8_t* b,uint32_t n){std::ostringstream o;o<<std::hex<<std::setfill('0');for(uint32_t i=0;i<n;++i)o<<std::setw(2)<<uint32_t(b[i]);return o.str();}
std::string Rows(const gs::Image& image){
    std::ostringstream o;o<<'[';bool comma=false;
    for(uint32_t i=0;i<gs::MaxNodes;++i){const auto& n=image.nodes[i];if(n.kind==gs::Kind::Empty)continue;if(comma)o<<',';comma=true;
        o<<"{\"slot\":"<<i<<",\"parent\":"<<n.parent<<",\"id\":"<<n.id<<",\"owner\":"<<n.security.owner<<",\"kind\":"<<uint32_t(n.kind)<<",\"name\":"<<std::quoted(n.name)<<",\"type\":"<<uint32_t(n.type)<<",\"hex\":\""<<Hex(n.data,n.size)<<"\",\"times\":[";
        for(uint32_t field=0;field<3;++field){if(field)o<<',';uint32_t seconds=0;const auto* bytes=reinterpret_cast<const uint8_t*>(image.header.reserved+1)+(i*3+field)*4;for(uint32_t x=0;x<4;++x)seconds|=uint32_t(bytes[x])<<(x*8);o<<gs::FileUtc(seconds);}o<<"]}";
    }return o.str()+']';
}
}
int main(int argc,char** argv){try{
    state::Require(argc==3,"Environment kernel and output required");const auto image=boot::KernelImage::Read(argv[1]);
    const auto output=fs::absolute(argv[2]);fs::create_directories(output);const auto root=output/("run-"+state::Token());fs::create_directory(root);
    const std::string canary="Guardian-private-canary-12E-5ef98a1c-83c6-4e07-92bf-not-a-guest-input";
    state::AtomicText(root/"host-personal.txt",canary);
    uint32_t checks=0;const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Environment boot check "+std::to_string(checks));};
    std::vector<boot::BootResult> boots;std::ostringstream matrix;matrix<<"{\"status\":\"PASS\",\"phase\":\"12E\",\"wine\":false,\"profiles\":[";bool comma=false;
    boot::CpuOptions cpu{};cpu.enabled=true;cpu.processes=true;cpu.io=true;cpu.storage=true;
    uint64_t id=120000;
    for(const auto* nt:{"win7","win10","win11"})for(uint32_t seed:{0u,42u}){
        uint64_t baseline=0;
        for(uint32_t repeat=0;repeat<2;++repeat){
            const auto directory=root/(std::string(nt)+"-"+std::to_string(seed)+"-"+std::to_string(repeat));fs::create_directory(directory);
            state::AtomicWrite(directory/"base.gvd",std::vector<uint8_t>(65536));const auto profile=environment::Make(seed,nt);const auto hardware=environment::Hardware(profile);const auto bootId=++id;
            boot::SessionSnapshot point;
            {boot::KernelBootSession first;first.Prepare(image,32ull<<20,bootId,cpu,{directory/"base.gvd",directory/"first.gvo",true},{},{},&hardware,{},nullptr,&profile);
                first.PauseAtIoConsole("12E READY\n");const auto result=first.Run(std::chrono::seconds(15));boots.push_back(result);
                check(result.paused&&result.ioConsole=="12E READY\n");point=first.Capture();}
            check(std::search(point.machine.ram.begin(),point.machine.ram.end(),canary.begin(),canary.end())==point.machine.ram.end());
            const auto disk=DiskImage(directory/"base.gvd",directory/"inspect.gvo",point.disk);const auto digest=gs::Hash(&disk,sizeof(disk));
            if(repeat)check(digest==baseline);else baseline=digest;
            if(comma)matrix<<',';comma=true;
            matrix<<"{\"ntBuild\":"<<profile.ntBuild<<",\"seed\":"<<seed<<",\"repeat\":"<<repeat<<",\"initialHash\":"<<digest<<",\"nodes\":"<<Rows(disk)<<'}';
            auto wrong=point;ge::Info wrongInfo{};std::memcpy(&wrongInfo,wrong.machine.ram.data()+ge::InfoGpa,sizeof(wrongInfo));wrongInfo.seed++;std::memcpy(wrong.machine.ram.data()+ge::InfoGpa,&wrongInfo,sizeof(wrongInfo));
            state::AtomicWrite(directory/"bad.gvo",point.disk);
            {boot::KernelBootSession changed;changed.Prepare(image,32ull<<20,bootId,cpu,{directory/"base.gvd",directory/"bad.gvo",true},{},{},&hardware,{},nullptr,&profile);bool refused=false;try{changed.Restore(wrong);}catch(const std::exception&){refused=true;}check(refused);}
            state::AtomicWrite(directory/"resumed.gvo",point.disk);
            {boot::KernelBootSession restored;restored.Prepare(image,32ull<<20,bootId,cpu,{directory/"base.gvd",directory/"resumed.gvo",true},{},{},&hardware,{},nullptr,&profile);restored.Restore(point);
                const auto result=restored.Run(std::chrono::seconds(15));boots.push_back(result);if(!result.success)std::cerr<<result.ioConsole<<result.error<<'\n';check(result.success&&result.ioConsole.find("12E DONE visits=1 opens=3")!=std::string::npos);
                check(std::count(result.ioConsole.begin(),result.ioConsole.end(),'\n')>=10);}
        }
    }
    matrix<<"],\"checks\":"<<checks<<",\"personalCanaryAbsent\":true,\"restores\":12,\"changedPolicyRejected\":12}\n";
    state::AtomicText(output/"profiles.json",matrix.str());boot::WriteBootReport(output/"boots.json",boots);
    std::cout<<checks<<" real synthetic-profile boots, deterministic full volumes, actual document reader, snapshot binding and host privacy checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
