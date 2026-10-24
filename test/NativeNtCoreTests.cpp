#include "../guest/NativeServices.h"
#include <iostream>
#include <memory>
#include <vector>
#include <stdexcept>
using namespace guardian;
void Check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    auto m=std::make_unique<memory::Manager>();auto p=std::make_unique<processes::Manager>();auto scheduler=std::make_unique<threads::Scheduler>();auto services=std::make_unique<native::Services>();
    std::vector<uint8_t> ram(64u<<20);abi::MemoryRegion map[]{{0,0x100000,abi::MemoryKind::BootData,0,0},{0x100000,ram.size()-0x100000,abi::MemoryKind::Available,0,0}};
    Check(m->physical.Initialize(reinterpret_cast<uint64_t>(ram.data()),ram.size(),map,2),"Physical initialization");const auto baseline=m->physical.Statistics().free;
    p->Configure(*m,*scheduler);processes::Token token;token.user=100;token.primaryGroup=10;token.integrity=2;uint32_t pid=0;Check(!p->CreateRoot(token,pid),"Root creation");
    const auto tid=scheduler->CreateThread();uint64_t thread=0;Check(!p->Attach(pid,tid,thread),"Thread attach");
    uint64_t scratch=0;Check(!p->Reserve(pid,scratch,8192,memory::ReadWrite)&&!p->Commit(pid,scratch,8192,memory::ReadWrite),"Scratch allocation");
    Check(services->Initialize(*m,*p,*scheduler,pid,nt::Find(1),scratch,{}),"Native initialization");
    Check(scheduler->Advance(1000),"Clock initialization");
    auto put=[&](uint64_t at,uint64_t value){Check(services->Write(at,&value,8),"Scratch write");};
    auto get=[&](uint64_t at){uint64_t value=0;Check(services->Read(at,&value,8),"Scratch read");return value;};
    auto call=[&](native::Op op,std::initializer_list<uint64_t> values){uint64_t args[11]{};uint32_t i=0;for(auto v:values)args[i++]=v;return services->Execute(op,args,tid,scratch,1000,100000000000000000ull);};
    put(scratch,0);put(scratch+8,12345);Check(!call(native::Op::Allocate,{native::CurrentProcess,scratch,0,scratch+8,0x3000,4}),"Native allocation");
    const auto base=get(scratch);Check(get(scratch+8)==16384,"Page rounding");put(scratch+16,77);Check(services->Write(base,&base,8),"Allocated memory writable");
    Check(!call(native::Op::Protect,{native::CurrentProcess,scratch,scratch+8,2,scratch+16})&&!services->Probe(base,8,true)&&get(scratch+16)==4,"Native protection");
    put(scratch+8,0);Check(!call(native::Op::Free,{native::CurrentProcess,scratch,scratch+8,0x8000})&&!services->Probe(base,8,false),"Native release");
    Check(call(native::Op::Allocate,{native::CurrentProcess,0x100000,0,scratch+8,0x3000,4})==native::AccessViolation,"Supervisor pointer accepted");
    put(scratch,0);put(scratch+8,8192);Check(!call(native::Op::Allocate,{native::CurrentProcess,scratch,0,scratch+8,0x2000,4}),"Reserve for unaligned commit");const auto reserved=get(scratch);
    put(scratch,reserved+4095);put(scratch+8,2);Check(!call(native::Op::Allocate,{native::CurrentProcess,scratch,0,scratch+8,0x1000,4})&&get(scratch)==reserved&&get(scratch+8)==8192&&services->Probe(reserved+4096,1,true),"Unaligned commit missed second page");
    put(scratch,reserved+1);put(scratch+8,UINT64_MAX);Check(call(native::Op::Free,{native::CurrentProcess,scratch,scratch+8,0x4000})==native::Invalid,"Overflowing decommit accepted");
    put(scratch,reserved);put(scratch+8,0);Check(!call(native::Op::Free,{native::CurrentProcess,scratch,scratch+8,0x8000}),"Unaligned allocation cleanup");
    put(scratch+32,8192);Check(!call(native::Op::CreateSection,{scratch+24,0xF001F,0,scratch+32,4,0x8000000,0}),"Section create");const auto section=get(scratch+24);
    put(scratch+40,0);put(scratch+48,0);Check(!call(native::Op::MapSection,{section,native::CurrentProcess,scratch+40,0,0,0,scratch+48,2,0,4}),"First view");const auto first=get(scratch+40);
    put(scratch+56,0);put(scratch+64,0);Check(!call(native::Op::MapSection,{section,native::CurrentProcess,scratch+56,0,0,0,scratch+64,2,0,4}),"Second view");const auto second=get(scratch+56);
    put(first,0x12345678);Check(get(second)==0x12345678,"Views do not share physical data");Check(!call(native::Op::Close,{section})&&get(second)==0x12345678,"Close invalidated views");
    Check(!call(native::Op::UnmapSection,{native::CurrentProcess,first})&&get(second)==0x12345678,"Unmap invalidated another view");Check(!call(native::Op::UnmapSection,{native::CurrentProcess,second}),"Last unmap");
    Check(call(native::Op::Close,{section})==native::InvalidHandle,"Stale section handle accepted");
    const auto free=m->physical.Statistics().free;
    for(int budget:{0,1,7}){put(scratch+32,32768);m->physical.Budget(budget);
        const auto status=call(native::Op::CreateSection,{scratch+24,0xF001F,0,scratch+32,4,0x8000000,0});m->physical.Budget(-1);
        Check(status==native::NoMemory&&m->physical.Statistics().free==free,"Section allocation rollback leaked frames");}
    put(scratch+32,8192);Check(!call(native::Op::CreateSection,{scratch+24,0xF001F,0,scratch+32,4,0x8000000,0}),"Fault view section");const auto faultSection=get(scratch+24);
    const auto keeper=m->physical.Statistics().free;put(scratch+40,memory::Arena+0x800000);put(scratch+48,8192);m->physical.Budget(0);
    const auto mappingStatus=call(native::Op::MapSection,{faultSection,native::CurrentProcess,scratch+40,0,0,0,scratch+48,2,0,4});m->physical.Budget(-1);
    Check(mappingStatus==native::NoMemory&&m->physical.Statistics().free==keeper&&!call(native::Op::Close,{faultSection})&&m->physical.Statistics().free==free,"View page-table rollback leaked backing");
    Check(!call(native::Op::CreateEvent,{scratch+72,0x1F0003,0,0,0}),"Event create");const auto event=get(scratch+72);put(scratch+80,0);
    Check(call(native::Op::WaitSingle,{event,0,scratch+80})==native::Timeout,"Unsignaled event wait");Check(!call(native::Op::SetEvent,{event,0})&&!call(native::Op::WaitSingle,{event,0,scratch+80}),"Event signaling");
    Check(!call(native::Op::Close,{event}),"Event close");
    Check(!call(native::Op::CreateEvent,{scratch+72,1,0,0,0}),"Restricted event creation");const auto restricted=get(scratch+72);
    Check(call(native::Op::SetEvent,{restricted,0})==native::Denied&&call(native::Op::WaitSingle,{restricted,0,scratch+80})==native::Denied&&!call(native::Op::Close,{restricted}),"Native event rights escalated");
    native::SyscallMap syscallMap;syscallMap.profile=1;syscallMap.dllHash=1;
    Check(native::ValidMap(syscallMap,1)&&!native::ValidMap(syscallMap,2),"Syscall profile binding");syscallMap.original=1;
    Check(!native::ValidMap(syscallMap,1),"Incomplete original syscall map accepted");
    Check(call(native::Op::Invalid,{})==native::NotImplemented&&services->Unknown()==1,"Unknown native call result");
    Check(services->Exit()&&services->Exit(),"Native cleanup");
    Check(services->Initialize(*m,*p,*scheduler,pid,nt::Find(2),scratch,{})&&services->Calls()==0&&services->Exit(),"Native state reuse");
    Check(!p->Kill(pid,0)&&p->Reap(pid),"Process cleanup");Check(m->physical.Statistics().free==baseline,"Native frame leak");
    std::cout<<"Native NT VM, shared section lifetime, pointer checks, events, unknown calls and cleanup PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
