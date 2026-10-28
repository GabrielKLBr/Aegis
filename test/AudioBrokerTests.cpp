#include "../src/microvm/gui/NativeGuiBroker.h"
#include "guardian/Audio.h"
#include <Windows.h>
#include <audioclient.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace a=guardian::audio;namespace g=guardian::nativegdi;namespace u=guardian::nativeuser;
using av::microvm::native_gui::Broker;
namespace {
unsigned checks=0;void Check(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
g::Command C(a::Op op,uint64_t id=0){g::Command c;c.op=static_cast<g::Op>(op);c.dc=id;return c;}
struct Client {Broker broker;uint64_t sequence=0;uint32_t pid=10,tid=20;explicit Client(const char* file):broker(file,1,0,false,true,true,true,true,true){}
g::Packet Call(g::Command draw={},u::Command command={}){g::Packet p;p.boot=1;p.session=broker.Enumeration().session;p.sequence=++sequence;p.pid=pid;p.tid=tid;p.draw=draw;p.command=command;Check(broker.Exchange(p),"audio transport");return p;}
uint64_t New(g::Command c){auto p=Call(c);Check(!p.reply.status&&SUCCEEDED(HRESULT(p.graphics.x))&&p.graphics.value,"audio allocation");return p.graphics.value;}
void Ok(g::Command c){auto p=Call(c);Check(!p.reply.status&&SUCCEEDED(HRESULT(p.graphics.x)),"audio operation");}
void Finish(){u::Command c;c.op=u::Op::Finish;c.flags=1;auto p=Call({},c);Check(!p.reply.status&&p.reply.completed&&!p.graphics.pixelBytes&&!p.graphics.live[1],"audio cleanup");broker.Finish();Check(broker.Completed(),"audio helper exit");}
};
}
int main(int argc,char** argv){try{Check(argc==2,"helper path");g::Command invalid=C(a::Op::Open);Check(!a::Valid(invalid),"format validator");auto open=C(a::Op::Open);open.args[0]=48000;open.args[1]=2;open.args[2]=16;open.args[3]=100;Check(a::Valid(open),"canonical open");invalid=open;invalid.text[0]='x';Check(!a::Valid(invalid),"unexpected bytes denied");
    Client c(argv[1]),other(argv[1]);const auto stream=c.New(open);Check(FAILED(HRESULT(other.Call(C(a::Op::Close,stream)).graphics.x)),"foreign session denied");
    c.pid=11;Check(FAILED(HRESULT(c.Call(C(a::Op::Close,stream)).graphics.x)),"foreign process denied");c.pid=10;
    auto allocate=C(a::Op::Allocate,stream);allocate.count=4096;const auto buffer=c.New(allocate);auto upload=C(a::Op::Upload,buffer);upload.count=512;upload.offset=512;Check(FAILED(HRESULT(c.Call(upload).graphics.x)),"nonsequential upload denied");upload.offset=0;for(unsigned i=0;i<8;++i){upload.offset=i*512;c.Ok(upload);}
    c.Ok(C(a::Op::Pause,stream));auto submit=C(a::Op::Submit,stream);submit.object=buffer;c.Ok(submit);Check(FAILED(HRESULT(c.Call(C(a::Op::Free,buffer)).graphics.x)),"queued memory pinned");c.Ok(C(a::Op::Reset,stream));Check(c.Call(C(a::Op::Poll,buffer)).graphics.value==1,"cancellation done");c.Ok(C(a::Op::Free,buffer));
    std::vector<uint64_t> buffers;allocate.count=a::MaxBuffer;for(unsigned i=0;i<4;++i)buffers.push_back(c.New(allocate));Check(FAILED(HRESULT(c.Call(allocate).graphics.x)),"byte budget");for(auto id:buffers)c.Ok(C(a::Op::Free,id));
    std::vector<uint64_t> streams;for(unsigned i=1;i<4;++i)streams.push_back(c.New(open));Check(FAILED(HRESULT(c.Call(open).graphics.x)),"stream quota");for(auto id:streams)c.Ok(C(a::Op::Close,id));
    c.Ok(C(a::Op::Close,stream));open.flags=1;const auto wasapi=c.New(open);auto pos=c.Call(C(a::Op::Position,wasapi));Check(pos.graphics.rect.right>0,"native WASAPI initialized");c.Ok(C(a::Op::Close,wasapi));c.Finish();other.Finish();
    Client gone(argv[1]);gone.New(open);u::Command exit;exit.op=u::Op::ThreadExit;Check(!gone.Call({},exit).graphics.live[1],"thread cleanup");gone.Finish();
    Client dead(argv[1]);dead.broker.KillForTesting();g::Packet packet;packet.boot=1;packet.session=dead.broker.Enumeration().session;packet.sequence=1;packet.pid=10;packet.tid=20;Check(!dead.broker.Exchange(packet),"helper crash visible");dead.broker.Finish();Check(!dead.broker.Completed(),"crash not success");
    std::printf("AUDIO_BROKER checks=%u PASS\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
