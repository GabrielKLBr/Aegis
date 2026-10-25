#pragma once
#include "Abi.h"
namespace guardian::component {
inline constexpr uint32_t Magic=0x384D4F43,Version=1,Payload=768,Endpoints=12,Calls=16;
enum class Op:uint32_t {Register=1,Lookup,Unregister,Call,Receive,Reply,Result,Cancel,Stats};
// No code pointers or host handles are part of the transport.
struct Request {
    uint32_t magic=Magic,version=Version,bytes=sizeof(Request);Op op=Op::Stats;
    uint64_t endpoint=0,call=0;uint32_t method=0,status=0,length=0,kind=0,pid=0,tid=0;
    uint8_t iid[16]{};char name[64]{};uint8_t data[Payload]{};
};
static_assert(sizeof(Request)<1024);
}
