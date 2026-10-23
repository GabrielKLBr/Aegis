#include "../src/microvm/environment/Profile.h"
#include "../guest/Volume.h"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
using namespace av::microvm;
namespace gs=guardian::storage;
template<size_t N,size_t M> void Copy(char (&out)[N],const char (&text)[M]) {
    static_assert(M<=N);std::memcpy(out,text,M);
}
struct Disk {std::array<uint8_t,65536> data{};uint64_t now=133485408000000000ull;int64_t writes=-1;};
bool Transfer(void* raw,guardian::io::Op op,uint64_t at,uint8_t* bytes) noexcept {
    auto& d=*static_cast<Disk*>(raw);if(op!=guardian::io::Op::Read){if(!d.writes)return false;if(d.writes>0)--d.writes;}
    if(op==guardian::io::Op::Flush)return true;if(at>65024)return false;
    if(op==guardian::io::Op::Read)std::memcpy(bytes,d.data.data()+at,512);else std::memcpy(d.data.data()+at,bytes,512);return true;
}
gs::Command Command(gs::Op op,uint64_t handle=0){gs::Command c{};c.op=op;c.handle=handle;return c;}
int main(int argc,char** argv){try{
    state::Require(argc==2,"Output directory required");const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
    uint32_t checks=0;const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Environment core check "+std::to_string(checks));};
    const auto reject=[&](auto action){bool bad=false;try{action();}catch(const std::exception&){bad=true;}check(bad);};
    for(const auto* nt:{"win7","win10","win11"})for(uint64_t seed:std::array<uint64_t,3>{0,42,UINT32_MAX}){
        const auto one=environment::Make(seed,nt),two=environment::Make(seed,nt);check(std::memcmp(&one,&two,sizeof(one))==0);
        check(environment::Hardware(one).identity==environment::Hardware(two).identity);
        for(uint32_t i=0;i<3;++i){const auto a=guardian::environment::Document(one,i),b=guardian::environment::Document(two,i);check(a.good&&a.size&&a.size<512&&std::memcmp(&a,&b,sizeof(a))==0);}
        check(environment::Json(one).find("\"hostImports\":false")!=std::string::npos);
    }
    reject([&]{environment::Make(uint64_t(UINT32_MAX)+1,"win11");});reject([&]{environment::Make(1,"xp");});
    auto bad=environment::Make(42,"win11");bad.version++;reject([&]{environment::Validate(bad);});
    bad=environment::Make(42,"win11");bad.catalogHash=1;reject([&]{environment::Validate(bad);});
    guardian::processes::Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;
    auto scheduler=std::make_unique<guardian::threads::Scheduler>();auto disk=std::make_unique<Disk>();auto volume=std::make_unique<gs::Volume>();
    volume->Configure(disk.get(),Transfer,*scheduler);check(volume->Mount()==0);
    auto c=Command(gs::Op::Open);Copy(c.path,"C:\\dates.txt");c.access=gs::All;c.disposition=1;check(volume->Execute(1,token,c)==0);const auto handle=c.handle;
    c=Command(gs::Op::FileTimesRead,handle);c.length=24;check(volume->Execute(1,token,c)==0);gs::FileTimes times{};std::memcpy(&times,c.data,24);check(!times.creation&&!times.access&&!times.write);
    times={disk->now-864000000000ull,disk->now,disk->now};c=Command(gs::Op::FileTimesWrite,handle);c.length=24;c.flags=7;std::memcpy(c.data,&times,24);check(volume->Execute(1,token,c)==0);
    volume->ConfigureClock([](void* p) noexcept {return static_cast<Disk*>(p)->now;});disk->now+=10000000;
    c=Command(gs::Op::Write,handle);c.length=1;c.data[0]=65;check(volume->Execute(1,token,c)==0);
    c=Command(gs::Op::FileTimesRead,handle);c.length=24;check(volume->Execute(1,token,c)==0);gs::FileTimes observed{};std::memcpy(&observed,c.data,24);check(observed.creation==times.creation&&observed.access==disk->now&&observed.write==disk->now);
    disk->now+=10000000;c=Command(gs::Op::Read,handle);c.length=1;check(volume->Execute(1,token,c)==0&&c.data[0]==65);
    c=Command(gs::Op::FileTimesRead,handle);c.length=24;check(volume->Execute(1,token,c)==0);std::memcpy(&observed,c.data,24);check(observed.access==disk->now&&observed.write==disk->now-10000000);
    const auto before=observed;
    for(uint64_t unsupported:{gs::UnixEpoch100ns-1,gs::UnixEpoch100ns+1,UINT64_MAX}){
        times={unsupported,unsupported,unsupported};c=Command(gs::Op::FileTimesWrite,handle);c.flags=7;c.length=24;std::memcpy(c.data,&times,24);check(volume->Execute(1,token,c)==gs::Unsupported);
        c=Command(gs::Op::FileTimesRead,handle);c.length=24;check(volume->Execute(1,token,c)==0);check(std::memcmp(c.data,&before,24)==0);
    }
    c=Command(gs::Op::FileTimesWrite,handle);c.length=24;c.flags=8;check(volume->Execute(1,token,c)==gs::Invalid);
    c=Command(gs::Op::FileTimesRead,handle);c.length=24;check(volume->Execute(2,token,c)==gs::InvalidHandle);
    check(volume->Flush()==0);const auto committed=disk->data;
    c=Command(gs::Op::Write,handle);c.length=1;c.data[0]=66;disk->now+=10000000;check(volume->Execute(1,token,c)==0);disk->writes=0;check(volume->Flush()==gs::IoError);disk->writes=-1;
    auto recovered=std::make_unique<gs::Volume>();recovered->Configure(disk.get(),Transfer,*scheduler);check(recovered->Mount()==0);
    c=Command(gs::Op::Open);Copy(c.path,"C:\\dates.txt");c.access=gs::Read;check(recovered->Execute(2,token,c)==0);const auto readOnly=c.handle;
    c=Command(gs::Op::FileTimesRead,readOnly);c.length=24;check(recovered->Execute(2,token,c)==0&&std::memcmp(c.data,&before,24)==0);
    c=Command(gs::Op::FileTimesWrite,readOnly);c.flags=7;c.length=24;check(recovered->Execute(2,token,c)==gs::Denied);
    c=Command(gs::Op::Read,readOnly);c.length=1;check(recovered->Execute(2,token,c)==0&&c.data[0]==65);check(disk->data==committed);
    volume->Exit(1);recovered->Exit(2);
    state::AtomicText(root/"core.json","{\"status\":\"PASS\",\"checks\":"+std::to_string(checks)+"}\n");
    std::cout<<checks<<" reproducible profiles, typed-only inputs, real file metadata, access rights and interrupted-commit checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
