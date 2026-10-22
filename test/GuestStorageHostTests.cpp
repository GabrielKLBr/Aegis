#include "../guest/Volume.h"
#include "../src/microvm/boot/PersistentBlockStore.h"
#include <fstream>
#include <memory>
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <string>
#include <cstring>
using av::microvm::boot::PersistentBlockStore;
using av::microvm::boot::StorageOptions;
namespace gs=guardian::storage;
namespace {
uint32_t checks=0;
void Check(bool b){++checks;if(!b)throw std::runtime_error("Persistent host check "+std::to_string(checks));}
template<class F>void Reject(F f){bool bad=false;try{f();}catch(const std::runtime_error&){bad=true;}Check(bad);}
bool Blocks(void* p,guardian::io::Op op,uint64_t offset,uint8_t* data) noexcept {auto& s=*static_cast<PersistentBlockStore*>(p);return op==guardian::io::Op::Read?s.Read(offset,data):(op==guardian::io::Op::Write?s.Write(offset,data):s.Flush());}
uint64_t HashFile(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);std::string s((std::istreambuf_iterator<char>(in)),{});return gs::Hash(s.data(),static_cast<uint32_t>(s.size()));}
gs::Command Command(gs::Op op,const char* path=""){gs::Command c{};c.op=op;std::memcpy(c.path,path,std::strlen(path)+1);return c;}
}
int main(int argc,char** argv){try{
    Check(argc==2);const auto root=std::filesystem::absolute(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(root);
    StorageOptions a{root/"base.gvd",root/"a.gvo",true},b{a.base,root/"b.gvo",false};uint8_t data[512]{},read[512]{};data[0]=71;data[511]=29;
    {
        auto store=std::make_unique<PersistentBlockStore>(a);Check(store->Read(0,read)&&!read[0]);Check(store->Write(0,data)&&store->Flush()&&store->Records()==1);
        Reject([&]{PersistentBlockStore locked(a);});Check(!store->Write(UINT64_MAX,data)&&!store->Read(1,read));
        store->FailAfterWrites(0);data[0]=72;Check(!store->Write(0,data)&&store->Read(0,read)&&read[0]==71);
    }
    const auto baseHash=HashFile(a.base);
    {PersistentBlockStore original(a);Check(original.Read(0,read)&&read[0]==71&&read[511]==29);PersistentBlockStore isolated(b);Check(isolated.Read(0,read)&&!read[0]);Check(isolated.Write(512,data)&&isolated.Flush());Check(original.Read(512,read)&&!read[0]);}
    Check(HashFile(a.base)==baseHash);
    {std::ofstream out(a.overlay,std::ios::binary|std::ios::app);out.write("xyz",3);}
    {PersistentBlockStore recovered(a);Check(recovered.RecoveredTailBytes()==3&&recovered.Read(0,read)&&read[0]==71);}
    const auto corrupt=root/"corrupt.gvo";std::filesystem::copy_file(a.overlay,corrupt);{std::ofstream out(corrupt,std::ios::binary|std::ios::app);const char zero[544]{};out.write(zero,sizeof(zero));}
    Reject([&]{PersistentBlockStore bad({a.base,corrupt,false});});
    const auto wrong=root/"wrong.gvd";std::filesystem::copy_file(a.base,wrong);{std::fstream out(wrong,std::ios::binary|std::ios::in|std::ios::out);out.put('x');}
    Reject([&]{PersistentBlockStore bad({wrong,a.overlay,false});});Reject([&]{PersistentBlockStore bad({a.base,a.base,false});});
#if defined(__linux__)
    Reject([&]{PersistentBlockStore bad({"/dev/null",root/"never.gvo",false});});
#else
    Reject([&]{PersistentBlockStore bad({L"\\\\.\\PhysicalDrive0",root/"never.gvo",false});});
#endif
    const auto unknown=root/"unknown.gvo";{std::ofstream out(unknown,std::ios::binary);out<<"not a volume";}
    Reject([&]{PersistentBlockStore bad({a.base,unknown,false});});Check(!std::filesystem::exists(root/"never.gvo"));
    guardian::processes::Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;
    auto scheduler=std::make_unique<guardian::threads::Scheduler>();
    {
        PersistentBlockStore store(b);auto v=std::make_unique<gs::Volume>();v->Configure(&store,Blocks,*scheduler);Check(v->Mount()==gs::Success);
        auto c=Command(gs::Op::Open,"C:\\Persist");c.access=gs::All;c.disposition=1;Check(v->Execute(1,token,c)==0);const auto h=c.handle;
        c=Command(gs::Op::Write);c.handle=h;c.length=1;c.data[0]=99;Check(v->Execute(1,token,c)==0);v->Exit(1);
        c=Command(gs::Op::RegCreate,"HKLM\\Persist");Check(v->Execute(1,token,c)==0);c=Command(gs::Op::RegSet,"HKLM\\Persist");c.type=4;c.length=4;c.data[0]=48;Check(v->Execute(1,token,c)==0);
        Check(v->Flush()==0&&v->Sequence()==1);
        c=Command(gs::Op::Open,"C:\\Persist");c.access=gs::Write;Check(v->Execute(1,token,c)==0);const auto hw=c.handle;
        c=Command(gs::Op::Write);c.handle=hw;c.length=1;c.data[0]=44;Check(v->Execute(1,token,c)==0);v->Exit(1);store.FailAfterWrites(10);Check(v->Flush()==gs::IoError);
    }
    {
        PersistentBlockStore store(b);auto v=std::make_unique<gs::Volume>();v->Configure(&store,Blocks,*scheduler);Check(v->Mount()==0&&v->Sequence()==1);
        auto c=Command(gs::Op::Open,"C:\\Persist");c.access=gs::Read;Check(v->Execute(1,token,c)==0);const auto h=c.handle;c=Command(gs::Op::Read);c.handle=h;c.length=1;Check(v->Execute(1,token,c)==0&&c.data[0]==99);v->Exit(1);
        c=Command(gs::Op::RegGet,"HKLM\\Persist");c.length=512;Check(v->Execute(1,token,c)==0&&c.data[0]==48);
    }
    Check(HashFile(a.base)==baseHash);std::cout<<checks<<" persistent host/overlay checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
