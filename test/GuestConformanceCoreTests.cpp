#include "../guest/RuntimeMemory.h"
#include "guardian/Conformance.h"
#include "guardian/SystemClock.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
namespace {
uint32_t checks=0;
void Check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
void Clock() {
    namespace t=guardian::time;namespace c=guardian::cpu;
    const c::Clock source{c::ClockMagic,c::Version,sizeof(c::Clock),2,42,1'000'000,1,
                          t::UnixEpoch100ns+1'000'000,t::Frequency};
    t::Sample sample{};Check(t::SampleAt(source,42,2'000'000,sample),"Valid clock rejected");
    Check(sample.counter==20000 && sample.frequency==t::Frequency && sample.utc100ns==source.utcEpoch100ns+20000,"Clock units diverge");
    const volatile c::Clock published=source;
    Check(t::ReadPublished(published,42,sample) && sample.counter==10000,"Published clock sample");
    auto odd=source;odd.sequence|=1;const auto preserved=sample;
    Check(!t::ReadPublished(odd,42,sample) && !std::memcmp(&sample,&preserved,sizeof(sample)),"Odd publication accepted");
    for(uint32_t field=0;field<10;++field) {
        auto clock=source;auto now=2'000'000ull;
        switch(field) {
        case 0:clock.magic=0;break;case 1:++clock.version;break;case 2:--clock.bytes;break;
        case 3:++clock.bootId;break;case 4:++clock.sequence;break;case 5:clock.performanceFrequency=0;break;
        case 6:clock.utcEpoch100ns=t::UnixEpoch100ns-1;break;case 7:now=source.monotonicNs-1;break;
        case 8:clock.utcEpoch100ns=UINT64_MAX;break;default:clock.performanceFrequency*=2;break;
        }
    const auto before=sample;Check(!t::SampleAt(clock,42,now,sample),"Malformed clock accepted");
        Check(std::memcmp(&before,&sample,sizeof(sample))==0,"Rejected clock modified output");
    }
    uint64_t deadline=0;Check(t::MillisecondDeadline(100,40,deadline)&&deadline==40'000'100,"Delay conversion");
    Check(t::MillisecondDeadline(UINT64_MAX-2,0,deadline)&&deadline==UINT64_MAX-2,"Zero delay boundary");
    Check(!t::MillisecondDeadline(UINT64_MAX-1'000'000,1,deadline),"Never sentinel became a finite deadline");
    Check(!t::MillisecondDeadline(UINT64_MAX-10,UINT32_MAX,deadline),"Delay overflow accepted");
}
void Memory() {
    using namespace guardian;
    auto memory=std::make_unique<memory::Manager>();auto processes=std::make_unique<processes::Manager>();
    auto scheduler=std::make_unique<threads::Scheduler>();
    constexpr uint64_t bytes=16ull<<20;auto ram=std::make_unique<uint8_t[]>(bytes);
    const abi::MemoryRegion map[]={{0,0x100000,abi::MemoryKind::Reserved,0,0},
                                  {0x100000,bytes-0x100000,abi::MemoryKind::Available,0,0}};
    Check(memory->physical.Initialize(reinterpret_cast<uint64_t>(ram.get()),bytes,map,2),"Physical memory");
    memory::Space idle{};Check(memory->Create(idle),"Idle space");memory->Activate(&idle);processes->Configure(*memory,*scheduler);
    processes::Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;uint32_t pid=0;
    Check(!processes->CreateRoot(token,pid),"Create owner");
    runtime::VirtualMemory api;runtime::Command command{};
    const auto execute=[&]() {return api.Execute(*processes,*memory,pid,command);};
    const auto request=[&](runtime::Op op,uint64_t handle=0) {command={};command.op=op;command.handle=handle;};
    const auto free=memory->physical.Statistics().free;
    request(runtime::Op::MemoryAllocate);command.flags=0x3000;command.argument=4096;command.access=memory::Guard|memory::ReadWrite;
    Check(execute()==storage::Unsupported,"Unsupported guard semantics advertised as complete");
    request(runtime::Op::MemoryAllocate);command.flags=0x3000;command.argument=4096;command.access=memory::ExecuteRead;
    Check(execute()==storage::Unsupported,"Unsupported JIT semantics advertised as complete");
    request(runtime::Op::MemoryAllocate);command.flags=0x3000;command.argument=8192;command.access=4;
    memory->physical.Budget(0);Check(execute()!=0,"Allocation injection accepted");memory->physical.Budget(-1);
    Check(memory->physical.Statistics().free==free && !processes->Get(pid)->committed,"Allocation rollback leaked");
    request(runtime::Op::MemoryAllocate);command.flags=0x3000;command.argument=4096;command.access=4;command.status=1;
    Check(execute()==storage::Invalid,"Malformed memory envelope accepted");
    request(runtime::Op::MemoryAllocate);command.flags=0x3000;command.argument=8192;command.access=4;
    Check(!execute(),"Virtual allocation");const auto base=command.handle;Check(!(base&65535),"Allocation granularity");
    for(uint32_t offset=0;offset<8192;++offset) {
        const auto physical=processes->Translate(pid,base+offset,true);
        Check(physical && *static_cast<uint8_t*>(memory->physical.At(physical))==0,"Committed bytes not zero");
    }
    request(runtime::Op::MemoryProtect,base+1);command.argument=4096;command.access=2;
    Check(!execute() && command.transferred==4,"Protection rounding/old value");
    Check(!processes->Translate(pid,base,true) && !processes->Translate(pid,base+4096,true),"Protection missed intersecting page");
    request(runtime::Op::MemoryQuery,base);command.length=sizeof(memory::Information);Check(!execute(),"Query");
    memory::Information info{};std::memcpy(&info,command.data,sizeof(info));
    Check(info.bytes==8192 && info.protect==2 && info.allocationProtect==4,"Protection/metadata disagree");
    uint64_t loader=0;Check(!processes->Reserve(pid,loader,4096,4)&&!processes->Commit(pid,loader,4096,4),"Owned loader region");
    request(runtime::Op::MemoryFree,loader);command.flags=0x8000;Check(execute()!=0,"User freed loader allocation");
    request(runtime::Op::MemoryProtect,loader);command.argument=4096;command.access=2;Check(execute()!=0,"User changed loader permissions");
    Check(processes->Translate(pid,loader,true)!=0 && !processes->Release(pid,loader),"Loader allocation changed");
    request(runtime::Op::MemoryFree,base);command.flags=0x8000;Check(!execute(),"Release");
    request(runtime::Op::MemoryFree,base);command.flags=0x8000;Check(execute()==static_cast<uint32_t>(memory::Status::NotAllocated),"Stale release");
    uint64_t allocations[8]{};
    for(auto& allocation:allocations){request(runtime::Op::MemoryAllocate);command.flags=0x3000;command.argument=4096;command.access=4;Check(!execute(),"Bounded allocation capacity");allocation=command.handle;}
    request(runtime::Op::MemoryAllocate);command.flags=0x3000;command.argument=4096;command.access=4;
    const auto committed=processes->Get(pid)->committed;Check(execute()==storage::Quota && committed==processes->Get(pid)->committed,"Allocation quota changed state");
    for(auto allocation:allocations){request(runtime::Op::MemoryFree,allocation);command.flags=0x8000;Check(!execute(),"Quota release");}
    Check(memory->physical.Statistics().free==free && !processes->Get(pid)->committed,"Runtime memory leak");
    Check(!processes->Kill(pid,0)&&processes->Reap(pid),"Owner teardown");
    memory->Activate(nullptr);Check(memory->Destroy(idle),"Idle teardown");
}
void Catalog() {
    using namespace guardian::conformance;
    std::set<std::string> names,domains;
    for(const auto& c:Cases){Check(names.insert(c.name).second,"Duplicate case identity");domains.insert(c.domain);}
    Check(CaseCount>=50,"Incomplete case matrix");for(const auto* domain:Domains)Check(domains.contains(domain),"Missing conformance domain");
}
}
int main() {try{Clock();Memory();Catalog();std::cout<<checks<<" clock, ownership, memory rollback and conformance catalog checks PASS\n";return 0;}
catch(const std::exception& error){std::cerr<<"Check "<<checks<<": "<<error.what()<<'\n';return 1;}}
