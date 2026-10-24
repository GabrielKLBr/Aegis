#include "../guest/PeNamespace.h"
#include "guardian/Memory.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
using namespace guardian;
namespace {
void Check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
struct Memory {
    static constexpr uint64_t Base=memory::Arena+0x40000;
    struct Slot {std::vector<uint8_t> data;std::vector<pe::Protection> access;};Slot slots[pe::MaxModules];int budget=-1;uint32_t steps=0;
    bool Step(){++steps;if(budget==0){budget=-1;return false;}if(budget>0)--budget;return true;}
    uint32_t Index(uint64_t at,uint32_t bytes){if(at<Base||at-Base>=uint64_t(pe::MaxModules)*pe::MaxImageBytes)return UINT32_MAX;
        const auto i=uint32_t((at-Base)/pe::MaxImageBytes),offset=uint32_t((at-Base)%pe::MaxImageBytes);
        return offset<=slots[i].data.size()&&bytes<=slots[i].data.size()-offset?i:UINT32_MAX;}
    bool Access(uint64_t at,uint32_t bytes,bool write){const auto i=Index(at,bytes);if(i==UINT32_MAX)return false;
        const auto offset=uint32_t((at-Base)%pe::MaxImageBytes);if(!bytes)return true;
        for(uint32_t p=offset/4096;p<=(offset+bytes-1)/4096;++p)if(slots[i].access[p]==pe::Protection::None||(write&&slots[i].access[p]!=pe::Protection::ReadWrite))return false;return true;}
    static bool Map(void* raw,uint64_t at,uint32_t bytes) noexcept {auto& m=*static_cast<Memory*>(raw);if(!m.Step()||at<Base||(at-Base)%pe::MaxImageBytes||bytes>pe::MaxImageBytes)return false;
        const auto i=uint32_t((at-Base)/pe::MaxImageBytes);if(i>=pe::MaxModules||!m.slots[i].data.empty())return false;
        m.slots[i].data.assign(bytes,0);m.slots[i].access.assign(bytes/4096,pe::Protection::ReadWrite);return true;}
    static bool Read(void* raw,uint64_t at,void* out,uint32_t bytes) noexcept {auto& m=*static_cast<Memory*>(raw);if(!m.Step()||!m.Access(at,bytes,false))return false;std::memcpy(out,m.slots[m.Index(at,bytes)].data.data()+(at-Base)%pe::MaxImageBytes,bytes);return true;}
    static bool Write(void* raw,uint64_t at,const void* in,uint32_t bytes) noexcept {auto& m=*static_cast<Memory*>(raw);if(!m.Step()||!m.Access(at,bytes,true))return false;std::memcpy(m.slots[m.Index(at,bytes)].data.data()+(at-Base)%pe::MaxImageBytes,in,bytes);return true;}
    static bool Protect(void* raw,uint64_t at,uint32_t bytes,pe::Protection access) noexcept {auto& m=*static_cast<Memory*>(raw);if(!m.Step()||(at&4095)||(bytes&4095))return false;
        const auto i=m.Index(at,bytes);if(i==UINT32_MAX)return false;const auto offset=uint32_t((at-Base)%pe::MaxImageBytes);
        std::fill(m.slots[i].access.begin()+offset/4096,m.slots[i].access.begin()+(offset+bytes)/4096,access);return true;}
    static bool Unmap(void* raw,uint64_t at) noexcept {auto& m=*static_cast<Memory*>(raw);const auto i=m.Index(at,1);if(i==UINT32_MAX)return false;m.slots[i]={};return true;}
    uint32_t Live() const {uint32_t n=0;for(const auto& s:slots)if(!s.data.empty())++n;return n;}
};
const pe::Backend Backend{Memory::Map,Memory::Read,Memory::Write,Memory::Protect,Memory::Unmap};
}
int main(int argc,char** argv){try{
    Check(argc==2,"Volume path required");std::ifstream stream(argv[1],std::ios::binary);std::vector<uint8_t> volume((std::istreambuf_iterator<char>(stream)),{});
    Check(volume.size()<=pevolume::MaxBytes&&pevolume::Valid(volume.data(),uint32_t(volume.size())),"Volume validation failed");
    const auto* h=reinterpret_cast<const pevolume::Header*>(volume.data());
    pe::Source sources[pe::MaxModules]{};pe::Catalog catalog;uint32_t count=0;
    Check(catalog.Open(volume.data(),uint32_t(volume.size()),h->profile,sources,count)&&count>4,"Catalog does not exceed four modules");
    Memory memory;pe::Loader loader;Check(loader.Configure(Backend,&memory,sources,count,Memory::Base,true),"Configure failed");loader.ConfigureNamespace(catalog.Resolver());uint32_t root=0;
    Check(loader.Load(catalog.RootName(),root),"Large external root failed to load");const auto successfulSteps=memory.steps;
    Check(loader.Get(root)->size>0x20000&&memory.Live()>4,"Image/module limits were not exceeded");
    Check(catalog.VersionMode()==2,"Embedded compatibility manifest was not interpreted");
    for(const char* xml:{"<garbage><assembly/></garbage>","<assembly/><assembly/>","<assembly><supportedOS/></assembly>",
        "<!DOCTYPE assembly><assembly/>","<assembly manifestVersion='2.0'/>","<assembly x='a' x='b'/>","<assembly><file name='host.dll'/></assembly>"}){
        uint32_t mask=0,mode=0;Check(!catalog.Manifest(reinterpret_cast<const uint8_t*>(xml),uint32_t(std::strlen(xml)),mask,mode),"Malformed/unsupported manifest accepted");}
    const auto leaf=catalog.Find("guardian_leaf.dll"),bridge=catalog.Find("guardian-bridge.dll");uint64_t ordinal=0,named=0,forward=0,data=0,forwardData=0;
    Check(catalog.Find("GUARDIAN_LEAF")==leaf,"Case/default DLL extension differs");
    Check(loader.Resolve(leaf,nullptr,7,ordinal)&&loader.Resolve(leaf,"ProbeAlias",0,named)&&ordinal==named,"Ordinal resolution differs");
    Check(loader.Resolve(bridge,"ForwardValue",0,forward)&&forward==named,"Forwarder resolution differs");
    Check(loader.Resolve(leaf,"ProbeData",0,data)&&!loader.Executable(data)&&loader.Resolve(bridge,"ForwardData",0,forwardData)&&data==forwardData,"Data export/forwarder failed");
    uint32_t value=0;Check(Memory::Read(&memory,data,&value,4)&&value==0x12345678,"Data export bytes differ");
    uint32_t bytes=0;uint64_t resource=0;Check(loader.Resource(root,10,101,UINT32_MAX,resource,bytes)&&bytes==4,"Resource lookup failed");
    Check(Memory::Read(&memory,resource,&value,4)&&value==0x56781234,"Resource content differs");
    Check(catalog.Find("..\\guardian_leaf.dll")==UINT32_MAX,"Traversal accepted");
    Check(catalog.Find("D:\\host.dll")==UINT32_MAX,"Host path accepted");
    const auto before=memory.Live();const auto context=catalog.CreateContext("C:\\App\\alternate.manifest");uint64_t cookie=0;
    Check(context&&catalog.Activate(100,context,cookie),"Activation context failed");catalog.CurrentThread(100);
    const auto widget2=catalog.Find("widget.dll");catalog.CurrentThread(101);const auto widget1=catalog.Find("widget.dll");
    Check(widget1!=widget2&&!loader.Get(widget2),"Context leaked to another thread");catalog.CurrentThread(100);
    uint32_t index=0,added=0;Check(loader.Acquire("widget.dll",index,added)&&index==widget2&&added,"Context DLL did not load");
    const auto generation=loader.Get(index)->generation;uint32_t removable=0;Check(loader.Release(index,removable)&&loader.Remove(removable)&&memory.Live()==before,"DLL reference cleanup leaked");
    Check(!catalog.Deactivate(100,cookie+1)&&!catalog.Deactivate(101,cookie),"Invalid/cross-thread cookie accepted");
    Check(catalog.ReleaseContext(context)&&catalog.Deactivate(100,cookie)&&catalog.Find("widget.dll")==widget1,"Context release/restore failed");
    for(int budget:{0,1,2,3,4,8,16,32}){
        const auto context2=catalog.CreateContext("C:\\App\\alternate.manifest");Check(catalog.Activate(100,context2,cookie),"Context setup failed");
        memory.budget=budget;const bool loaded=loader.Acquire("widget.dll",index,added);memory.budget=-1;
        if(loaded){Check(loader.Get(index)->generation>generation,"Generation did not advance");Check(loader.Release(index,removable)&&loader.Remove(removable),"Injected load cleanup failed");}
        Check(memory.Live()==before,"Injected load failure leaked memory");Check(catalog.Deactivate(100,cookie)&&catalog.ReleaseContext(context2),"Injected context cleanup failed");
    }
    Check(loader.UnloadAll()&&!memory.Live(),"Final unload leaked memory");
    for(int budget:{0,1,2,3,4,8,16,32,64}){
        Memory failing;pe::Loader candidate;pe::Catalog scope;uint32_t n=0;Check(scope.Open(volume.data(),uint32_t(volume.size()),h->profile,sources,n),"Scope failed");
        Check(candidate.Configure(Backend,&failing,sources,n,Memory::Base,true),"Candidate failed");candidate.ConfigureNamespace(scope.Resolver());failing.budget=budget;
        if(candidate.Load(scope.RootName(),index))Check(candidate.UnloadAll(),"Successful injected candidate cleanup failed");
        Check(!failing.Live()&&!candidate.MappedMask(),"Transactional initial load leaked");
    }
    for(uint32_t budget:{successfulSteps/2,successfulSteps-1}){
        Memory failing;pe::Loader candidate;pe::Catalog scope;uint32_t n=0;Check(scope.Open(volume.data(),uint32_t(volume.size()),h->profile,sources,n),"Late scope failed");
        Check(candidate.Configure(Backend,&failing,sources,n,Memory::Base,true),"Late candidate failed");candidate.ConfigureNamespace(scope.Resolver());failing.budget=int(budget);
        Check(!candidate.Load(scope.RootName(),index),"Late fault injection did not reach the load transaction");
        Check(!failing.Live()&&!candidate.MappedMask(),"Late injected failure leaked");
    }
    // Replace only the forwarder metadata in an independent in-memory copy.
    // This also exercises malformed ordinal/cyclic/missing targets without
    // relying on the host linker to generate deliberately invalid libraries.
    for(const char* replacement:{"guardian_leaf.#7","guardian_leaf.#99999","missing.Never","guardian-bridge.#4"}){
        pe::Catalog scope;uint32_t n=0;Check(scope.Open(volume.data(),uint32_t(volume.size()),h->profile,sources,n),"Forwarder scope failed");
        const auto i=scope.Find("guardian-bridge.dll");std::vector<uint8_t> copy(sources[i].data,sources[i].data+sources[i].bytes);
        const char original[]="guardian_leaf.ProbeAlias";auto at=std::search(copy.begin(),copy.end(),original,original+sizeof(original));
        Check(at!=copy.end(),"Forwarder fixture missing");
        Check(std::strlen(replacement)<sizeof(original),"Replacement exceeds fixture forwarder");
        std::fill(at,at+sizeof(original),uint8_t(0));std::memcpy(&*at,replacement,std::strlen(replacement));sources[i].data=copy.data();
        Memory isolated;pe::Loader candidate;Check(candidate.Configure(Backend,&isolated,sources,n,Memory::Base,true),"Forwarder candidate failed");candidate.ConfigureNamespace(scope.Resolver());
        const bool accepted=candidate.Load(scope.RootName(),index);
        if(std::strcmp(replacement,"guardian_leaf.#7")==0)Check(accepted,"Ordinal forwarder failed");else Check(!accepted,"Invalid forwarder accepted");
        if(accepted)Check(candidate.UnloadAll(),"Forwarder cleanup failed");Check(!isolated.Live(),"Forwarder rollback leaked");
    }
    auto corrupt=volume;corrupt[48+128]^=1;Check(!pevolume::Valid(corrupt.data(),uint32_t(corrupt.size())),"Corrupt table accepted");
    std::cout<<"External PE loader: >128 KiB, >4 modules, ordinal/data/forwarders/delay, resource/manifest, per-thread activation, refs/generations and fault rollback PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
