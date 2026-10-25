#include "../guest/NativeServices.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace guardian;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Block(void* raw,io::Op op,uint64_t offset,uint8_t* data) noexcept {auto& disk=*static_cast<std::array<uint8_t,65536>*>(raw);if(op==io::Op::Flush)return true;if(!data||(offset&511)||offset>disk.size()-512)return false;
    for(uint32_t i=0;i<512;++i)if(op==io::Op::Read)data[i]=disk[size_t(offset)+i];else if(op==io::Op::Write)disk[size_t(offset)+i]=data[i];else return false;return true;}
int main(){try{
    auto memory=std::make_unique<memory::Manager>();auto processes=std::make_unique<processes::Manager>();auto scheduler=std::make_unique<threads::Scheduler>();
    std::vector<uint8_t> ram(64u<<20);abi::MemoryRegion map[]{{0,0x100000,abi::MemoryKind::BootData,0,0},{0x100000,ram.size()-0x100000,abi::MemoryKind::Available,0,0}};
    Check(memory->physical.Initialize(reinterpret_cast<uint64_t>(ram.data()),ram.size(),map,2),"RAM init");const auto free=memory->physical.Statistics().free;processes->Configure(*memory,*scheduler);
    auto volume=std::make_unique<storage::Volume>();auto disk=std::make_unique<std::array<uint8_t,65536>>();volume->Configure(disk.get(),Block,*scheduler);Check(!volume->Mount(),"Shared volume mount");
    auto first=std::make_unique<native::Services>();auto second=std::make_unique<native::Services>();native::Services* services[]{first.get(),second.get()};uint32_t pids[2]{},tids[2]{};uint64_t scratch[2]{};
    processes::Token token;token.user=100;token.primaryGroup=10;token.integrity=2;
    for(uint32_t i=0;i<2;++i){Check(!processes->CreateRoot(token,pids[i]),"Process create");tids[i]=scheduler->CreateThread();uint64_t h=0;Check(!processes->Attach(pids[i],tids[i],h),"Thread attach");
        Check(!processes->Reserve(pids[i],scratch[i],8192,memory::ReadWrite)&&!processes->Commit(pids[i],scratch[i],8192,memory::ReadWrite),"Scratch");
        Check(services[i]->Initialize(*memory,*processes,*scheduler,pids[i],nt::Find(1),scratch[i],{},volume.get()),"Shared native service");}
    auto put=[&](uint32_t i,uint64_t at,const void* data,uint32_t bytes){Check(services[i]->Write(at,data,bytes),"Write user data");};
    auto call=[&](uint32_t i,native::Op op,std::initializer_list<uint64_t> list){uint64_t args[11]{};uint32_t n=0;for(auto v:list)args[n++]=v;return services[i]->Execute(op,args,tids[i],scratch[i],1000,100000000000000000ull);};
    auto get=[&](uint32_t i,uint64_t at){uint64_t value=0;Check(services[i]->Read(at,&value,8),"Read user result");return value;};
    for(uint32_t i=0;i<2;++i){const char16_t path[]=u"\\??\\C:\\shared.bin";put(i,scratch[i]+512,path,sizeof(path));native::Unicode name{uint16_t(sizeof(path)-2),uint16_t(sizeof(path)),0,scratch[i]+512};put(i,scratch[i]+256,&name,sizeof(name));
        native::Attributes attrs;attrs.name=scratch[i]+256;attrs.attributes=0x40;put(i,scratch[i]+128,&attrs,sizeof(attrs));
        Check(!call(i,native::Op::CreateFile,{scratch[i],0xC0000000,scratch[i]+128,scratch[i]+64,0,0,7,i?1ull:3ull,0x60,0,0}),"Shared file open");}
    const auto a=get(0,scratch[0]),b=get(1,scratch[1]);uint64_t content=0x1434;put(0,scratch[0]+1024,&content,8);int64_t zero=0;put(0,scratch[0]+1100,&zero,8);put(1,scratch[1]+1100,&zero,8);
    Check(!call(0,native::Op::WriteFile,{a,0,0,0,scratch[0]+64,scratch[0]+1024,8,scratch[0]+1100,0}),"Parent file write");
    Check(!call(1,native::Op::ReadFile,{b,0,0,0,scratch[1]+64,scratch[1]+1024,8,scratch[1]+1100,0})&&get(1,scratch[1]+1024)==content,"Cross-process file data");
    uint64_t maximum=8;put(0,scratch[0]+1200,&maximum,8);Check(!call(0,native::Op::CreateSection,{scratch[0]+16,0xF001F,0,scratch[0]+1200,4,0x8000000,a}),"File section create");const auto section=get(0,scratch[0]+16);uint64_t base=0,size=0;put(0,scratch[0]+24,&base,8);put(0,scratch[0]+32,&size,8);
    Check(!call(0,native::Op::MapSection,{section,native::CurrentProcess,scratch[0]+24,0,0,0,scratch[0]+32,2,0,4}),"File view map");base=get(0,scratch[0]+24);Check(get(0,base)==content,"File view initial content");content=0xBEEF;put(0,base,&content,8);
    Check(!call(0,native::Op::Close,{section})&&!call(0,native::Op::Close,{a}),"Backing lifetime after handle close");uint64_t extra[8]{base,8};Check(!first->Win32(4,extra),"Mapped file flush");
    Check(!call(1,native::Op::ReadFile,{b,0,0,0,scratch[1]+64,scratch[1]+1024,8,scratch[1]+1100,0})&&get(1,scratch[1]+1024)==content,"Mapped writes visible to another process");
    Check(!call(0,native::Op::UnmapSection,{native::CurrentProcess,base}),"View cleanup");
    uint64_t remote=0;Check(!processes->OpenProcess(pids[0],pids[1],processes::GenericAll,remote),"Foreign process test handle");uint64_t bytes=4096;put(0,scratch[0]+24,&zero,8);put(0,scratch[0]+32,&bytes,8);
    Check(call(0,native::Op::Allocate,{remote,scratch[0]+24,0,scratch[0]+32,0x3000,4})==native::Denied,"Foreign allocation allowed");
    Check(!first->Probe(0x100000,1,false),"Supervisor pointer accessible");Check(first->Win32(999,extra)==native::NotImplemented,"Unknown Win32 helper accepted");
    Check(first->Exit()&&second->Exit()&&!volume->Unmount(),"Shared volume cleanup");for(uint32_t i=0;i<2;++i)Check(!processes->Kill(pids[i],0)&&processes->Reap(pids[i]),"Process reclaim");
    Check(memory->physical.Statistics().free==free,"Frame leak");std::cout<<"Win32 shared filesystem, mapped backing lifetime, process isolation and complete reclaim PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
