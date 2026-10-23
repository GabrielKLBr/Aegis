#include "../guest/Volume.h"
#include "../src/microvm/analysis/Analysis.h"
#include "../src/microvm/boot/SessionSnapshot.h"
#include "../src/microvm/state/Files.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
using namespace av::microvm;
namespace gt=guardian::telemetry;namespace gs=guardian::storage;
namespace {
template<size_t N,size_t M> void Copy(char (&out)[N],const char (&text)[M]) {
    static_assert(M<=N);std::memcpy(out,text,M);
}
analysis::Trace* active=nullptr;uint64_t serial=0;bool accepted=true;
void Exchange(gt::Event& e) noexcept {e.boot=71;e.sequence=++serial;if(e.stage==gt::Stage::Intent)e.operationId=e.sequence;try{if(!active->Accept(e,serial*1000))accepted=false;}catch(...){accepted=false;}}
struct Disk {std::array<uint8_t,65536> bytes{};};
bool Block(void* p,guardian::io::Op op,uint64_t offset,uint8_t* bytes) noexcept {auto& disk=*static_cast<Disk*>(p);if(op==guardian::io::Op::Flush)return true;if(offset>65024)return false;if(op==guardian::io::Op::Read)std::memcpy(bytes,disk.bytes.data()+offset,512);else std::memcpy(disk.bytes.data()+offset,bytes,512);return true;}
gt::Event Sample(gt::Domain d=gt::Domain::File,gt::Operation op=gt::Operation::Write){auto e=gt::Begin(d,op,17,100,"C:\\sample.txt");e.boot=71;e.sequence=e.operationId=1;return e;}
}
int main(int argc,char** argv){try{
    state::Require(argc==2,"Output directory required");const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);uint32_t checks=0;
    const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Analysis core check "+std::to_string(checks));};
    const auto reject=[&](auto action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed);};
    check(gt::ValidPolicy({}));for(uint32_t mask:{1024u,UINT32_MAX}){gt::Policy p{};p.denyMask=mask;check(!gt::ValidPolicy(p));}
    {gt::Policy p{};p.denyMask=p.simulateMask=1;check(!gt::ValidPolicy(p));p={};p.eventLimit=15;check(!gt::ValidPolicy(p));p.eventLimit=8193;check(!gt::ValidPolicy(p));}
    for(uint32_t d=1;d<=10;++d){check(analysis::ParseDomain(analysis::DomainName(gt::Domain(d)))==gt::Domain(d));}
    reject([&]{analysis::ParseDomain("host-shell");});
    {analysis::Trace empty;empty.Configure(71);empty.Complete(true);check(!empty.Coherent());check(empty.Json().find("inconclusive")!=std::string::npos);}
    for(uint32_t invalid=0;invalid<16;++invalid){analysis::Trace trace;trace.Configure(71);auto e=Sample();
        switch(invalid){case 0:e.magic++;break;case 1:e.boot++;break;case 2:e.version++;break;case 3:e.bytes--;break;case 4:e.sequence=0;break;case 5:e.domain=gt::Domain(11);break;case 6:e.operation=gt::Operation(17);break;case 7:e.stage=gt::Stage(3);break;case 8:e.decision=gt::Decision(3);break;case 9:e.reason=gt::Reason(7);break;case 10:e.flags|=16;break;case 11:e.reserved[1]=1;break;case 12:std::memset(e.resource,'a',128);break;case 13:e.secondary[0]=1;break;case 14:e.resource[100]='a';break;case 15:e.beforeBytes=513;break;}
        check(!trace.Accept(e,1)&&trace.Records().empty()&&trace.Rejected()==1);trace.Complete(true);check(!trace.Coherent()&&trace.Json().find("inconclusive")!=std::string::npos);
    }
    {analysis::Trace trace;trace.Configure(71);auto e=Sample();check(trace.Accept(e,100));auto wrong=e;wrong.sequence=2;wrong.stage=gt::Stage::Result;wrong.pid++;check(!trace.Accept(wrong,200));check(trace.Rejected()==1);}
    {analysis::Trace trace;trace.Configure(71);auto e=Sample();check(trace.Accept(e,100));e.sequence=2;e.stage=gt::Stage::Result;check(!trace.Accept(e,99));}
    for(auto decision:{gt::Decision::Allow,gt::Decision::Deny,gt::Decision::Simulate}){
        analysis::Trace trace;gt::Policy p{};if(decision==gt::Decision::Deny)p.denyMask=gt::Bit(gt::Domain::File);if(decision==gt::Decision::Simulate)p.simulateMask=gt::Bit(gt::Domain::File);trace.Configure(71,p);auto e=Sample();check(trace.Accept(e,1)&&e.decision==decision);e.sequence=2;e.stage=gt::Stage::Result;e.status=decision==gt::Decision::Deny?gs::Denied:0;check(trace.Accept(e,2));trace.Complete(true);check(trace.Coherent());
        state::Writer w;trace.Save(w);analysis::Trace restored;state::Reader r(w.data);restored.Load(r,71,p);r.End();check(restored.Json()==trace.Json());auto changed=p;changed.eventLimit--;reject([&]{state::Reader bad(w.data);analysis::Trace other;other.Load(bad,71,changed);});
    }
    {analysis::Trace trace;trace.Configure(71);auto e=Sample();check(trace.Accept(e,1));e.sequence=2;e.stage=gt::Stage::Result;check(trace.Accept(e,2));state::Writer saved;trace.Save(saved);
        const std::string path="C:\\sample.txt";const auto at=std::search(saved.data.begin(),saved.data.end(),path.begin(),path.end());check(at!=saved.data.end());*at='D';reject([&]{analysis::Trace restored;state::Reader r(saved.data);restored.Load(r,71,{});});}
    {analysis::Trace trace;trace.Configure(71);auto e=Sample(gt::Domain::Process,gt::Operation::Create);e.flags|=gt::InvalidText;
        check(trace.Accept(e,1)&&e.decision==gt::Decision::Allow);e.sequence=2;e.stage=gt::Stage::Result;e.status=gs::Invalid;check(trace.Accept(e,2)&&e.reason==gt::Reason::InvalidInput);trace.Complete(true);check(trace.Coherent()&&trace.Json().find("policy-block-v1")==std::string::npos);}
    {analysis::Trace trace;gt::Policy p{};p.simulateMask=gt::Bit(gt::Domain::Process);trace.Configure(71,p);auto e=Sample(gt::Domain::Process,gt::Operation::Create);check(trace.Accept(e,1)&&e.decision==gt::Decision::Deny&&e.reason==gt::Reason::UnsupportedSimulation);}
    {analysis::Trace trace;gt::Policy p{};p.eventLimit=16;trace.Configure(71,p);for(uint64_t i=0;i<20;++i){auto e=Sample();e.sequence=e.operationId=i*2+1;check(trace.Accept(e,i*2+1));e.sequence++;e.stage=gt::Stage::Result;e.status=e.decision==gt::Decision::Deny?gs::Denied:0;check(trace.Accept(e,i*2+2));}trace.Complete(true);check(trace.Dropped()==24&&trace.Records().size()==16&&!trace.Coherent());}
    // Independent examples distinguish an actual correlated pattern from benign writes.
    for(bool patterns:{false,true}){analysis::Trace trace;trace.Configure(71);uint64_t sequence=0;for(uint32_t i=0;i<3;++i){const auto path="C:\\"+std::to_string(i)+".txt";auto e=gt::Begin(gt::Domain::File,gt::Operation::Write,17,100,path.c_str());e.boot=71;e.sequence=e.operationId=++sequence;e.flags|=gt::ExistedBefore;e.oldHash=11;e.beforeBytes=20;check(trace.Accept(e,sequence*1000));e.sequence=++sequence;e.stage=gt::Stage::Result;e.newHash=22;e.afterBytes=21;e.flags|=gt::ExistsAfter;check(trace.Accept(e,sequence*1000));
            if(patterns){auto rename=gt::Begin(gt::Domain::File,gt::Operation::Rename,17,100,path.c_str(),(path+".locked").c_str());rename.boot=71;rename.sequence=rename.operationId=++sequence;check(trace.Accept(rename,sequence*1000));rename.sequence=++sequence;rename.stage=gt::Stage::Result;check(trace.Accept(rename,sequence*1000));}}
        trace.Complete(true);check(trace.Coherent());const auto json=trace.Json();check((json.find("bulk-rewrite-rename-v1")!=std::string::npos)==patterns);check((json.find("\"classification\":\"legitimate\"")!=std::string::npos)==!patterns);
    }
    for(uint32_t negative=0;negative<3;++negative){analysis::Trace trace;trace.Configure(71);uint64_t seq=0;
        for(uint32_t i=0;i<3;++i){const auto path="C:\\"+std::to_string(negative==0?0:i)+".txt";const auto pid=negative==1?i+1:17u;const uint64_t base=negative==2?uint64_t(i)*11000000000ull:uint64_t(i)*10000;
            auto e=gt::Begin(gt::Domain::File,gt::Operation::Write,pid,100,path.c_str());e.boot=71;e.sequence=e.operationId=++seq;e.flags|=gt::ExistedBefore;e.beforeBytes=20;e.oldHash=1;check(trace.Accept(e,base+1));e.sequence=++seq;e.stage=gt::Stage::Result;e.newHash=2;check(trace.Accept(e,base+2));
            e=gt::Begin(gt::Domain::File,gt::Operation::Rename,pid,100,path.c_str(),(path+".locked").c_str());e.boot=71;e.sequence=e.operationId=++seq;check(trace.Accept(e,base+3));e.sequence=++seq;e.stage=gt::Stage::Result;check(trace.Accept(e,base+4));}
        trace.Complete(true);check(trace.Json().find("bulk-rewrite-rename-v1")==std::string::npos);
    }
    {analysis::Trace trace;trace.Configure(71);auto e=Sample();std::memset(e.resource,0,128);gt::Text(e.resource,"<script>alert(1)</script>");check(trace.Accept(e,1));e.sequence=2;e.stage=gt::Stage::Result;check(trace.Accept(e,2));trace.Complete(true);check(trace.Html().find("<script>")==std::string::npos&&trace.Html().find("&lt;script&gt;")!=std::string::npos);}
    {analysis::Trace trace;gt::Policy policy{};policy.eventLimit=gt::MaxEvents;trace.Configure(71,policy);
        for(uint64_t i=0;i<gt::MaxEvents/2;++i){auto e=Sample(gt::Domain::File,gt::Operation::Read);e.sequence=e.operationId=i*2+1;state::Require(trace.Accept(e,i*2+1),"Full trace intent rejected");e.sequence++;e.stage=gt::Stage::Result;state::Require(trace.Accept(e,i*2+2),"Full trace result rejected");}
        state::Writer saved;trace.Save(saved);check(saved.data.size()>(1u<<20)&&saved.data.size()<(5u<<20));analysis::Trace restored;state::Reader r(saved.data);restored.Load(r,71,policy);r.End();check(restored.Records().size()==gt::MaxEvents&&!restored.Dropped());}
    // The same interception point is used by the real guest volume.
    auto volume=std::make_unique<gs::Volume>();auto disk=std::make_unique<Disk>();auto scheduler=std::make_unique<guardian::threads::Scheduler>();guardian::processes::Token token{};token.user=100;token.primaryGroup=10;token.integrity=2;
    volume->Configure(disk.get(),Block,*scheduler);check(volume->Mount()==0);gs::Command c{};c.op=gs::Op::Open;Copy(c.path,"C:\\policy.txt");c.access=gs::All;c.disposition=1;check(volume->Execute(17,token,c)==0);const auto handle=c.handle;c={};c.op=gs::Op::Write;c.handle=handle;c.length=1;c.data[0]='A';check(volume->Execute(17,token,c)==0);check(volume->Flush()==0);const auto original=disk->bytes;
    analysis::Trace trace;gt::Policy p{};p.denyMask=gt::Bit(gt::Domain::File);trace.Configure(71,p);active=&trace;serial=0;accepted=true;gt::exchange=Exchange;
    c={};c.op=gs::Op::Write;c.handle=handle;c.length=1;c.data[0]='B';check(volume->Execute(17,token,c)==gs::Denied);
    c={};c.op=gs::Op::Read;c.handle=handle;c.length=1;check(volume->Execute(17,token,c)==0&&c.data[0]=='A');check(disk->bytes==original);trace.Complete(true);check(trace.Coherent()&&accepted);check(std::strcmp(trace.Records()[0].event.resource,"C:\\policy.txt")==0);
    p={};p.simulateMask=gt::Bit(gt::Domain::File);trace.Configure(71,p);serial=0;c={};c.op=gs::Op::Write;c.handle=handle;c.length=1;c.data[0]='B';check(volume->Execute(17,token,c)==0);
    c={};c.op=gs::Op::Read;c.handle=handle;c.length=1;check(volume->Execute(17,token,c)==0&&c.data[0]=='B');trace.Complete(true);check(trace.Coherent()&&accepted&&trace.Records()[0].event.decision==gt::Decision::Simulate);check(disk->bytes==original);
    gt::exchange=nullptr;volume->Exit(17);
    c={};c.op=gs::Op::RegCreate;Copy(c.path,"HKCU\\Lab\\Run");check(volume->Execute(17,token,c)==0);
    c={};c.op=gs::Op::RegSet;Copy(c.path,"HKCU\\Lab\\Run");Copy(c.name,"MixedCase");c.type=3;c.length=1;c.data[0]='A';check(volume->Execute(17,token,c)==0);
    p={};p.simulateMask=gt::Bit(gt::Domain::Registry);trace.Configure(71,p);serial=0;gt::exchange=Exchange;c.data[0]='B';Copy(c.name,"MIXEDCASE");check(volume->Execute(17,token,c)==0);trace.Complete(true);
    check(trace.Coherent()&&trace.Records().size()==2);const auto changed=trace.Records()[1].event;check(changed.domain==gt::Domain::Registry&&changed.beforeBytes==1&&changed.afterBytes==1&&changed.oldHash!=changed.newHash&&(changed.flags&6)==6);
    p={};p.denyMask=gt::Bit(gt::Domain::Registry);trace.Configure(71,p);serial=0;c={};c.op=gs::Op::RegSet;Copy(c.path,"HKCU\\Lab\\Run");Copy(c.name,"MixedCase");c.type=3;c.length=1;c.data[0]='C';check(volume->Execute(17,token,c)==gs::Denied);
    c={};c.op=gs::Op::RegGet;Copy(c.path,"HKCU\\Lab\\Run");Copy(c.name,"mixedcase");c.length=512;check(volume->Execute(17,token,c)==0&&c.data[0]=='B');trace.Complete(true);check(trace.Coherent());gt::exchange=nullptr;
    // Corrupted serialized guest inputs cannot grow the bounded result set.
    for(uint32_t i=0;i<1024;++i){analysis::Trace fuzz;fuzz.Configure(71);auto e=Sample();auto* bytes=reinterpret_cast<uint8_t*>(&e);bytes[(i*37)%sizeof(e)]^=uint8_t(1+(i%255));fuzz.Accept(e,i);check(fuzz.Records().size()<=1);}
    state::AtomicText(output/"core.json","{\"status\":\"PASS\",\"phase\":13,\"checks\":"+std::to_string(checks)+",\"realVolumePolicy\":true,\"untrustedInputs\":true}\n");std::cout<<checks<<" input, loss, classification, snapshot and actual guest volume policy checks PASS\n";return 0;
}catch(const std::exception& e){gt::exchange=nullptr;std::cerr<<e.what()<<'\n';return 1;}}
