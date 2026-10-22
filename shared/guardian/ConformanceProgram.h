#pragma once
#include "Conformance.h"
#include "Memory.h"
namespace guardian::conformance {
using Handle = void*;
using Worker = uint32_t (*)(void*);
struct Api {
    bool (*counter)(int64_t*); bool (*frequency)(int64_t*);
    uint64_t (*ticks)(); uint32_t (*ticks32)(); uint64_t (*utc)(); bool (*unbiased)(uint64_t*);
    void (*sleep)(uint32_t); uint32_t (*error)(); void (*setError)(uint32_t);
    Handle (*heap)(); void* (*allocate)(Handle,uint32_t,uint64_t);
    void* (*reallocate)(Handle,uint32_t,void*,uint64_t); bool (*release)(Handle,uint32_t,void*);
    void* (*virtualAllocate)(uint64_t); bool (*virtualFree)(void*);
    bool (*protect)(void*,uint64_t,uint32_t,uint32_t*);
    uint64_t (*query)(const void*,memory::Information*,uint64_t);
    Handle (*mutex)(bool); uint32_t (*wait)(Handle,uint32_t); bool (*unlock)(Handle); bool (*close)(Handle);
    Handle (*thread)(Worker,void*,uint32_t*); bool (*exitCode)(Handle,uint32_t*);
    uint32_t (*threadId)(); uint32_t (*processId)();
    Handle (*open)(const char*,uint32_t,uint32_t,uint32_t);
    bool (*read)(Handle,void*,uint32_t,uint32_t*); bool (*write)(Handle,const void*,uint32_t,uint32_t*);
    bool (*seek)(Handle,int64_t,int64_t*,uint32_t); bool (*size)(Handle,int64_t*); bool (*flush)(Handle);
    uint32_t (*exception)(uint32_t); uint32_t (*features)();
};
using Sink = bool (*)(void*,uint32_t,uint64_t);
struct WorkerData { Api* api; Handle mutex; uint32_t mode, id=0; };
inline uint32_t WorkerEntry(void* argument) {
    auto& d = *static_cast<WorkerData*>(argument); auto& a = *d.api; d.id = a.threadId();
    if (d.mode == 2) { a.setError(0x12C77); a.sleep(0); return a.error()==0x12C77?17u:99u; }
    const auto result=a.wait(d.mutex,d.mode==1?UINT32_MAX:0u);
    if (d.mode!=1 && (result==0 || result==128)) a.unlock(d.mutex);
    return result;
}
// One test program, compiled once against the guest DLL and once against Win32.
// It uses only its own allocations, threads, mutexes and supplied file path.
inline uint32_t Run(Api& a,const char* path,Sink sink,void* context) {
    uint32_t index=0,failed=0;
    const auto observe=[&](uint64_t actual) {
        if(index>=CaseCount || actual!=Cases[index].expected)++failed;
        if(!sink(context,index,actual))++failed;
        ++index;
    };
    int64_t frequency=0,q0=0,q1=0;
    observe(a.frequency(&frequency) && frequency>0);
    observe(a.counter(&q0) && a.counter(&q1) && q1>=q0);
    const auto ticks0=a.ticks();const auto ticks32=a.ticks32();const auto ticks1=a.ticks();
    observe(static_cast<uint32_t>(ticks32-static_cast<uint32_t>(ticks0)) <= ticks1-ticks0);
    int64_t bracketStart=0,bracketEnd=0;
    a.counter(&bracketStart);const auto utc0=a.utc();observe(utc0>=116444736000000000ull);
    uint64_t u0=0,u1=0;observe(a.unbiased(&u0));
    a.counter(&q0);a.sleep(40);a.counter(&q1);a.unbiased(&u1);const auto utc1=a.utc();a.counter(&bracketEnd);
    const auto elapsed=frequency>0 && q1>=q0?static_cast<uint64_t>(q1-q0)*1000/static_cast<uint64_t>(frequency):0;
    // Windows timer quantization may shorten a relative Sleep by one clock tick.
    // Scheduling delays have no upper bound, so the test imposes no maximum.
    observe(elapsed>=24);
    const auto unbiased=u1>=u0?(u1-u0)/10000:UINT64_MAX;
    const auto outer=frequency>0 && bracketEnd>=bracketStart?
        static_cast<uint64_t>(bracketEnd-bracketStart)*1000/static_cast<uint64_t>(frequency):0;
    // Bracketing includes arbitrary preemption between API calls. Only the
    // documented system tick quantization needs a fixed (16 ms) tolerance.
    observe(unbiased<=UINT64_MAX-16 && outer<=UINT64_MAX-16 &&
            unbiased+16>=elapsed && unbiased<=outer+16);
    observe(utc1>=utc0 && utc1-utc0>=240000);

    auto mutex=a.mutex(true);if(!mutex)return failed+100;
    observe(a.unlock(mutex));
    const bool unowned=a.unlock(mutex);observe(!unowned?a.error():UINT32_MAX);
    observe(a.wait(mutex,0));a.wait(mutex,0);a.unlock(mutex);
    WorkerData recursive{&a,mutex,0};uint32_t tid=0,code=UINT32_MAX;
    auto thread=a.thread(WorkerEntry,&recursive,&tid);if(!thread)return failed+100;
    const auto joined=a.wait(thread,UINT32_MAX);a.exitCode(thread,&code);observe(!joined?code:UINT32_MAX);
    a.close(thread);observe(a.unlock(mutex));
    WorkerData abandoned{&a,mutex,1};thread=a.thread(WorkerEntry,&abandoned,&tid);
    if(!thread)return failed+100;
    a.wait(thread,UINT32_MAX);a.close(thread);observe(a.wait(mutex,UINT32_MAX));a.unlock(mutex);
    WorkerData errors{&a,nullptr,2};a.setError(0x12C33);
    thread=a.thread(WorkerEntry,&errors,&tid);if(!thread)return failed+100;
    a.setError(0x12C33);
    const auto mainTid=a.threadId();const auto wait=a.wait(thread,UINT32_MAX);
    const bool exit=a.exitCode(thread,&code);observe(!wait && exit && code==17);
    observe(errors.id==tid && tid && tid!=mainTid);
    observe(a.error()==0x12C33);a.close(thread);
    const auto stale=a.wait(thread,0);observe(stale==UINT32_MAX?a.error():UINT32_MAX);
    const bool twice=a.close(thread);observe(!twice?a.error():UINT32_MAX);a.close(mutex);

    const auto heap=a.heap();auto* bytes=static_cast<uint8_t*>(a.allocate(heap,8,32));
    if(!bytes)return failed+100;
    bool zero=(reinterpret_cast<uint64_t>(bytes)&15)==0;
    for(uint32_t i=0;i<32;++i){zero=zero&&!bytes[i];bytes[i]=static_cast<uint8_t>(i+1);}observe(zero);
    auto* grown=static_cast<uint8_t*>(a.reallocate(heap,8,bytes,80));if(!grown){a.release(heap,0,bytes);return failed+100;}
    bool prefix=true,extension=true;for(uint32_t i=0;i<32;++i)prefix=prefix&&grown[i]==i+1;
    for(uint32_t i=32;i<80;++i)extension=extension&&!grown[i];observe(prefix);observe(extension);observe(a.release(heap,0,grown));

    auto* region=static_cast<uint8_t*>(a.virtualAllocate(8192));if(!region)return failed+100;
    zero=(reinterpret_cast<uint64_t>(region)&65535)==0;
    for(uint32_t i=0;i<8192;++i)zero=zero&&!region[i];observe(zero);region[0]=0x42;
    memory::Information info{};
    observe(a.query(region,&info,sizeof(info))==sizeof(info) && info.allocationBase==reinterpret_cast<uint64_t>(region) &&
            info.state==memory::Commit && info.protect==memory::ReadWrite && info.type==0x20000 && info.bytes>=8192);
    uint32_t previous=0;const auto protectedOk=a.protect(region,4096,2,&previous);observe(protectedOk?previous:UINT32_MAX);
    observe(a.query(region,&info,sizeof(info))==sizeof(info) && info.bytes==4096 && info.protect==2 && info.allocationProtect==4);
    const auto restored=a.protect(region,4096,4,&previous);observe(restored?previous:UINT32_MAX);
    observe(region[0]==0x42);observe(a.virtualFree(region));
    observe(a.query(region,&info,sizeof(info))==sizeof(info) && info.state==memory::Free && !info.allocationBase);
    const bool released=a.virtualFree(region);observe(!released?a.error():UINT32_MAX);

    const auto invalid=reinterpret_cast<Handle>(UINT64_MAX);
    auto file=a.open(path,0xC0000000,3,2);observe(file!=invalid);if(file==invalid)return failed+100;
    uint32_t transferred=0;const uint8_t input[3]={11,22,33};uint8_t output[4]{};
    observe(a.write(file,input,3,&transferred)?transferred:UINT32_MAX);
    int64_t position=-1,size=-1;observe(a.seek(file,0,&position,0)?static_cast<uint64_t>(position):UINT64_MAX);
    observe(a.read(file,output,3,&transferred) && transferred==3 && output[0]==11 && output[1]==22 && output[2]==33);
    observe(a.read(file,output,1,&transferred)?transferred:UINT32_MAX);
    observe(a.size(file,&size)?static_cast<uint64_t>(size):UINT64_MAX);
    const bool negative=a.seek(file,-1,&position,0);observe(!negative?a.error():UINT32_MAX);
    auto exclusive=a.open(path,0x80000000,0,3);observe(exclusive==invalid?a.error():UINT32_MAX);if(exclusive!=invalid)a.close(exclusive);
    observe(a.flush(file));observe(a.close(file));
    auto collision=a.open(path,0xC0000000,3,1);observe(collision==invalid?a.error():UINT32_MAX);if(collision!=invalid)a.close(collision);
    auto readOnly=a.open(path,0x80000000,3,3);if(readOnly==invalid)return failed+100;
    transferred=123;const bool denied=a.write(readOnly,input,1,&transferred);observe(!denied?a.error():UINT32_MAX);observe(transferred);a.close(readOnly);
    file=a.open(path,0xC0000000,3,5);if(file==invalid)return failed+100;
    observe(a.size(file,&size)?static_cast<uint64_t>(size):UINT64_MAX);a.close(file);

    for(uint32_t kind=0;kind<3;++kind)observe(a.exception(kind));
    const auto features=a.features();observe(sizeof(void*)==8 && (features&1));observe((features&2)!=0);observe((features&4)!=0);
    observe(a.processId()!=0 && a.threadId()==mainTid);
    return index==CaseCount?failed:failed+100;
}
}
