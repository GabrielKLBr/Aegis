#include "guardian/ExpandedGdi.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace e=guardian::expandedgdi;namespace g=guardian::nativegdi;
int main(){try{unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Expanded protocol contract");};
static_assert(sizeof(g::Command)==972&&sizeof(g::Reply)==896);
check(g::Magic==e::Magic&&g::Version==5);check(sizeof(g::Packet)<4096);
g::Command c;c.op=static_cast<g::Op>(e::Op::Ellipse);c.dc=1;c.rect={0,0,100,100};check(e::Valid(c));c.other=1;check(!e::Valid(c));c.other=0;c.rect.right=4097;check(!e::Valid(c));
c={};c.op=static_cast<g::Op>(e::Op::Bezier);c.dc=1;c.count=4;check(e::Valid(c));c.count=5;check(!e::Valid(c));c.count=4;c.pixels[8]=1;check(!e::Valid(c));
c={};c.op=static_cast<g::Op>(e::Op::World);c.dc=1;c.flags=1;c.args[0]=0x7F800000;check(!e::Valid(c));c.args[0]=0x7FC00000;check(!e::Valid(c));
c={};c.op=static_cast<g::Op>(e::Op::ExtText);c.dc=1;c.flags=0x4000;check(!e::Valid(c));c.flags=0;c.count=1;c.text[0]=0xD800;check(!e::Valid(c));c.flags=0x10;c.text[0]=0;check(e::Valid(c));c.count=65;check(!e::Valid(c));
c={};c.op=static_cast<g::Op>(e::Op::ExtText);c.dc=1;c.count=1;c.text[0]='A';c.mode=2;c.pixels[0]=257;check(!e::Valid(c));c.pixels[0]=16;check(e::Valid(c));
c={};c.op=static_cast<g::Op>(e::Op::Raster);c.dc=1;c.other=2;c.rect=c.source={0,0,16,16};c.color=0x00CC0020;check(e::Valid(c));c.color=0x40000000;check(!e::Valid(c));
c={};c.op=static_cast<g::Op>(e::Op::Path);c.dc=1;c.flags=11;c.mode=5;check(e::Valid(c));c.mode=6;check(!e::Valid(c));
c={};c.op=static_cast<g::Op>(e::Op::Widths);c.dc=1;c.args[0]=32;c.args[1]=63;check(e::Valid(c));c.args[1]=64;check(!e::Valid(c));
guardian::nativeuser::Command cursor;cursor.op=static_cast<guardian::nativeuser::Op>(guardian::nativecontrols::Op::Cursor);cursor.wp=32512;check(guardian::nativecontrols::Valid(cursor));cursor.wp=123;check(!guardian::nativecontrols::Valid(cursor));
std::cout<<checks<<" expanded GDI protocol checks PASS bytes="<<sizeof(g::Packet)<<"\n";return 0;}catch(const std::exception& x){std::cerr<<x.what()<<"\n";return 1;}}

