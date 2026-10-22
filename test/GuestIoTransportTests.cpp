#include "../src/microvm/boot/IoBackend.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace guardian::io;
namespace {
uint32_t checks=0;
void Check(bool ok){++checks;if(!ok)throw std::runtime_error("Transport check "+std::to_string(checks));}
}
int main(){try{
    auto b=std::make_unique<Backend>(42);Check(ValidBus(b->Enumeration(),42));auto bus=b->Enumeration();bus.devices[2].capacity=UINT64_MAX;Check(!ValidBus(bus,42));
    uint64_t sequence=0;
    auto packet=[&](uint32_t device,Op op,uint32_t n=512,uint64_t offset=0){return Packet{Magic,Version,sizeof(Packet),42,sequence+1,sequence+1,device,op,offset,n,0,0,0,{}};};
    auto send=[&](Packet& p){const auto request=p;Check(b->Exchange(p));++sequence;Check(ValidResponse(request,p));};
    for(uint32_t kind=0;kind<12;++kind){auto p=packet(3,Op::Write);switch(kind){case 0:p.magic=0;break;case 1:++p.version;break;case 2:--p.bytes;break;case 3:++p.bootId;break;case 4:++p.sequence;break;case 5:p.request=0;break;case 6:p.device=4;break;case 7:p.op=static_cast<Op>(4);break;case 8:p.length=513;break;case 9:p.status=1;break;case 10:p.transferred=1;break;default:p.reserved=1;break;}Check(!b->Exchange(p) && !b->Requests() && !b->BlockTransferred());}
    auto p=packet(3,Op::Write,512,UINT64_MAX);send(p);Check(p.status==Invalid && !b->BlockTransferred());
    p=packet(3,Op::Write);p.data[0]=123;p.data[511]=45;send(p);Check(p.status==Success && p.transferred==512);auto replay=p;replay.status=replay.transferred=0;Check(!b->Exchange(replay));
    p=packet(3,Op::Read);send(p);Check(!p.status && p.data[0]==123 && p.data[511]==45);
    p=packet(3,Op::Flush,0);send(p);Check(!p.status && !p.transferred);
    p=packet(1,Op::Write,3);p.data[0]='o';p.data[1]=0;p.data[2]='k';send(p);Check(p.status==Invalid && !b->ConsoleSize());
    p=packet(1,Op::Write,3);p.data[0]='o';p.data[1]='k';p.data[2]='\n';send(p);Check(!p.status && b->ConsoleSize()==3);
    for(uint32_t i=0;i<MaxRequests;++i){p=packet(2,Op::Write,8);p.data[0]=static_cast<uint8_t>(i);send(p);Check(!p.status);}
    p=packet(2,Op::Write,8);send(p);Check(p.status==Busy);
    p=packet(2,Op::Read,4);send(p);Check(p.status==Small);
    for(uint32_t i=0;i<MaxRequests;++i){p=packet(2,Op::Read,512);send(p);Check(!p.status && p.transferred==8 && p.data[0]==i);}
    p=packet(2,Op::Read,512);send(p);Check(p.status==Busy);
    // Failures are bounded status returns and never overwrite prior block data.
    b->Fail(3);p=packet(3,Op::Write);send(p);Check(p.status==Removed && b->BlockTransferred()==1024);
    auto a=packet(2,Op::Read);auto corrupt=a;corrupt.transferred=513;Check(!ValidResponse(a,corrupt));corrupt=a;corrupt.sequence++;Check(!ValidResponse(a,corrupt));
    std::cout<<checks<<" host I/O boundary checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
