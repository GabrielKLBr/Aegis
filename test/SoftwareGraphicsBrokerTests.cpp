#include "../src/microvm/gui/NativeGuiBroker.h"
#include "guardian/SoftwareGraphics.h"
#include <Windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstring>
namespace f=guardian::softwaregraphics;namespace g=guardian::nativegdi;namespace u=guardian::nativeuser;
using av::microvm::native_gui::Broker;
namespace {
unsigned checks=0;void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int32_t Word(float value){int32_t out;std::memcpy(&out,&value,4);return out;}
g::Command C(f::Op op,uint64_t id=0){g::Command c;c.op=static_cast<g::Op>(op);c.dc=id;return c;}
struct Client {
    Broker broker;uint64_t sequence=0;uint32_t pid=10,tid=20;
    explicit Client(const std::filesystem::path& file):broker(file,1,0,false,true,true,true,true){}
    g::Packet Call(g::Command draw={},u::Command command={}){g::Packet p;p.boot=1;p.session=broker.Enumeration().session;p.sequence=++sequence;p.pid=pid;p.tid=tid;p.draw=draw;p.command=command;Check(broker.Exchange(p),"Software broker exchange");Check(p.reply.restrictions==31,"Restricted software helper");return p;}
    uint64_t Window(){u::Command c;c.op=u::Op::Create;c.kind=u::Class::Custom;c.style=WS_POPUP|WS_VISIBLE;c.rect={120,120,440,360};auto p=Call({},c);Check(!p.reply.status&&p.reply.value,"Owned window");return p.reply.value;}
    uint64_t New(g::Command c){const auto p=Call(c);Check(!p.reply.status&&p.graphics.x==S_OK&&p.graphics.value,"Software object creation");return p.graphics.value;}
    void Ok(g::Command c){const auto p=Call(c);Check(!p.reply.status&&p.graphics.x==S_OK,"Software operation success");}
    void Free(uint64_t id){Ok(C(f::Op::Free,id));}
    void Finish(){u::Command c;c.op=u::Op::Finish;c.flags=1;const auto p=Call({},c);Check(p.reply.completed&&!p.reply.status&&p.graphics.live[0]==0&&p.graphics.live[1]==0&&p.graphics.live[2]==0&&!p.graphics.pixelBytes,"All native resources reclaimed");broker.Finish();Check(broker.Completed(),"Native software worker clean exit");}
};
void TwoD(Client& a,uint64_t window){
    const auto target=a.New(C(f::Op::Target,window));a.Ok(C(f::Op::Begin,target));auto clear=C(f::Op::Clear,target);for(unsigned i=0;i<4;++i)clear.args[i]=Word(1);a.Ok(clear);
    auto c=C(f::Op::Brush,target);c.args[0]=c.args[3]=Word(1);const auto brush=a.New(c);
    c=C(f::Op::Rectangle,target);c.object=brush;c.flags=1;c.args[0]=c.args[1]=Word(10);c.args[2]=c.args[3]=Word(50);a.Ok(c);
    auto formatCall=C(f::Op::Format);const wchar_t face[]=L"Segoe UI",locale[]=L"pt-BR";
    std::memcpy(formatCall.font.face,face,sizeof(face));std::memcpy(formatCall.text,locale,sizeof(locale));formatCall.count=5;formatCall.flags=400;formatCall.mode=5;formatCall.args[0]=Word(16);
    const auto format=a.New(formatCall);c=C(f::Op::Text,target);c.object=brush;c.other=format;c.text[0]='A';c.count=1;c.args[0]=Word(70);c.args[1]=Word(10);c.args[2]=Word(110);c.args[3]=Word(60);a.Ok(c);
    a.Ok(C(f::Op::End,target));g::Command dc;dc.op=g::Op::Dc;dc.dc=window;const auto hdc=a.Call(dc).graphics.value;Check(hdc!=0,"Owned GDI inspection DC");
    dc={};dc.op=g::Op::Pixel;dc.dc=hdc;dc.args[0]=dc.args[1]=20;Check(a.Call(dc).graphics.value==RGB(255,0,0),"Direct2D software output is visible in the owned native surface");
    dc.op=g::Op::Release;dc.args[0]=dc.args[1]=0;Check(!a.Call(dc).reply.status,"Inspection DC released");
    const auto before=a.Call(C(f::Op::Stats)).graphics.selected[1];c=C(f::Op::Rectangle,target);c.object=brush;
    Check(a.Call(c).graphics.x==D2DERR_WRONG_STATE,"Drawing outside Begin/End is a real failure");Check(a.Call(C(f::Op::Stats)).graphics.selected[1]>before,"Failures are counted");
    a.Ok(C(f::Op::Begin,target));c=C(f::Op::Clip,target);c.args[2]=c.args[3]=Word(8);a.Ok(c);Check(FAILED(a.Call(C(f::Op::End,target)).graphics.x),"Unbalanced clip is not reported as success");
    a.tid=21;Check(a.Call(C(f::Op::Begin,target)).reply.status==ERROR_INVALID_HANDLE,"Thread ownership");a.tid=20;
    a.Free(format);a.Free(brush);a.Free(target);
}
void Images(Client& a){
    BITMAPFILEHEADER file{};file.bfType=0x4D42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+16;
    BITMAPINFOHEADER header{};header.biSize=sizeof(header);header.biWidth=header.biHeight=2;header.biPlanes=1;header.biBitCount=24;header.biSizeImage=16;
    std::vector<uint8_t> bytes(file.bfSize);std::memcpy(bytes.data(),&file,sizeof(file));std::memcpy(bytes.data()+sizeof(file),&header,sizeof(header));
    const uint8_t pixels[]{0,0,255,0,255,0,0,0,255,0,0,255,255,255,0,0};std::memcpy(bytes.data()+file.bfOffBits,pixels,16);
    auto c=C(f::Op::Stream);c.count=uint32_t(bytes.size());const auto stream=a.New(c);c=C(f::Op::Write,stream);c.count=uint32_t(bytes.size());std::memcpy(c.pixels,bytes.data(),bytes.size());a.Ok(c);
    const auto frame=a.New(C(f::Op::Decode,stream));a.Free(stream);auto info=a.Call(C(f::Op::SourceInfo,frame));Check(info.graphics.rect.right==2&&info.graphics.rect.bottom==2,"WIC decodes bounded BMP dimensions");
    c=C(f::Op::Convert,frame);c.flags=1;const auto converted=a.New(c);a.Free(frame);c=C(f::Op::Read,converted);c.count=16;
    const auto p=a.Call(c);Check(!p.reply.status&&p.graphics.x==S_OK&&p.graphics.value==16,"Converted pixels copied through the bounded wire");
    const uint32_t expected[]{0xFF0000FF,0xFFFFFFFF,0xFFFF0000,0xFF00FF00};Check(!std::memcmp(p.graphics.pixels,expected,16),"WIC orientation and channel ordering");
    a.Free(converted);
    c=C(f::Op::Stream);c.count=4;const auto invalid=a.New(c);c=C(f::Op::Write,invalid);c.count=4;a.Ok(c);Check(FAILED(a.Call(C(f::Op::Decode,invalid)).graphics.x),"Unknown codec returns a genuine error");a.Free(invalid);
}
void Warp(Client& a,uint64_t window){
    const auto p=a.Call(C(f::Op::Device));Check(!p.reply.status&&p.graphics.x==S_OK&&p.graphics.value,"WARP device works under the restricted token");const auto device=p.graphics.value;
    Check(p.graphics.selected[0]==D3D_FEATURE_LEVEL_9_1,"Actual declared feature level");auto c=C(f::Op::Texture,device);c.args[0]=c.args[1]=8;c.color=87;c.mode=1;const auto texture=a.New(c);const auto view=a.New(C(f::Op::View,texture));
    c=C(f::Op::ClearView,view);c.args[0]=c.args[3]=Word(1);a.Ok(c);c=C(f::Op::Read,texture);c.count=256;const auto pixels=a.Call(c);Check(pixels.graphics.x==S_OK&&pixels.graphics.pixels[0]==0xFFFF0000,"WARP clear and synchronized readback");
    c=C(f::Op::Present,texture);c.object=window;a.Ok(c);const auto query=a.New(C(f::Op::Query,device));a.Ok(C(f::Op::Signal,query));
    const auto start=GetTickCount64();bool signaled=false;do{const auto reply=a.Call(C(f::Op::Poll,query));Check(reply.graphics.x==S_OK||reply.graphics.x==S_FALSE,"Event query status");signaled=reply.graphics.x==S_OK&&reply.graphics.value;if(!signaled)Sleep(1);}while(!signaled&&GetTickCount64()-start<2000);
    Check(signaled,"WARP event synchronization");a.Free(query);a.Free(view);a.Free(texture);a.Free(device);
}
}
int main(int argc,char** argv){try{Check(argc==2,"Software helper path required");Client a(argv[1]),b(argv[1]);const auto window=a.Window();TwoD(a,window);Images(a);
    auto c=C(f::Op::Stream);c.count=16;const auto foreign=b.New(c);Check(a.Call(C(f::Op::Free,foreign)).reply.status==ERROR_INVALID_HANDLE,"Foreign session object refused");b.Free(foreign);
    Warp(a,window);std::vector<uint64_t> objects;for(unsigned i=0;i<17;++i){c=C(f::Op::Stream);c.count=f::MaxEncoded;const auto p=a.Call(c);if(p.graphics.x==E_OUTOFMEMORY){Check(i==16,"Session byte quota enforced");break;}Check(p.graphics.x==S_OK&&p.graphics.value,"Bounded stream resource");objects.push_back(p.graphics.value);}Check(objects.size()==16,"Native byte accounting");for(const auto id:objects)a.Free(id);
    Check(a.Call(C(f::Op::Stats)).graphics.selected[3]==15,"All four native software backends actually used");
    a.Finish();b.Finish();
    Client crashed(argv[1]);crashed.Window();crashed.broker.KillForTesting();g::Packet packet;packet.boot=1;packet.session=crashed.broker.Enumeration().session;packet.sequence=++crashed.sequence;packet.pid=10;packet.tid=20;
    Check(!crashed.broker.Exchange(packet),"Dead software helper is a transport failure");crashed.broker.Finish();Check(!crashed.broker.Completed(),"Killed helper cannot become completed");
    Client stalled(argv[1]);stalled.Window();stalled.broker.StallForTesting();packet={};packet.boot=1;packet.session=stalled.broker.Enumeration().session;packet.sequence=++stalled.sequence;packet.pid=10;packet.tid=20;
    const auto start=GetTickCount64();Check(!stalled.broker.Exchange(packet),"Stalled software helper times out");Check(GetTickCount64()-start<18000,"Bounded software timeout and cleanup");stalled.broker.Finish();Check(!stalled.broker.Completed(),"Timeout cannot become completed");
    std::cout<<"Software graphics broker "<<checks<<" PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
