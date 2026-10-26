#include "guardian/NativeGdi.h"
#include <iostream>
#include <stdexcept>
namespace g=guardian::nativegdi;
int main(){try{unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("GDI wire contract");};g::Command c;check(g::Valid(c));c.dc=1;check(!g::Valid(c));c={};c.op=g::Op::Bitmap;c.args[0]=512;c.args[1]=512;check(g::Valid(c));c.args[0]=513;check(!g::Valid(c));c={};c.op=g::Op::Read;c.dc=1;c.count=g::Chunk;check(g::Valid(c));c.count=g::Chunk+1;check(!g::Valid(c));c={};c.op=g::Op::OutputText;c.dc=1;c.count=2;c.text[0]=0xD83D;c.text[1]=0xDE00;check(g::Valid(c));c.count=1;check(!g::Valid(c));c={};c.op=g::Op::FormatText;c.dc=1;c.rect={0,0,100,100};c.flags=0x10000;check(!g::Valid(c));c.flags=0x420;check(g::Valid(c));c.mode=1;check(!g::Valid(c));check(sizeof(g::Packet)<4096&&sizeof(g::Call)<4096);std::cout<<checks<<" GDI bounded structure, text, bitmap and unused-field checks PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
