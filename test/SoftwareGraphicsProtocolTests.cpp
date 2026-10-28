#include "guardian/SoftwareGraphics.h"
#include "guardian/PeVolume.h"
#include <iostream>
#include <stdexcept>
#include <xmmintrin.h>
namespace f=guardian::softwaregraphics;namespace g=guardian::nativegdi;
namespace {unsigned checks=0;void Check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}g::Command Call(f::Op op){g::Command c;c.op=static_cast<g::Op>(op);return c;}}
int main(){try{
    static_assert(g::Version==6&&g::Magic==f::Magic&&sizeof(g::Packet)==3260);
    const auto csr=_mm_getcsr();_mm_setcsr(csr&~0x1F80u);
    Check(!f::Float(0x7F800001,0,1),"Signaling NaN validation does not execute floating point");
    Check(!f::Float(0x00000001,1,96),"Denormal bounds use exact IEEE ordering");
    Check(f::Float(0x80000000,0,1)&&f::Float(0x3F800000,0,1),"Signed zero and inclusive boundary");
    _mm_setcsr(csr);
    namespace pv=guardian::pevolume;
    Check(pv::DataPath("c:\\app\\image.png",2)&&pv::DataPath("c:\\app\\image.bmp",2)&&pv::DataPath("c:\\app\\image.jpg",2)&&pv::DataPath("c:\\app\\image.jpeg",2),"V2 guest image paths");
    Check(!pv::DataPath("c:\\app\\image.png",1)&&!pv::DataPath("c:\\windows\\system32\\image.png",2)&&!pv::DataPath("c:\\app\\image.svg",2),"Legacy/system/unknown image paths refused");
    auto c=Call(f::Op::Clear);Check(f::Valid(c),"Canonical zero clear");c.args[3]=0x3F800000;Check(f::Valid(c),"Opaque clear");
    c.args[0]=0x7FC00000;Check(!f::Valid(c),"NaN rejected");c.args[0]=0x7F800000;Check(!f::Valid(c),"Infinity rejected");c.args[0]=0x40000000;Check(!f::Valid(c),"Color component quota");
    c=Call(f::Op::Stream);c.count=f::MaxEncoded;Check(f::Valid(c),"Bounded encoded data");++c.count;Check(!f::Valid(c),"Encoded quota");
    c=Call(f::Op::Write);c.count=1;c.pixels[0]=0xFF;Check(f::Valid(c),"Byte chunks");c.pixels[0]=0x100;Check(!f::Valid(c),"Unused tail bytes rejected");c.pixels[0]=0;c.count=513;Check(!f::Valid(c),"Oversized chunks");c.count=512;c.offset=f::MaxEncoded-511;Check(!f::Valid(c),"Offset overflow");
    c=Call(f::Op::Target);c.flags=2;Check(!f::Valid(c),"Target kind allowlist");
    c=Call(f::Op::Texture);c.args[0]=c.args[1]=512;c.color=87;Check(f::Valid(c),"Known BGRA texture");c.color=999;Check(!f::Valid(c),"Unknown texture format");c.color=28;c.args[0]=513;Check(!f::Valid(c),"Texture dimension quota");c.args[0]=512;c.flags=c.mode=1;Check(!f::Valid(c),"Staging render target rejected");
    c=Call(f::Op::Text);c.count=1;c.text[0]=0xD800;Check(!f::Valid(c),"Unpaired surrogate");c.text[0]='A';Check(f::Valid(c),"Bounded text");c.text[2]='x';Check(!f::Valid(c),"Text tail rejected");
    c=Call(f::Op::Device);c.other=1;Check(!f::Valid(c),"Device cannot take a host adapter");c.other=0;c.text[0]='C';Check(!f::Valid(c),"Device cannot take a DLL path");
    c=Call(f::Op::Decode);c.text[0]='C';Check(!f::Valid(c),"Codec cannot take a host path");c=Call(f::Op::Stats);c.pixels[127]=1;Check(!f::Valid(c),"Reserved fields rejected");
    c={};Check(f::Valid(c),"Inherited zero GDI command");c.op=g::Op::Brush;c.color=0x123456;Check(f::Valid(c),"Inherited GDI contract remains active");
    std::cout<<"Software graphics protocol "<<checks<<" PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
