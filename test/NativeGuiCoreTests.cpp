#include "../src/microvm/gui/NativeGuiCore.h"
#include "../src/microvm/gui/NativeGuiManifest.h"
#include <iostream>
#include <stdexcept>
#include <string>
namespace n=guardian::nativegui;using av::microvm::native_gui::Core;
namespace {unsigned checks=0;void Check(bool ok){++checks;if(!ok)throw std::runtime_error("Native GUI protocol check "+std::to_string(checks));}
struct Client {uint64_t sequence=0;uint32_t calls=0;Core core{1,77,[&](const n::Command&,uint64_t,uint64_t& value){++calls;value=0x123456;return 0;}};
n::Packet Call(n::Command c,uint32_t pid=1,uint32_t tid=2){n::Packet p;p.boot=1;p.session=77;p.sequence=++sequence;p.pid=pid;p.tid=tid;p.command=c;Check(core.Exchange(p));return p;}};
n::Command Window(){n::Command c;c.op=n::Op::Window;c.rect={10,10,110,90};c.title[0]='A';return c;}}
int main(){try{Client c;const auto window=c.Call(Window()).reply.value;n::Command command;command.op=n::Op::Brush;command.color=0x123456;const auto brush=c.Call(command).reply.value;command={};command.op=n::Op::Dc;command.target=window;const auto dc=c.Call(command).reply.value;
    command={};command.op=n::Op::Fill;command.target=dc;command.object=brush;command.rect={0,0,100,80};Check(!c.Call(command).reply.status);const auto before=c.calls;
    command.target=window;Check(c.Call(command).reply.status==6&&c.calls==before);command.target=dc;Check(c.Call(command,3).reply.status==6);Check(c.Call(command,1,4).reply.status==6);command.rect.right=101;Check(c.Call(command).reply.status==87&&c.calls==before);
    command={};command.op=n::Op::Finish;Check(c.Call(command).reply.status==170);command.op=n::Op::DestroyWindow;command.target=window;Check(c.Call(command).reply.status==170);
    command.op=n::Op::ReleaseDc;command.target=dc;Check(!c.Call(command).reply.status);Check(c.Call(command).reply.status==6);command.op=n::Op::DeleteBrush;command.target=brush;Check(!c.Call(command).reply.status);command.op=n::Op::DestroyWindow;command.target=window;Check(!c.Call(command).reply.status);command={};command.op=n::Op::Finish;auto finish=c.Call(command);Check(finish.reply.completed&&!finish.reply.live[0]&&!finish.reply.live[1]&&!finish.reply.live[2]);
    for(uint32_t variant=0;variant<9;++variant){Client other;n::Packet p;p.boot=1;p.session=77;p.sequence=1;p.pid=1;p.tid=2;p.command=Window();if(variant==0)++p.magic;if(variant==1)++p.boot;if(variant==2)++p.session;if(variant==3)++p.sequence;if(variant==4)++p.version;if(variant==5)++p.bytes;if(variant==6)p.pid=0;if(variant==7)p.tid=0;if(variant==8)p.reply.value=1;Check(!other.core.Exchange(p)&&!other.calls);}
    Client quota;for(uint32_t i=0;i<4;++i)Check(!quota.Call(Window()).reply.status);Check(quota.Call(Window()).reply.status==1816&&quota.calls==4);
    Client invalid;for(uint32_t i=0;i<6;++i){auto bad=Window();if(i==0)bad.flags=1;if(i==1)bad.color=1;if(i==2)bad.rect.right=2000;if(i==3)bad.title[0]=0xD800;if(i==4)bad.reserved=1;if(i==5)bad.target=0x12345;Check(invalid.Call(bad).reply.status==87&&!invalid.calls);}auto unknown=Window();unknown.op=n::Op(0xFFFF);Check(invalid.Call(unknown).reply.status==50&&!invalid.calls);
    Client budget;command={};command.op=n::Op::Dc;command.target=123;for(uint32_t i=0;i<n::MaxCommands;++i)Check(budget.Call(command).reply.status==6);n::Packet excess;excess.boot=1;excess.session=77;excess.sequence=n::MaxCommands+1;excess.pid=1;excess.tid=2;excess.command=command;Check(!budget.core.Exchange(excess));
    const auto manifest=av::microvm::native_gui::ReadManifest(R"({"phase":"14.10A","nativeGuiRuntime":{"backend":"win32-gdi-helper","helperSha256":"abcd","protocol":1}})");Check(manifest.phase=="14.10A"&&manifest.backend=="win32-gdi-helper"&&manifest.protocol=="1");
    for(const auto text:{R"({"phase":"14.10A","phase":"14.9"})",R"({"phase":1})",R"({"phase":"14.10A","nativeGuiRuntime":{"backend":true}})",R"({"phase":"14.10A","x":01})",R"({"phase":"14.10A"}junk)",R"({"nested":{"phase":"14.10A"}})"}){bool rejected=false;try{av::microvm::native_gui::ReadManifest(text);}catch(const std::exception&){rejected=true;}Check(rejected);}
    std::cout<<checks<<" native GUI protocol/resource/ownership/quota checks PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
