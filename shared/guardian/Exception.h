#pragma once
#include <stdint.h>
#include <stddef.h>
namespace guardian::exceptions {
inline constexpr uint32_t ContextFlags=0x10000B,Noncontinuable=1,Unwinding=2,TargetUnwind=0x20;
inline constexpr uint32_t AccessViolation=0xC0000005,IllegalInstruction=0xC000001D,IntegerDivide=0xC0000094,
    NoncontinuableException=0xC0000025,InvalidDisposition=0xC0000026,InvalidUnwindTarget=0xC0000029,Unhandled=0xC0000144;
struct Record {uint32_t code=0,flags=0;uint64_t nested=0,address=0;uint32_t count=0,padding=0;uint64_t information[15]{};};
// Win64 CONTEXT layout. Debug/vector-extension state is not advertised.
struct alignas(16) Context {
    uint64_t home[6]{};uint32_t flags=ContextFlags,mxcsr=0x1F80;
    uint16_t cs=0x2B,ds=0x23,es=0x23,fs=0,gs=0,ss=0x23;uint32_t eflags=0x202;
    uint64_t debug[6]{},gpr[16]{},rip=0;uint8_t fx[512]{},vector[26][16]{};
    uint64_t vectorControl=0,debugControl=0,lastBranchTo=0,lastBranchFrom=0,lastExceptionTo=0,lastExceptionFrom=0;
};
struct Function {uint32_t begin=0,end=0,unwind=0;};
struct Dispatcher {uint64_t controlPc=0,imageBase=0,function=0,establisher=0,targetIp=0,context=0,handler=0,data=0,history=0;uint32_t scopeIndex=0,padding=0;};
struct Pointers {Record* record;Context* context;};
struct alignas(16) Packet {Record record{};uint64_t padding=0;Context context{};Dispatcher dispatcher{};Function function{};uint32_t pad=0;uint64_t routine=0,token=0;};
enum class Disposition:uint32_t {ContinueExecution=0,ContinueSearch=1};
static_assert(sizeof(Record)==152 && sizeof(Context)==1232 && offsetof(Context,gpr)==120 && offsetof(Context,rip)==248 && offsetof(Context,fx)==256);
static_assert(sizeof(Dispatcher)==80 && offsetof(Packet,context)==160 && offsetof(Packet,dispatcher)==1392 && sizeof(Packet)==1504);
}
