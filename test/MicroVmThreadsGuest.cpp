#include "MicroVmTestGuest.h"
#include <functional>
namespace {
constexpr uint64_t Worker = 0x3000, EventSlot = 0x8800, ThreadSlot = 0x8810, Basic = 0x8A00;
constexpr uint64_t Before = 0x8B00, Seen = 0x8B10, WorkerStep = 0x8B20, Parameter = 0x8B30, Tid = 0x8B40, Self = 0x8B50;
constexpr uint64_t SavedGp = 0x8B60, SavedSse = 0x8B70, SavedStack = 0x8B80, SavedMxcsr = 0x8B90, SavedFpu = 0x8BA0;
constexpr uint64_t MainMxcsr = 0x8C00, OtherMxcsr = 0x8C10, MainFpu = 0x8C20, OtherFpu = 0x8C30;
constexpr uint64_t StackAddress = 0x8C40, WorkerDefaultFpu = 0x8C50, StatusBase = 0x9000;
struct Arg { uint64_t value = 0; bool indirect = false; Arg(uint64_t v) : value(v) {} static Arg At(uint64_t address) { Arg a(address); a.indirect = true; return a; } };
struct Code {
    Fixture& f; std::vector<uint8_t> bytes; std::vector<std::pair<uint64_t,uint32_t>>& statuses;
    void Emit(std::initializer_list<uint8_t> list) { bytes.insert(bytes.end(), list); }
    void U32(uint32_t v) { Fixture::U32(bytes, v); } void U64(uint64_t v) { Fixture::U64(bytes, v); }
    void Mov(unsigned reg, uint64_t value) { Emit({static_cast<uint8_t>(0x48 | (reg >= 8 ? 1 : 0)), static_cast<uint8_t>(0xB8 | (reg & 7))}); U64(value); }
    void Load(uint64_t address) { Emit({0x48,0xA1}); U64(address); }
    void Store(uint64_t address) { Emit({0x48,0xA3}); U64(address); }
    void Set(unsigned reg, Arg arg) {
        if (!arg.indirect) Mov(reg, arg.value);
        else { Load(arg.value); Emit({static_cast<uint8_t>(0x48 | (reg >= 8 ? 1 : 0)),0x89,static_cast<uint8_t>(0xC0 | (reg & 7))}); }
    }
    void Call(const char* name, std::array<Arg,4> args = {0,0,0,0}, std::vector<Arg> stack = {}, uint32_t expected = 0) {
        Emit({0x48,0x81,0xEC}); U32(0x80);
        for (size_t i = 0; i < stack.size(); ++i) {
            Set(0, stack[i]); Emit({0x48,0x89,0x44,0x24,static_cast<uint8_t>(0x28 + i * 8)});
        }
        const unsigned regs[]{1,2,8,9}; for (unsigned i=0;i<4;++i) Set(regs[i],args[i]);
        const auto ssn=f.kernel.SsnOf(name); check(ssn>=0,"thread test SSN unavailable");
        Emit({0xB8}); U32(static_cast<uint32_t>(ssn)); Emit({0x0F,0x05});
        Emit({0x48,0x81,0xC4}); U32(0x80);
        const auto slot=StatusBase+statuses.size()*8; statuses.push_back({slot,expected}); Store(slot);
    }
    void Put(uint64_t address, uint64_t value) { Mov(0,value); Store(address); }
    void Gs(uint32_t offset, uint64_t out) { Emit({0x65,0x48,0x8B,0x04,0x25}); U32(offset); Store(out); }
    void Fpu(bool load, uint64_t address) { Emit({0xD9,static_cast<uint8_t>(load?0x2C:0x3C),0x25}); U32(static_cast<uint32_t>(address)); }
    void Mxcsr(bool load, uint64_t address) { Emit({0x0F,0xAE,static_cast<uint8_t>(load?0x14:0x1C),0x25}); U32(static_cast<uint32_t>(address)); }
};
void ContextAndJoin() {
    Fixture f; std::vector<std::pair<uint64_t,uint32_t>> statuses;
    Code main{f,{},statuses}, worker{f,{},statuses};
    f.memory.Write(Before,uint64_t(0xAABBCCDDFFFFFFFFull));
    f.memory.Write(MainMxcsr,uint32_t(0x5F80)); f.memory.Write(OtherMxcsr,uint32_t(0x7F80));
    f.memory.Write(MainFpu,uint16_t(0x77F)); f.memory.Write(OtherFpu,uint16_t(0xB7F));
    main.Call("NtCreateEvent",{EventSlot,0x1F0003,0,1},{0});
    main.Call("NtCreateThreadEx",{ThreadSlot,0x1FFFFF,0,UINT64_MAX},{Worker,0x1122334455667788ull,1,0,0,0,0});
    main.Call("NtDuplicateObject",{UINT64_MAX,Arg::At(ThreadSlot),UINT64_MAX,0x8820},{0x40,0,0});
    main.Call("NtResumeThread",{Arg::At(0x8820),0,0,0},{},0xC0000022);
    main.Call("NtWaitForSingleObject",{Arg::At(0x8820),0,0,0},{},0xC0000022);
    main.Call("NtClose",{Arg::At(0x8820),0,0,0});
    main.Call("NtQueryInformationThread",{Arg::At(ThreadSlot),0,Basic,48},{ReturnLength});
    main.Load(Basic+8); main.Emit({0x48,0x8B,0x80}); main.U32(0x1478); main.Store(StackAddress); main.Store(BaseSlot);
    main.Put(SizeSlot,0); main.Call("NtFreeVirtualMemory",{UINT64_MAX,BaseSlot,SizeSlot,0x8000},{},0xC0000022);
    main.Call("NtResumeThread",{Arg::At(ThreadSlot),Before,0,0});
    main.Mov(12,Marker); main.Mov(0,Marker); main.Emit({0x66,0x48,0x0F,0x6E,0xF0});
    main.Fpu(true,MainFpu); main.Mxcsr(true,MainMxcsr); main.Emit({0xD9,0xE8}); main.Mov(0,Marker); main.Emit({0x50});
    main.Call("NtWaitForSingleObject",{Arg::At(EventSlot),0,0,0});
    main.Emit({0x58}); main.Store(SavedStack); main.Emit({0x4C,0x89,0xE0}); main.Store(SavedGp);
    main.Emit({0x66,0x48,0x0F,0x7E,0xF0}); main.Store(SavedSse);
    main.Fpu(false,SavedFpu); main.Mxcsr(false,SavedMxcsr); main.Load(WorkerStep); main.Store(Seen);
    main.Emit({0xDD,0x1C,0x25}); main.U32(0x8BB0); main.Gs(0x30,0x8BD0);
    main.Call("NtWaitForSingleObject",{Arg::At(ThreadSlot),0,0,0});
    main.Call("NtQueryInformationThread",{Arg::At(ThreadSlot),0,Basic+0x80,48},{ReturnLength});
    main.Call("NtClose",{Arg::At(ThreadSlot),0,0,0});
    worker.Emit({0x48,0x89,0xC8}); worker.Store(Parameter); worker.Gs(0x48,Tid); worker.Gs(0x30,Self);
    worker.Fpu(false,WorkerDefaultFpu); worker.Mov(12,0x8877665544332211ull);
    worker.Mov(0,0x8877665544332211ull); worker.Emit({0x66,0x48,0x0F,0x6E,0xF0});
    worker.Fpu(true,OtherFpu); worker.Mxcsr(true,OtherMxcsr); worker.Emit({0xD9,0xEE});
    worker.Put(WorkerStep,1); worker.Call("NtSetEvent",{Arg::At(EventSlot),0,0,0});
    worker.Call("NtYieldExecution"); worker.Gs(0x30,0x8BC0); worker.Put(WorkerStep,2); worker.Mov(0,0x77); worker.Emit({0xC3});
    std::memcpy(static_cast<uint8_t*>(f.vm.RamData())+Worker,worker.bytes.data(),worker.bytes.size());
    f.Run(main.bytes);
    for (const auto& pair:statuses) check(static_cast<uint32_t>(f.Read(pair.first))==pair.second,"thread syscall returned unexpected status");
    check(f.Read(Parameter)==0x1122334455667788ull && f.Read(Seen)==1 && f.Read(WorkerStep)==2,"thread parameter/continuations incorrect");
    check(f.Read(SavedGp)==Marker && f.Read(SavedSse)==Marker && f.Read(SavedStack)==Marker,"GPR/SSE/stack context not preserved");
    check(f.Read<uint32_t>(SavedMxcsr)==0x5F80 && f.Read<uint16_t>(SavedFpu)==0x77F && f.Read<uint16_t>(WorkerDefaultFpu)==0x37F,"floating-point control state not isolated");
    check(f.Read(0x8BB0)==0x3FF0000000000000ull && f.Read(0x8BC0)==f.Read(Self) && f.Read(0x8BD0)==0,
        "x87 value stack or GS base did not survive both context switches");
    check(f.Read(Before)==0xAABBCCDD00000001ull,"resume previous-count ULONG canary corrupted");
    check(f.Read(Basic+8)==f.Read(Self) && f.Read(Basic+24)==f.Read(Tid) && f.Read(Tid)!=0x1004,"TEB/client ID not thread-local");
    check(f.Read<uint32_t>(Basic+0x80)==0x77 && f.Read(Basic+0x88)==0,"thread exit status/TEB lifetime incorrect");
    check(!f.vm.CanAccessMemory(f.Read(StackAddress)+4096,1,1) && !f.vm.CanAccessMemory(f.Read(Self),1,1),"thread resources not released");
    std::cout<<"PASS: real ring-3 event handoff/join, suspended creation/resume, direct routine return, GPR/SSE/x87/MXCSR/stack/TEB context and resource cleanup\n";
}
void TimedWaits() {
    Fixture f; std::vector<std::pair<uint64_t,uint32_t>> statuses; Code main{f,{},statuses};
    constexpr uint64_t timeout=0x8D00,past=0x8D10;
    f.memory.Write(timeout,int64_t(-100000)); f.memory.Write(past,int64_t(1));
    main.Call("NtCreateEvent",{EventSlot,0x1F0003,0,1},{0});
    main.Call("NtWaitForSingleObject",{Arg::At(EventSlot),0,timeout,0},{},0x102);
    main.Call("NtDelayExecution",{0,timeout,0,0});
    main.Call("NtWaitForSingleObject",{Arg::At(EventSlot),0,past,0},{},0x102);
    main.Call("NtDelayExecution",{0,past,0,0});
    main.Call("NtWaitForSingleObject",{Arg::At(EventSlot),0,UINT64_MAX-3,0},{},0xC0000005);
    const auto start=std::chrono::steady_clock::now(); f.Run(main.bytes);
    const auto elapsed=std::chrono::steady_clock::now()-start;
    for(const auto& pair:statuses) check(static_cast<uint32_t>(f.Read(pair.first))==pair.second,"timed wait/delay status wrong");
    check(elapsed>=std::chrono::milliseconds(18),"wait/delay fabricated completion before deadlines");
    std::cout<<"PASS: relative event timeout, delay and expired absolute deadlines with no other runnable thread\n";
}
void CreationAndTermination() {
    Fixture f; std::vector<std::pair<uint64_t,uint32_t>> statuses; Code main{f,{},statuses};
    constexpr uint64_t output=0x8820, info=0x8D00;
    f.memory.Write(output,uint64_t(0xABCDEF));
    main.Call("NtCreateThreadEx",{output,0x1FFFFF,0,0x1234},{Worker,0,0,0,0,0,0},0xC0000008);
    main.Call("NtCreateThreadEx",{output,0x1FFFFF,0,UINT64_MAX},{Worker,0,4,0,0,0,0},0xC00000BB);
    main.Call("NtCreateThreadEx",{output,0x1FFFFF,0,UINT64_MAX},{Worker,0,0,0,1,0,0},0xC00000BB);
    main.Call("NtCreateThreadEx",{output,0x1FFFFF,0,UINT64_MAX},{UINT64_MAX,0,0,0,0,0,0},0xC0000005);
    main.Call("NtCreateThreadEx",{ThreadSlot,0x1FFFFF,0,UINT64_MAX},{Worker,0,1,0,0,0,0});
    main.Call("NtSuspendThread",{Arg::At(ThreadSlot),Before,0,0});
    main.Call("NtResumeThread",{Arg::At(ThreadSlot),Before+8,0,0});
    main.Call("NtQueryInformationThread",{Arg::At(ThreadSlot),0,info,48},{ReturnLength});
    main.Call("NtTerminateThread",{Arg::At(ThreadSlot),0x44,0,0});
    main.Call("NtWaitForSingleObject",{Arg::At(ThreadSlot),0,0,0});
    main.Call("NtQueryInformationThread",{Arg::At(ThreadSlot),0,info+0x80,48},{ReturnLength});
    main.Call("NtResumeThread",{Arg::At(ThreadSlot),0,0,0},{},0xC000004B);
    main.Call("NtClose",{Arg::At(ThreadSlot),0,0,0});
    f.Run(main.bytes);
    for(const auto& pair:statuses) check(static_cast<uint32_t>(f.Read(pair.first))==pair.second,"create/suspend/terminate status wrong");
    check(f.Read(output)==0xABCDEF && f.Read<uint32_t>(Before)==1 && f.Read<uint32_t>(Before+8)==2,"failed create changed output or suspend count wrong");
    check(f.Read<uint32_t>(info)==0x103 && f.Read<uint32_t>(info+0x80)==0x44 && f.Read(info+0x88)==0,"other-thread termination/query incorrect");
    check(!f.vm.CanAccessMemory(f.Read(info+8),1,1),"terminated suspended thread leaked TEB mapping");
    std::cout<<"PASS: failed creation preserves output, nested suspend/resume counts, termination of a suspended thread and wait/query after exit\n";
}
void RunFailure(Fixture& f,const std::vector<uint8_t>& code,const std::wstring& expected,bool cancel=false) {
    std::memcpy(static_cast<uint8_t*>(f.vm.RamData())+Entry,code.data(),code.size());
    const WHV_REGISTER_NAME names[]{WHvX64RegisterRip,WHvX64RegisterRsp,WHvX64RegisterRflags};
    WHV_REGISTER_VALUE values[3]{};values[0].Reg64=Entry;values[1].Reg64=Stack;values[2].Reg64=2;
    check(f.vm.SetRegisters(names,3,values),"failure scenario register setup failed");
    std::thread canceller;
    if(cancel) canceller=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(50));f.vm.Cancel();});
    const auto start=std::chrono::steady_clock::now();bool ran=false;
    {Watchdog guard(f.vm);ran=f.vm.Run(1000);}
    if(canceller.joinable())canceller.join();
    if(ran||f.vm.Error().find(expected)==std::wstring::npos) {std::wcerr<<f.vm.Error()<<'\n';throw std::runtime_error("expected scheduler failure was not reported");}
    if(cancel)check(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(2500),"idle scheduler did not observe cancellation promptly");
    check(!f.vm.Run(1),"failed scheduler resumed with uncertain thread state");
}
void DeadlockAndCancellation(bool cancel) {
    Fixture f;std::vector<std::pair<uint64_t,uint32_t>> statuses;Code main{f,{},statuses},worker{f,{},statuses};
    if(cancel) {
        f.memory.Write(0x8D00,int64_t(-100000000));main.Call("NtDelayExecution",{0,0x8D00,0,0});
        RunFailure(f,main.bytes,L"canceled while idle",true);
        std::cout<<"PASS: host cancellation is observed while no vCPU is running\n";
    } else {
        main.Call("NtCreateEvent",{EventSlot,0x1F0003,0,1},{0});
        main.Call("NtCreateThreadEx",{ThreadSlot,0x1FFFFF,0,UINT64_MAX},{Worker,0,0,0,0,0,0});
        main.Call("NtWaitForSingleObject",{Arg::At(EventSlot),0,0,0});
        main.Put(Seen,0xBAD);
        worker.Put(WorkerStep,1);worker.Mov(0,0);worker.Emit({0xC3});
        std::memcpy(static_cast<uint8_t*>(f.vm.RamData())+Worker,worker.bytes.data(),worker.bytes.size());
        RunFailure(f,main.bytes,L"deadlock");
        check(f.Read(Seen)==0&&f.Read(WorkerStep)==1,"deadlock path resumed blocked thread or failed to run worker");
        std::cout<<"PASS: all-waiting/no-deadline deadlock stops execution without fabricating wait success\n";
    }
}
}
int main() {
    BOOL available=FALSE;UINT32 written=0;
    if (FAILED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent,&available,sizeof(available),&written))||!available) { std::cout<<"SKIP: WHP unavailable\n";return 77; }
    try { ContextAndJoin(); TimedWaits(); CreationAndTermination(); DeadlockAndCancellation(false); DeadlockAndCancellation(true); return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
}
