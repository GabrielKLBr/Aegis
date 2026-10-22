#include "../guest/Volume.h"
#include "../src/microvm/state/Resources.h"
#include "../src/microvm/state/StorageBudget.h"
#include "../src/microvm/state/SnapshotDisk.h"
#include "../src/microvm/boot/SessionSnapshot.h"
#include "../src/microvm/boot/PersistentBlockStore.h"
#include "../src/microvm/boot/IoBackend.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
using namespace av::microvm;
namespace {
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,"Invalid resource request accepted");}
void Budgets(){
    using namespace state::resources;
    Host host{16*1024*MiB,12*1024*MiB,8};
    const auto small=Admit(128,1,false,host);Check(small.ramMiB==128&&small.vcpus==1&&small.reserve==4*1024*MiB,"Incorrect admission");
    const auto maximum=Admit(0,1,true,host);Check(maximum.ramMiB==1008,"Maximum mode ignored checkpoint reserve");
    Reject([&]{Admit(1025,1,false,host);});Check(Admit(128,4,false,host).vcpus==4,"SMP admission lost requested processors");Reject([&]{Admit(128,5,false,host);});Reject([&]{Admit(128,4,false,host,{0,4,1});});
    Reject([&]{Admit(128,1,false,host,{0,7,1});});Reject([&]{Admit(128,1,false,host,{0,1,4});});
    Reject([&]{Admit(128,1,false,host,{8*1024*MiB,1,1});});
    Host low{1024*MiB,600*MiB,2};Reject([&]{Admit(16,1,false,low);});
    Host single{4*1024*MiB,3*1024*MiB,1};Check(Admit(32,1,false,single).vcpus==1,"Single CPU host rejected");
    Reject([&]{Admit(32,1,false,single,{0,1,1});});
    Host busy{16*1024*MiB,3*1024*MiB,8};
    Reject([&]{Admit(32,1,false,busy);});
    Check(Admit(32,1,false,busy,{},1024).reserve==1024*MiB,"Explicit safe reserve ignored");
    Reject([&]{Admit(32,1,false,busy,{},511);});
    Usage stronger{};stronger.reserve=4*1024*MiB;
    Reject([&]{Admit(32,1,false,busy,stronger,1024);});
    const auto live=Probe();Check(live.total&&live.available&&live.cpus,"Host inventory unavailable");
}
struct Disk {std::array<uint8_t,guardian::io::BlockBytes> bytes{};uint64_t backingAvailable=UINT64_MAX;};
bool Block(void* raw,guardian::io::Op op,uint64_t offset,uint8_t* data) noexcept {
    auto& d=*static_cast<Disk*>(raw);if(op==guardian::io::Op::Flush)return true;
    if(!data||(offset&511)||offset>d.bytes.size()-512)return false;
    if(op==guardian::io::Op::Write)std::memcpy(d.bytes.data()+offset,data,512);else std::memcpy(data,d.bytes.data()+offset,512);return true;
}
void Text(char* out,const char* text){std::memcpy(out,text,std::strlen(text)+1);}
void VolumeQuota(){
    using namespace guardian;using namespace storage;
    auto disk=std::make_unique<Disk>();auto scheduler=std::make_unique<threads::Scheduler>();auto volume=std::make_unique<Volume>();
    volume->Configure(disk.get(),Block,*scheduler);Check(volume->ConfigureQuota(2048),"Quota configuration failed");
    volume->ConfigureBacking([](void* p,BackingSpace& out) noexcept {out.available=static_cast<Disk*>(p)->backingAvailable;return true;});
    Check(!volume->ConfigureQuota(1000)&&volume->Mount()==Success,"Invalid quota accepted or mount failed");
    processes::Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;
    auto execute=[&](Command& c){return volume->Execute(1,token,c);};
    Command c{};c.op=Op::QuerySpace;Text(c.path,"C:\\");Check(execute(c)==Success,"Volume query failed");Space space{};std::memcpy(&space,c.data,sizeof(space));
    Check(space.capacity==16384&&space.allocated==1536&&space.available==512&&space.free==14848,"Capacity, allocation and quota conflated");
    c={};c.op=Op::Open;Text(c.path,"C:\\one");c.access=All;c.disposition=1;Check(execute(c)==Success,"First quota allocation failed");const auto handle=c.handle;
    Check(volume->QuerySpace().available==0,"Full quota reports writable capacity");
    c={};c.op=Op::Open;Text(c.path,"C:\\two");c.access=All;c.disposition=1;Check(execute(c)==NoSpace,"Full quota accepted allocation");
    c={};c.op=Op::Write;c.handle=handle;c.length=512;c.data[511]=0x71;Check(execute(c)==Success,"Allocated cluster cannot be overwritten");
    disk->backingAvailable=0;
    c={};c.op=Op::Write;c.handle=handle;c.length=512;c.data[511]=0x22;Check(execute(c)==NoSpace,"Physical quota accepted a cached write");
    c={};c.op=Op::Read;c.handle=handle;c.length=512;Check(execute(c)==Success&&c.data[511]==0x71,"Rejected cached write mutated the file");
    disk->backingAvailable=UINT64_MAX;
    c={};c.op=Op::Unlink;Text(c.path,"C:\\one");Check(execute(c)==Success&&volume->QuerySpace().available==0,"Delete-open reclaimed a live allocation");
    c={};c.op=Op::Close;c.handle=handle;Check(execute(c)==Success&&volume->QuerySpace().available==512,"Close did not reclaim deleted allocation");
    Check(volume->Flush()==Success,"Quota state flush failed");const auto checkpoint=*disk;
    volume=std::make_unique<Volume>();volume->Configure(disk.get(),Block,*scheduler);Check(volume->ConfigureQuota(2048)&&volume->Mount()==Success&&volume->QuerySpace().available==512,"Restart lost quota/accounting");
    c={};c.op=Op::Open;Text(c.path,"C:\\after");c.access=All;c.disposition=1;Check(execute(c)==Success&&volume->Flush()==Success,"Post-delete allocation failed");
    volume.reset();*disk=checkpoint;volume=std::make_unique<Volume>();volume->Configure(disk.get(),Block,*scheduler);
    Check(volume->ConfigureQuota(2048)&&volume->Mount()==Success&&volume->QuerySpace().available==512,"Restore lost quota/accounting");
}
void Aggregate(const std::filesystem::path& root){
    namespace fs=std::filesystem;const auto p=fs::absolute(root/("aggregate-"+state::Token()));fs::create_directories(p);
    state::StorageBudget budget(p,1ull<<20);std::vector<uint8_t> large(600*1024,0x71),small(200*1024,0x72);
    state::AtomicWrite(p/"disk.gvo",large);const auto original=state::ReadFile(p/"disk.gvo");
    Reject([&]{state::AtomicWrite(p/"disk.gvo",large);});Check(state::ReadFile(p/"disk.gvo")==original,"Failed replacement destroyed original disk");
    state::AtomicWrite(p/"save.gvs",small);Reject([&]{state::AtomicWrite(p/"second.gvs",small);});
    Check(state::StorageUsage(p).allocated<(1ull<<20),"Snapshots bypassed aggregate storage cap");
    fs::remove(p/"save.gvs");state::AtomicWrite(p/"second.gvs",small);
    Check(state::StorageUsage(p).allocated<(1ull<<20),"Deleting snapshot failed to reclaim quota");
}
void Backing(const std::filesystem::path& root){
    namespace fs=std::filesystem;fs::create_directories(root);
    const auto run=root/state::Token();fs::create_directory(run);
    boot::StorageOptions options{fs::absolute(run/"base.gvd"),fs::absolute(run/"disk.gvo"),true};const auto unit=std::max(uint64_t(4096),state::AllocationUnit(run));options.overlayLimitBytes=unit;
    uint8_t bytes[512]{},out[512]{};bytes[0]=0x71;
    {
        boot::PersistentBlockStore store(options);Check(store.Write(0,bytes)&&store.Write(512,bytes)&&store.Flush(),"Quota rejected writes early");
        while(store.Space().available>=544)Check(store.Write(0,bytes),"Backing admission disagrees with writes");
        const auto before=store.Snapshot();Check(!store.Write(1024,bytes)&&store.LastStatus()==guardian::storage::NoSpace,"Backing full did not return disk full");
        Check(store.Flush()&&store.LastStatus()==0,"Successful flush retained a stale quota error");
        Check(store.Space().available==0&&store.Space().allocated<=options.overlayLimitBytes&&store.Read(1024,out)&&!out[0]&&store.Snapshot()==before,"Rejected write changed disk");
        guardian::io::Backend transport(42);transport.Storage(&store,
            [](void* p,uint64_t o,uint8_t* b) noexcept{return static_cast<boot::PersistentBlockStore*>(p)->Read(o,b);},
            [](void* p,uint64_t o,const uint8_t* b) noexcept{return static_cast<boot::PersistentBlockStore*>(p)->Write(o,b);},
            [](void* p) noexcept{return static_cast<boot::PersistentBlockStore*>(p)->Flush();},
            [](void* p) noexcept{return static_cast<boot::PersistentBlockStore*>(p)->LastStatus();});
        transport.QueryBacking([](void* p,guardian::storage::BackingSpace& s) noexcept{s=static_cast<boot::PersistentBlockStore*>(p)->Space();return true;});
        guardian::io::Packet request{guardian::io::Magic,1,sizeof(request),42,1,1,3,guardian::io::Op::QuerySpace,0,512,0,0,0,{}};const auto sent=request;
        Check(transport.Exchange(request)&&guardian::io::ValidResponse(sent,request)&&!request.status,"Backing query transport failed");
        Check(transport.BlockTransferred()==0,"Capacity metadata counted as disk payload");
        guardian::storage::BackingSpace space{};std::memcpy(&space,request.data,sizeof(space));Check(space.available==0&&space.allocated<=options.overlayLimitBytes,"Wire availability disagrees with write");
        request={guardian::io::Magic,1,sizeof(request),42,2,2,3,guardian::io::Op::Write,1024,512,0,0,0,{}};const auto write=request;
        Check(transport.Exchange(request)&&request.status==guardian::io::NoSpace&&guardian::io::ValidResponse(write,request),"I/O lost disk-full status");
        for(uint32_t reserved:{4u,15u,17u,UINT32_MAX}){
            request={guardian::io::Magic,1,sizeof(request),42,3,3,3,static_cast<guardian::io::Op>(reserved),0,512,0,0,0,{}};
            Check(!transport.Exchange(request)&&transport.Requests()==2,"Reserved operation mutated I/O state");
        }
    }
    {boot::PersistentBlockStore store(options);Check(store.Read(512,out)&&out[0]==0x71&&store.Space().available==0,"Restart lost full backing state");}
    options.overlayLimitBytes+=unit;
    {boot::PersistentBlockStore store(options);Check(store.Write(1024,bytes)&&store.Flush(),"Raising quota did not recover writes");while(store.Space().available>=544)Check(store.Write(0,bytes),"Raised backing admission disagrees");Check(store.Flush()&&store.Space().allocated<=options.overlayLimitBytes,"Backing allocation exceeded limit");}
    options.hostReserveBytes=UINT64_MAX;options.overlayLimitBytes+=unit;
    {boot::PersistentBlockStore store(options);Check(!store.Write(1536,bytes)&&store.LastStatus()==guardian::storage::NoSpace&&store.Space().available==0,"Host reserve ignored");}
}
void StreamingDisk(const std::filesystem::path& root){
    const auto p=std::filesystem::absolute(root/("stream-"+state::Token()));std::filesystem::create_directories(p);
    boot::SessionSnapshot point;point.machine.backend=2;point.machine.ram.resize(16ull<<20);point.machine.access.resize(4096,3);point.machine.processor={1};
    state::Writer metadata;metadata.Pod(uint64_t(42));metadata.Pod(uint64_t(16ull<<20));metadata.Pod(uint64_t(123));point.metadata=metadata.data;point.disk.resize(512,0x71);
    auto encoded=point.Encode();const auto file=p/"point.gvs";state::AtomicWrite(file,encoded);
    Check(state::SnapshotDisk(file,16,123)==point.disk,"Streamed disk differs from sealed snapshot");
    Reject([&]{state::SnapshotDisk(file,32,123);});Reject([&]{state::SnapshotDisk(file,16,124);});
    encoded.back()^=1;state::AtomicWrite(file,encoded);Reject([&]{state::SnapshotDisk(file,16,123);});
    point.disk.resize(5ull<<20,0x73);point.machine.processor.resize(192ull<<10,0x33);encoded=point.Encode();state::AtomicWrite(file,encoded);
    Check(state::SnapshotDisk(file,16,123,16ull<<20)==point.disk,"Scalable disk streaming differs or materializes incompatible bounds");
    Reject([&]{state::SnapshotDisk(file,16,123);});Reject([&]{state::SnapshotDisk(file,16,123,4ull<<20);});
}
}
int main(int argc,char** argv){try{Check(argc==2,"Workspace required");Budgets();VolumeQuota();Aggregate(argv[1]);Backing(argv[1]);StreamingDisk(argv[1]);std::cout<<"Resource reserves, quota exhaustion, wire errors, deletion, restart and restore verified\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
