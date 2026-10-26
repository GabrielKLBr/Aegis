#pragma once
#include <stdint.h>
// Versioned, pointer-free transport. TLS and application protocols remain in CPL3.
namespace guardian::internet {
inline constexpr uint64_t Magic=0x3154454E494147ull, PacketGpa=0x1F9000;
inline constexpr uint16_t Port=0xF9;
inline constexpr uint32_t Payload=2048, MaxSockets=16, MaxEndpoints=8;
enum class Mode:uint32_t { Offline, Simulated, Lab, Authorized };
enum class Op:uint32_t { Resolve=1, Open, Connect, Send, Receive, Poll, Close, Random, Exit };
struct Endpoint { char name[64]{}; uint8_t address[16]{}; uint32_t family=2,port=0,kind=1,service=0; };
struct Policy {
    uint64_t magic=Magic; uint32_t version=1,bytes=sizeof(Policy);
    Mode mode=Mode::Offline; uint32_t endpoints=0,requestLimit=32768,byteLimit=2u<<20;
    Endpoint entries[MaxEndpoints]{};
};
struct Request {
    uint64_t magic=Magic,sequence=0; uint32_t version=1,bytes=sizeof(Request);
    Op op=Op::Resolve; uint32_t pid=0,status=0,id=0,kind=1,port=0,length=0,flags=0;
    uint8_t address[16]{}; char name[64]{}; uint8_t data[Payload]{};
};
static_assert(sizeof(Request)<=4096 && sizeof(Policy)==800);
}
