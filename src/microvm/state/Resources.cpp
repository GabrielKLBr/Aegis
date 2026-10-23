#include "Resources.h"
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <thread>
#include <fstream>
#include <limits>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#include <sched.h>
#include <unistd.h>
#endif
namespace av::microvm::state::resources {
Host Probe() {
    Host h;
#ifdef _WIN32
    MEMORYSTATUSEX m{};m.dwLength=sizeof(m);
    Require(GlobalMemoryStatusEx(&m)!=0,"Cannot query host memory budget");
    h.total=m.ullTotalPhys;h.available=std::min(m.ullAvailPhys,m.ullAvailPageFile);
#else
    const auto page=::sysconf(_SC_PAGESIZE),total=::sysconf(_SC_PHYS_PAGES),available=::sysconf(_SC_AVPHYS_PAGES);
    Require(page>0&&total>0&&available>=0,"Cannot query host memory budget");
    h.total=uint64_t(page)*uint64_t(total);h.available=uint64_t(page)*uint64_t(available);
    std::ifstream info("/proc/meminfo");std::string name;uint64_t value=0;
    while(info>>name>>value){if(name=="MemAvailable:"){h.available=std::min(h.total,value*1024);break;}info.ignore(std::numeric_limits<std::streamsize>::max(),'\n');}
#endif
    h.cpus=std::thread::hardware_concurrency();
#ifndef _WIN32
    cpu_set_t allowed;CPU_ZERO(&allowed);if(::sched_getaffinity(0,sizeof(allowed),&allowed)==0)h.cpus=uint32_t(CPU_COUNT(&allowed));
#endif
    Require(h.total&&h.cpus,"Host resource inventory unavailable");return h;
}
uint64_t Reserve(const Host& h,uint32_t reserveMiB) noexcept {return reserveMiB?uint64_t(reserveMiB)*MiB:std::max(512*MiB,h.total/4);}
Plan Admit(uint32_t requested,uint32_t vcpus,bool maximum,const Host& h,Usage u,uint32_t reserveMiB,uint64_t diskBytes) {
    Require(h.total&&h.available<=h.total&&h.cpus,"Invalid host resource inventory");
    Require(vcpus>=1&&vcpus<=4,"Supported SMP range is 1..4 real processors");
    Require(diskBytes<=256*MiB,"Disk checkpoint commitment rejected");
    const auto overhead=WorkerOverhead+4*diskBytes;
    Require(maximum||(requested>=16&&requested<=MaxRamMiB),"RAM must be 16..1024 MiB or maximum");
    Require(!reserveMiB||(reserveMiB>=512&&uint64_t(reserveMiB)*MiB<=h.total),"Host reserve must be automatic or at least 512 MiB within physical RAM");
    const auto reserve=std::max(Reserve(h,reserveMiB),u.reserve);
    Require(u.workers<4&&uint64_t(u.cpus)+vcpus<=h.cpus-(h.cpus>1?1u:0u),"Host CPU/worker reserve exhausted");
    const auto limit=std::min(h.total,h.available);
    Require(reserve<limit&&u.committed<limit-reserve,"Host operational memory reserve exhausted");
    const auto free=limit-reserve-u.committed;
    Require(free>=overhead+16*MiB*RamCopies,"Insufficient host memory for a recoverable VM session");
    const auto admitted=maximum?std::min(MaxRamMiB,(free-overhead)/(MiB*RamCopies)):uint64_t(requested);
    const auto cost=admitted*MiB*RamCopies+overhead;
    Require(cost<=free,"RAM request exceeds host budget including checkpoint/restore overhead");
    return {uint32_t(admitted),vcpus,cost,reserve};
}
namespace {
std::unique_ptr<Lease> Admission(const std::filesystem::path& root) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;){
        auto lease=std::make_unique<Lease>(root/"admission.lock",true);
        if(lease->Held())return lease;
        Require(std::chrono::steady_clock::now()<deadline,"Timed out waiting for resource admission");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
std::filesystem::path Pool() {
    namespace fs=std::filesystem;
#ifdef _WIN32
    wchar_t local[32768]{};const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
    Require(length&&length<32768,"LOCALAPPDATA unavailable");
    const auto p=fs::path(local)/"Guardian"/"microvm-resources-v1";
    fs::create_directories(p);
    const auto attributes=GetFileAttributesW(p.c_str());
    Require(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Resource pool path rejected");
#else
    const auto p=fs::temp_directory_path()/("guardian-resources-"+std::to_string(::getuid()));
    if(::mkdir(p.c_str(),0700)!=0)Require(errno==EEXIST,"Cannot create resource pool");
    struct stat st{};Require(::lstat(p.c_str(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==::getuid()&&!(st.st_mode&0077),"Resource pool ownership rejected");
#endif
    Require(fs::is_directory(fs::symlink_status(p))&&!fs::is_symlink(fs::symlink_status(p)),"Resource pool type rejected");
    return p;
}
Usage Slots(const std::filesystem::path& root,int& vacant) {
    Usage usage;
    for(int i=0;i<16;++i){
        Lease slot(root/(std::to_string(i)+".lock"),true);
        if(slot.Held()){if(vacant<0)vacant=i;continue;}
        const auto data=Open(ReadFile(root/(std::to_string(i)+".gvr"),128));Reader r(data);
        const auto bytes=r.Pod<uint64_t>();const auto cores=r.Pod<uint32_t>();const auto reserve=r.Pod<uint64_t>();r.End();
        Require(bytes<=MaxRamMiB*MiB*RamCopies+WorkerOverhead+4*256*MiB&&cores>=1&&cores<=4,"Resource reservation record rejected");
        Require(reserve>=512*MiB,"Resource reserve record rejected");
        usage.committed+=bytes;usage.cpus+=cores;++usage.workers;usage.reserve=std::max(usage.reserve,reserve);
    }
    return usage;
}
}
Usage Inspect(){const auto root=Pool();auto transaction=Admission(root);int vacant=-1;return Slots(root,vacant);}
Reservation::Reservation(uint32_t ram,uint32_t cpus,uint32_t reserveMiB,uint64_t diskBytes) {
    const auto root=Pool();auto transaction=Admission(root);int vacant=-1;const auto usage=Slots(root,vacant);
    Require(vacant>=0,"Resource pool full");const auto host=Probe();const auto plan=Admit(ram,cpus,false,host,usage,reserveMiB,diskBytes);
    lease_=std::make_unique<Lease>(root/(std::to_string(vacant)+".lock"));
    Writer w;w.Pod(plan.commitment);w.Pod(plan.vcpus);w.Pod(Reserve(host,reserveMiB));AtomicWrite(root/(std::to_string(vacant)+".gvr"),Seal(w.data));
}
}
