#include "../guest/MemoryManager.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace guardian::memory;
namespace {
void Check(bool b, const char* why) { if (!b) throw std::runtime_error(why); }
struct Grants {
    int64_t fail = -1; uint64_t calls = 0;
    std::vector<uint32_t> rights;
    static bool Set(void* context, uint64_t page, uint32_t access) noexcept {
        auto& g = *static_cast<Grants*>(context); ++g.calls;
        if (g.fail == 0) { g.fail = -1; return false; }
        if (g.fail > 0) --g.fail;
        if (page / 4096 >= g.rights.size()) return false;
        g.rights[page / 4096] = access; return true;
    }
};
}
int main() {
    try {
        constexpr uint64_t bytes = 16 << 20;
        auto ram = std::make_unique<uint8_t[]>(bytes); auto m = std::make_unique<Manager>();
        const guardian::abi::MemoryRegion map[]{{0,0x100000,guardian::abi::MemoryKind::Reserved,0,0},
            {0x100000,bytes-0x100000,guardian::abi::MemoryKind::Available,0,0}};
        Check(m->physical.Initialize(reinterpret_cast<uint64_t>(ram.get()), bytes, map, 2), "Physical map rejected");
        const auto free = m->physical.Statistics().free;
        Check(free == 3840 && m->physical.Kind(0) == Frame::Unusable && !m->physical.Release(0x100000, Frame::Data), "Reserved frame/free validation");
        Grants grants; grants.rights.resize(bytes / 4096, 3); m->Configure(&grants, Grants::Set, nullptr);
        Space a{}, b{}; Check(m->Create(a) && m->Create(b) && a.root != b.root, "Independent spaces");
        const auto baseline = m->physical.Statistics();
        for (unsigned i=0;i<16;++i) Check(a.privatePages[i]!=b.privatePages[i],"Shared private stack/data frame");
        Space third{}, fourth{}, fifth{};
        Check(m->Create(third)&&m->Create(fourth)&&!m->Create(fifth)&&m->Destroy(third)&&m->Destroy(fourth),"Address-space resource limit");
        uint64_t va = Arena + 7; Information q{}; uint32_t old = 0xDEADBEEF;
        Check(m->ReserveRange(a, va, 8192, ReadWrite) == Status::Success && va == Arena, "64KiB reserve rounding");
        Check(m->Query(a, va, q) == Status::Success && q.state == Reserve && q.bytes == 12288 && q.allocationBase == va, "Reserve query extent");
        Check(m->physical.Statistics().free == baseline.free, "Reserve allocated physical memory");
        Check(m->CommitRange(a, va+1, 4096, ReadWrite) == Status::Success, "Commit page rounding");
        const auto pa = m->Translate(a, va); Check(pa && pa != va && m->Translate(a, va + 4096), "Physical backing missing");
        auto* data = static_cast<uint8_t*>(m->physical.At(pa)); Check(data[0] == 0 && data[4095] == 0, "Commit not zeroed"); data[0] = 83;
        Check(m->CommitRange(a, va, 4096, ReadOnly) == Status::Success && data[0] == 83 && (m->Entry(a, va)&2), "Recommit changed contents/protection");
        Check(m->ProtectRange(a, va, 12288, ReadOnly, old) == Status::NotCommitted && old == 0xDEADBEEF && (m->Entry(a,va)&2), "Partial protect failure changed state");
        grants.fail = 1;
        Check(m->ProtectRange(a, va, 8192, ExecuteRead, old) == Status::NoMemory && grants.rights[pa/4096] == 3 && (m->Entry(a,va)&2), "Permission failure not rolled back");
        Check(m->ProtectRange(a, va, 4096, ReadWrite|Guard, old) == Status::Success && old == ReadWrite && !m->Translate(a,va), "Guard state");
        Check(m->ConsumeGuard(a,va) && !m->ConsumeGuard(a,va) && m->Translate(a,va) == pa, "One-shot guard");
        Check(m->ProtectRange(a, va, 4096, ExecuteRead, old) == Status::Success && grants.rights[pa/4096] == 5, "Guest and host RX grants");
        Check(m->ProtectRange(a, va, 4096, 0x40, old) == Status::Protection, "W+X accepted");
        Check(m->ProtectRange(a, va, 4096, NoAccess, old) == Status::Success && !m->Translate(a,va), "Noaccess still translated");
        Check(m->DecommitRange(a,va,4096) == Status::Success && m->CommitRange(a,va,4096,ReadWrite) == Status::Success, "Decommit/recommit");
        Check(*static_cast<uint8_t*>(m->physical.At(m->Translate(a,va))) == 0, "Released page data leaked");
        Check(m->ReleaseRange(a,va+4096) == Status::NotAllocated && m->ReleaseRange(a,va,4096) == Status::Invalid, "Release boundary validation");
        Check(m->ReleaseRange(a,va) == Status::Success && m->Query(a,va,q) == Status::Success && q.state == Free, "Release query");
        for (int64_t budget = 0; budget < 7; ++budget) {
            uint64_t at = Arena + 0x1F0000;
            Check(m->ReserveRange(a,at,0x15000,ReadWrite) == Status::Success, "OOM reserve");
            // An old committed page must survive partial commit failure intact.
            Check(m->CommitRange(a,at,4096,ReadWrite) == Status::Success, "Old commit");
            const auto oldPage = m->Translate(a,at); *static_cast<uint8_t*>(m->physical.At(oldPage)) = 91;
            const auto before = m->physical.Statistics(); m->physical.Budget(budget);
            Check(m->CommitRange(a,at+0xE000,7*4096,ReadWrite) == Status::NoMemory, "Quota did not fail"); m->physical.Budget(-1);
            const auto after = m->physical.Statistics();
            Check(after.free==before.free && after.tables==before.tables && after.data==before.data &&
                m->Translate(a,at)==oldPage && *static_cast<uint8_t*>(m->physical.At(oldPage))==91, "Commit OOM changed previous state");
            Check(m->ReleaseRange(a,at)==Status::Success, "OOM cleanup");
        }
        // Fail physical permission grants after earlier new pages were sealed RX.
        for (int64_t fail=0;fail<4;++fail) {
            uint64_t pos=0;Check(m->ReserveRange(a,pos,16384,ExecuteRead)==Status::Success,"RX failure reserve");
            grants.fail=fail;
            Check(m->CommitRange(a,pos,16384,ExecuteRead)==Status::NoMemory&&m->physical.Statistics().free==baseline.free,"RX commit failure leaked");
            Check(m->Query(a,pos,q)==Status::Success&&q.state==Reserve&&m->ReleaseRange(a,pos)==Status::Success,"RX rollback changed reservation");
        }
        for (int64_t budget = 0; budget < 21; ++budget) {
            Space s{}; m->physical.Budget(budget); Check(!m->Create(s), "Create quota ignored"); m->physical.Budget(-1);
            Check(!s.root && m->physical.Statistics().free==baseline.free, "Create rollback leaked");
        }
        for (unsigned i=0;i<1000;++i) {
            uint64_t at=0; Check(m->ReserveRange(a,at,(i%31+1)*4096,ReadWrite)==Status::Success, "Cycle reserve");
            Check(m->CommitRange(a,at,(i%31+1)*4096,ReadWrite)==Status::Success && m->ReleaseRange(a,at)==Status::Success, "Cycle commit/free");
        }
        // Exhaust real frames, then require a failed commit to leave its reservation intact.
        std::vector<uint64_t> held;
        for (;;) { const auto p=m->physical.Allocate(Frame::Data); if (!p) break; held.push_back(p); }
        uint64_t at=0; Check(m->ReserveRange(a,at,4096,ReadWrite)==Status::Success && m->CommitRange(a,at,4096,ReadWrite)==Status::NoMemory, "Real physical exhaustion");
        Check(m->Query(a,at,q)==Status::Success && q.state==Reserve && m->ReleaseRange(a,at)==Status::Success, "Exhaustion lost reservation");
        for (auto p:held) Check(m->physical.Release(p,Frame::Data), "Physical release");
        Check(!m->physical.Release(held[0],Frame::Data) && !m->physical.Release(UINT64_MAX,Frame::Data), "Double/overflow free accepted");
        Check(m->physical.Statistics().free==baseline.free, "Cycle/exhaustion leak");
        for (uint64_t invalid:{uint64_t(0),Arena-1,Arena+ArenaBytes,UINT64_MAX}) {
            Check(m->CommitRange(a,invalid,4096,ReadWrite)==Status::Invalid && m->Query(a,invalid,q)==Status::Invalid, "Invalid address admitted");
        }
        Check(m->CommitRange(a,Arena,UINT64_MAX,ReadWrite)==Status::Invalid, "Overflow admitted");
        uint64_t slots[MaxRegions]{};
        for (auto& p:slots) Check(m->ReserveRange(a,p,4096,ReadWrite)==Status::Success,"Region metadata exhausted early");
        at=0; Check(m->ReserveRange(a,at,4096,ReadWrite)==Status::NoMemory && at==0,"Metadata exhaustion changed output");
        for(auto p:slots) Check(m->ReleaseRange(a,p)==Status::Success,"Region release");
        m->Activate(&a); Check(!m->Destroy(a),"Destroyed active CR3"); m->Activate(nullptr);
        Check(m->Destroy(a)&&m->Destroy(b)&&m->physical.Statistics().free==free,"Space teardown leak");
        alignas(16) uint8_t heapBytes[4096]{}; Heap heap;
        Check(heap.Initialize(heapBytes,sizeof(heapBytes)),"Heap init");
        auto* p=static_cast<uint8_t*>(heap.Allocate(33,true)); Check(p&&p[32]==0&&!(reinterpret_cast<uint64_t>(p)&15),"Heap align/zero"); p[0]=88;
        Check(!heap.Reallocate(p,UINT32_MAX)&&p[0]==88,"Failed realloc lost allocation");
        auto* n=static_cast<uint8_t*>(heap.Reallocate(p,512)); Check(n&&n[0]==88&&heap.Release(n)&&!heap.Release(n),"Heap copy/double free");
        auto* full=heap.Allocate(4080); Check(full&&heap.Release(full)&&heap.Used()==0,"Heap did not coalesce");
        Request request{Magic,Version,sizeof(Request),17,1,0x100000,4096,5,0};
        Check(ValidRequest(request,17,1,bytes),"Valid request refused");
        request.access=7; Check(!ValidRequest(request,17,1,bytes),"Host W+X accepted"); request.access=5;
        Check(!ValidRequest(request,18,1,bytes)&&!ValidRequest(request,17,2,bytes),"Session/replay accepted");
        for(unsigned kind=0;kind<8;++kind) {
            auto bad=request;
            if(kind==0) bad.magic=0; if(kind==1) ++bad.version; if(kind==2) bad.bytes=0;
            if(kind==3) bad.gpa=UINT64_MAX; if(kind==4) ++bad.gpa; if(kind==5) bad.length=8192;
            if(kind==6) bad.reserved=1; if(kind==7) bad.access=0;
            Check(!ValidRequest(bad,17,1,bytes),"Malformed request accepted");
        }
        Report report{}; report.magic=Magic;report.version=Version;report.bytes=sizeof(report);report.bootId=17;
        Check(ValidReport(report,17),"Valid report rejected");report.reserved[0]=1;Check(!ValidReport(report,17),"Report reserved bits accepted");
        std::cout<<"Freestanding memory core: isolation, transactional OOM, physical exhaustion, 1000 cycles, heap and protocol PASS\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
