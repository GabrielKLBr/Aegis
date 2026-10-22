#define GUARDIAN_RUNTIME_BUILD
#include "PeRuntimeApi.h"
#include "guardian/Runtime.h"
#include "guardian/SystemClock.h"
extern "C" uint32_t GuardianRuntimeCall(guardian::runtime::Command*) noexcept;
extern "C" uint32_t GuardianRuntimeError(uint32_t) noexcept;
namespace guardian::win {
namespace {
using runtime::Command; using runtime::Op;
bool Result(uint32_t status) noexcept {
    if (!status) return true;
    SetLastError(GuardianRuntimeError(status)); return false;
}
bool Clock(time::Sample& sample) noexcept {
    Command c{}; c.op = Op::ClockRead;
    if (!Result(GuardianRuntimeCall(&c))) return false;
    if (c.transferred != sizeof(sample)) { SetLastError(87); return false; }
    for (uint32_t i=0;i<sizeof(sample);++i)
        reinterpret_cast<uint8_t*>(&sample)[i]=c.data[i];
    return true;
}
}
extern "C" Bool QueryPerformanceCounter(int64_t* result) {
    if (!result) { SetLastError(87); return 0; }
    time::Sample sample{}; if (!Clock(sample)) return 0;
    *result=static_cast<int64_t>(sample.counter); return 1;
}
extern "C" Bool QueryPerformanceFrequency(int64_t* result) {
    if (!result) { SetLastError(87); return 0; }
    time::Sample sample{}; if (!Clock(sample)) return 0;
    *result=static_cast<int64_t>(sample.frequency); return 1;
}
extern "C" uint64_t GetTickCount64() {time::Sample s{};return Clock(s)?s.monotonicNs/1'000'000:0;}
extern "C" Dword GetTickCount() {return static_cast<Dword>(GetTickCount64());}
extern "C" Bool QueryUnbiasedInterruptTime(uint64_t* result) {
    if (!result) {SetLastError(87);return 0;}
    time::Sample s{};if(!Clock(s))return 0;*result=s.monotonicNs/100;return 1;
}
extern "C" void GetSystemTimeAsFileTime(FileTime* result) {
    if (!result) {SetLastError(87);return;}
    time::Sample s{};if(!Clock(s))return;
    result->low=static_cast<uint32_t>(s.utc100ns);result->high=static_cast<uint32_t>(s.utc100ns>>32);
}
extern "C" void Sleep(Dword ms) {
    Command c{};c.op=Op::Delay;c.argument=ms;
    const auto status=GuardianRuntimeCall(&c);
    // Unsupported/invalid requests must never look like a completed wait.
    if(status)RaiseException(status,1,0,nullptr);
}
extern "C" void* VirtualAlloc(void* address,uint64_t bytes,Dword type,Dword protection) {
    if(address){SetLastError(50);return nullptr;}
    Command c{};c.op=Op::MemoryAllocate;c.argument=bytes;c.flags=type;c.access=protection;
    if(!Result(GuardianRuntimeCall(&c)))return nullptr;return reinterpret_cast<void*>(c.handle);
}
extern "C" Bool VirtualFree(void* address,uint64_t bytes,Dword type) {
    Command c{};c.op=Op::MemoryFree;c.handle=reinterpret_cast<uint64_t>(address);c.argument=bytes;c.flags=type;
    return Result(GuardianRuntimeCall(&c))?1:0;
}
extern "C" Bool VirtualProtect(void* address,uint64_t bytes,Dword protection,Dword* previous) {
    if(!previous){SetLastError(87);return 0;}
    Command c{};c.op=Op::MemoryProtect;c.handle=reinterpret_cast<uint64_t>(address);c.argument=bytes;c.access=protection;
    if(!Result(GuardianRuntimeCall(&c)))return 0;*previous=c.transferred;return 1;
}
extern "C" uint64_t VirtualQuery(const void* address,memory::Information* information,uint64_t bytes) {
    if(!information||bytes<sizeof(*information)){SetLastError(87);return 0;}
    Command c{};c.op=Op::MemoryQuery;c.handle=reinterpret_cast<uint64_t>(address);c.length=sizeof(*information);
    if(!Result(GuardianRuntimeCall(&c)))return 0;
    for(uint32_t i=0;i<sizeof(*information);++i)reinterpret_cast<uint8_t*>(information)[i]=c.data[i];
    return sizeof(*information);
}
extern "C" Bool GetFileTime(Handle handle,FileTime* creation,FileTime* access,FileTime* write) {
    Command c{};c.op=Op::FileTimesRead;c.handle=reinterpret_cast<uint64_t>(handle);
    if(!Result(GuardianRuntimeCall(&c)))return 0;
    if(c.transferred!=24){SetLastError(87);return 0;}
    FileTime* fields[3]={creation,access,write};for(uint32_t i=0;i<3;++i)if(fields[i]){
        auto* out=reinterpret_cast<uint8_t*>(fields[i]);for(uint32_t n=0;n<8;++n)out[n]=c.data[i*8+n];
    }return 1;
}
extern "C" Bool SetFileTime(Handle handle,const FileTime* creation,const FileTime* access,const FileTime* write) {
    Command c{};c.op=Op::FileTimesWrite;c.handle=reinterpret_cast<uint64_t>(handle);c.length=24;
    const FileTime* fields[3]={creation,access,write};for(uint32_t i=0;i<3;++i)if(fields[i] && (fields[i]->low||fields[i]->high)){
        c.flags|=1u<<i;const auto* in=reinterpret_cast<const uint8_t*>(fields[i]);for(uint32_t n=0;n<8;++n)c.data[i*8+n]=in[n];
    }return Result(GuardianRuntimeCall(&c));
}
extern "C" Bool GuardianEnvironmentInfo(guardian::environment::Info* info) {
    if(!info){SetLastError(87);return 0;}Command c{};c.op=Op::EnvironmentRead;
    if(!Result(GuardianRuntimeCall(&c)))return 0;
    if(c.transferred!=sizeof(*info)){SetLastError(87);return 0;}
    auto* out=reinterpret_cast<uint8_t*>(info);for(uint32_t i=0;i<sizeof(*info);++i)out[i]=c.data[i];return 1;
}

namespace {
bool CommandText(char* out,const char* text,uint32_t bytes){if(!text)return false;for(uint32_t i=0;i<bytes;++i){out[i]=text[i];if(!text[i])return true;}return false;}
}
extern "C" Bool MoveFileA(const char* from,const char* to){Command c{};c.op=Op::FileRename;if(!CommandText(c.path,from,128)||!CommandText(reinterpret_cast<char*>(c.data),to,128)){SetLastError(87);return 0;}return Result(GuardianRuntimeCall(&c));}
extern "C" Bool DeleteFileA(const char* path){Command c{};c.op=Op::FileDelete;if(!CommandText(c.path,path,128)){SetLastError(87);return 0;}return Result(GuardianRuntimeCall(&c));}
extern "C" Bool GuardianNetworkEcho(){Command c{};c.op=Op::NetworkEcho;return Result(GuardianRuntimeCall(&c));}
extern "C" Bool GuardianAnalysisInfo(uint32_t* scenario){if(!scenario){SetLastError(87);return 0;}Command c{};c.op=Op::AnalysisInfo;if(!Result(GuardianRuntimeCall(&c)))return 0;*scenario=c.transferred;return 1;}
extern "C" Bool GuardianCreateService(const char* name,const char* image,Dword* id){if(!id){SetLastError(87);return 0;}Command c{};c.op=Op::ServiceCreate;if(!CommandText(c.path,name,32)||!CommandText(reinterpret_cast<char*>(c.data),image,64)){SetLastError(87);return 0;}if(!Result(GuardianRuntimeCall(&c)))return 0;*id=c.transferred;return 1;}
extern "C" Bool GuardianStartService(Dword id){Command c{};c.op=Op::ServiceStart;c.handle=id;return Result(GuardianRuntimeCall(&c));}
extern "C" Bool GuardianStopService(Dword id){Command c{};c.op=Op::ServiceStop;c.handle=id;return Result(GuardianRuntimeCall(&c));}
extern "C" Bool GuardianQueryService(Dword id,Dword* state){if(!state){SetLastError(87);return 0;}Command c{};c.op=Op::ServiceQuery;c.handle=id;if(!Result(GuardianRuntimeCall(&c)))return 0;*state=c.transferred;return 1;}

}
