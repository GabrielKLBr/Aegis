#include "../src/microvm/boot/KernelBoot.h"
#include "../src/microvm/state/Files.h"
#include "../src/microvm/state/SnapshotDisk.h"
#include <cstring>
#include <iostream>
using namespace av::microvm;
namespace {
void Check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,"Invalid quota checkpoint accepted");}
void Verify(const boot::KernelImage& image,const std::filesystem::path& root,bool modern){
    namespace fs=std::filesystem;const auto p=root/state::Token();fs::create_directories(p);
    boot::StorageOptions storage{fs::absolute(p/"base.gvd"),fs::absolute(p/"disk.gvo"),true};
    boot::CpuOptions cpu{};cpu.enabled=true;cpu.processes=true;cpu.io=true;cpu.storage=true;
    boot::SessionSnapshot point;
    {boot::KernelBootSession original;original.Prepare(image,16ull<<20,42,cpu,storage);point=original.Capture();}
    state::AtomicWrite(p/"point.gvs",point.Encode());
    Check(state::SnapshotDisk(p/"point.gvs",16,state::Hash(image.FileBytes()))==point.disk,"Streaming real checkpoint changed disk data");
    const auto gpa=guardian::storage::QuotaGpa;
    guardian::storage::QuotaInfo quota{};std::memcpy(&quota,point.machine.ram.data()+gpa,sizeof(quota));
    Check((quota.magic==guardian::storage::QuotaMagic)==modern,"Imported kernel quota declaration disagrees with boot");
    {boot::KernelBootSession restored;restored.Prepare(image,16ull<<20,42,cpu,storage);restored.Restore(point);const auto result=restored.Run(std::chrono::seconds(15));Check(result.success,"Restored reference storage kernel failed");}
    // Restore the original disk before validating candidate transactions.
    state::AtomicWrite(storage.overlay,point.disk);
    if(modern){
        auto wrong=point;wrong.machine.access[gpa/4096]=3;
        Reject([&]{boot::KernelBootSession candidate;candidate.Prepare(image,16ull<<20,42,cpu,storage);candidate.Restore(wrong);});
        wrong=point;reinterpret_cast<guardian::storage::QuotaInfo*>(wrong.machine.ram.data()+gpa)->allocationLimit=8192;
        Reject([&]{boot::KernelBootSession candidate;candidate.Prepare(image,16ull<<20,42,cpu,storage);candidate.Restore(wrong);});
        auto configured=storage;configured.volumeQuotaBytes=8192;
        Reject([&]{boot::KernelBootSession candidate;candidate.Prepare(image,16ull<<20,42,cpu,configured);candidate.Restore(point);});
    }else{
        storage.volumeQuotaBytes=8192;
        Reject([&]{boot::KernelBootSession candidate;candidate.Prepare(image,16ull<<20,42,cpu,storage);});
    }
}
}
int main(int argc,char** argv){try{
    Check(argc==3||argc==4,"Current storage kernel, output and optional legacy kernel required");
    const auto current=boot::KernelImage::Read(argv[1]);Check(current.SupportsStorageQuota(),"Current kernel lacks quota capability");Verify(current,argv[2],true);
    if(argc==4){const auto legacy=boot::KernelImage::Read(argv[3]);Check(!legacy.SupportsStorageQuota(),"Legacy fixture is not legacy");Verify(legacy,argv[2],false);}
    std::cout<<"Real storage boot/restore, quota binding and imported kernel compatibility verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
