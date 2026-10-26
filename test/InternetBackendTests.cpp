#include "../src/microvm/net/InternetBackend.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace guardian::internet;
using av::microvm::internet_session::Backend;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Client {Backend& backend;uint64_t sequence=0;uint32_t pid=101;uint32_t Run(Request& request){request.pid=pid;request.sequence=++sequence;request.status=0;Check(backend.Exchange(request),"transport rejected a valid request");return request.status;}};
int main(){try{
    Policy offline;Backend denied(offline);Client d{denied};Request random;random.op=Op::Random;random.length=32;Check(!d.Run(random),"offline entropy unavailable");Request resolve;resolve.op=Op::Resolve;std::memcpy(resolve.name,"localhost",10);Check(d.Run(resolve)==10013,"offline DNS policy bypass");
    Policy policy;policy.mode=Mode::Simulated;policy.endpoints=2;for(uint32_t i=0;i<2;++i){auto& e=policy.entries[i];std::memcpy(e.name,i?"http.guardian.test":"echo.guardian.test",i?19:19);e.address[0]=198;e.address[1]=18;e.address[3]=uint8_t(10+i);e.port=i?80:7;e.service=i?1:0;}
    Backend backend(policy);Client c{backend};Request open;open.op=Op::Open;Check(!c.Run(open)&&open.id,"socket create");const auto id=open.id;
    Request stolen;stolen.op=Op::Close;stolen.id=id;c.pid=202;Check(c.Run(stolen)==10038,"socket process ownership bypass");c.pid=101;
    Request connect;connect.op=Op::Connect;connect.id=id;connect.port=7;connect.address[0]=127;connect.address[3]=1;Check(c.Run(connect)==10013,"unlisted loopback destination allowed");std::memcpy(connect.address,policy.entries[0].address,16);Check(!c.Run(connect),"simulated connect");
    Request send;send.op=Op::Send;send.id=id;send.length=5;std::memcpy(send.data,"hello",5);Check(!c.Run(send),"simulated send");Request receive;receive.op=Op::Receive;receive.id=id;receive.length=5;Check(!c.Run(receive)&&receive.length==5&&!std::memcmp(receive.data,"hello",5),"simulated echo payload");
    Request replay;replay.pid=101;replay.sequence=c.sequence;Check(!backend.Exchange(replay),"sequence replay allowed");Request malformed;malformed.pid=101;malformed.sequence=c.sequence+1;malformed.length=Payload+1;Check(!backend.Exchange(malformed),"oversized request allowed");Request exit;exit.op=Op::Exit;Check(!c.Run(exit),"process cleanup");Request stale;stale.op=Op::Close;stale.id=id;Check(c.Run(stale)==10038,"stale socket resurrected");
    Policy invalid=policy;invalid.mode=Mode::Lab;bool rejected=false;try{Backend b(invalid);}catch(const std::exception&){rejected=true;}Check(rejected,"lab accepted a non-loopback endpoint");
    Policy small=policy;small.requestLimit=16;Backend limited(small);Client q{limited};Request socket;socket.op=Op::Open;Check(!q.Run(socket),"quota socket allocation");for(uint32_t i=0;i<16;++i){Request query;query.op=Op::Resolve;std::memcpy(query.name,"echo.guardian.test",19);q.Run(query);}Request close;close.op=Op::Close;close.id=socket.id;Check(!q.Run(close),"quota prevented resource release");
    Request flags;flags.pid=101;flags.sequence=c.sequence+1;flags.flags=1;Check(!backend.Exchange(flags),"input response flags accepted");
    Policy datagrams=policy;datagrams.entries[0].kind=2;Backend udp(datagrams);Client u{udp};Request udpOpen;udpOpen.op=Op::Open;udpOpen.kind=2;Check(!u.Run(udpOpen),"UDP allocation");Request udpConnect;udpConnect.op=Op::Connect;udpConnect.id=udpOpen.id;udpConnect.port=7;std::memcpy(udpConnect.address,datagrams.entries[0].address,16);Check(!u.Run(udpConnect),"UDP connect");
    for(uint32_t n=0;n<2;++n){Request packet;packet.op=Op::Send;packet.id=udpOpen.id;packet.length=3;std::memset(packet.data,int('a'+n),3);Check(!u.Run(packet),"UDP packet queued");}
    Request shortRead;shortRead.op=Op::Receive;shortRead.id=udpOpen.id;shortRead.length=2;Check(u.Run(shortRead)==10040&&shortRead.length==2&&shortRead.data[0]=='a',"UDP truncated packet contract");Request nextRead;nextRead.op=Op::Receive;nextRead.id=udpOpen.id;nextRead.length=8;Check(!u.Run(nextRead)&&nextRead.length==3&&nextRead.data[0]=='b',"UDP boundaries preserved");Request udpExit;udpExit.op=Op::Exit;Check(!u.Run(udpExit)&&udp.Json().find("\"nativeSocketsOpened\":0")!=std::string::npos,"simulated UDP opened host socket");
    Check(backend.Json().find("\"activeSockets\":0")!=std::string::npos,"socket cleanup report");std::cout<<"Internet policy, entropy, ownership, replay, payload, quotas and cleanup PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
