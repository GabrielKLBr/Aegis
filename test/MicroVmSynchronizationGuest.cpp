// Generated ring-3 programs only; no external executable or malware sample.
#include "MicroVmTestGuest.h"
namespace {
constexpr uint64_t Sem = 0x8000, Mutex = 0x8010, Alias = 0x8020, Restricted = 0x8030;
constexpr uint64_t T1 = 0x8040, T2 = 0x8050, Event = 0x8060, Failed = 0x8070;
constexpr uint64_t Zero = 0x8100, Short = 0x8110, Long = 0x8120, Previous = 0x8130, Length = 0x8140;
constexpr uint64_t Basic = 0x8200, Seen = 0x8500, Before = 0x8510;
constexpr uint64_t Worker1 = 0x20000, Worker2 = 0x22000;
struct Arg {
    uint64_t value; bool indirect = false;
    Arg(uint64_t v = 0) : value(v) {}
    static Arg At(uint64_t address) { Arg arg(address); arg.indirect = true; return arg; }
};
struct Expected { uint64_t slot; uint32_t status; const char* name; };
struct Code {
    Fixture& fixture; std::vector<Expected>& expected; std::vector<uint8_t> bytes;
    void Emit(std::initializer_list<uint8_t> v) { bytes.insert(bytes.end(), v); }
    void U32(uint32_t v) { Fixture::U32(bytes, v); }
    void U64(uint64_t v) { Fixture::U64(bytes, v); }
    void Mov(unsigned reg, uint64_t value) { Emit({static_cast<uint8_t>(0x48 | (reg >= 8 ? 1 : 0)), static_cast<uint8_t>(0xB8 | (reg & 7))}); U64(value); }
    void Load(uint64_t address) { Emit({0x48, 0xA1}); U64(address); }
    void Store(uint64_t address) { Emit({0x48, 0xA3}); U64(address); }
    void Set(unsigned reg, Arg arg) {
        if (!arg.indirect) Mov(reg, arg.value);
        else { Load(arg.value); Emit({static_cast<uint8_t>(0x48 | (reg >= 8 ? 1 : 0)), 0x89, static_cast<uint8_t>(0xC0 | (reg & 7))}); }
    }
    void Put(uint64_t address, uint64_t value) { Mov(0, value); Store(address); }
    void Copy(uint64_t source, uint64_t destination) { Load(source); Store(destination); }
    void Call(const char* name, std::array<Arg, 4> args = {0,0,0,0}, std::vector<Arg> stack = {}, uint32_t status = 0) {
        Emit({0x48,0x81,0xEC}); U32(0x80);
        for (size_t i = 0; i < stack.size(); ++i) { Set(0, stack[i]); Emit({0x48,0x89,0x44,0x24,static_cast<uint8_t>(0x28 + i * 8)}); }
        const unsigned registers[]{1,2,8,9}; for (unsigned i = 0; i < 4; ++i) Set(registers[i], args[i]);
        const auto ssn = fixture.kernel.SsnOf(name); check(ssn >= 0, "synchronization syscall unavailable");
        Emit({0xB8}); U32(static_cast<uint32_t>(ssn)); Emit({0x0F,0x05,0x48,0x81,0xC4}); U32(0x80);
        const auto slot = 0x18000 + expected.size() * 8;
        fixture.memory.Write(slot, UINT64_MAX); expected.push_back({slot,status,name}); Store(slot);
    }
    void Wait(uint64_t handle, uint64_t timeout = 0, uint32_t status = 0) { Call("NtWaitForSingleObject", {Arg::At(handle),0,timeout,0}, {}, status); }
    void Thread(uint64_t slot, uint64_t entry) { Call("NtCreateThreadEx", {slot,0x1FFFFF,0,UINT64_MAX}, {entry,0,0,0,0,0,0}); }
    void Query(const char* name, uint64_t handle, uint64_t out = Basic) { Call(name, {Arg::At(handle),0,out,8}, {Length}); }
    void Ret() { Mov(0, 0); Emit({0xC3}); }
};
struct Program {
    Fixture fixture; std::vector<Expected> expected;
    Code main{fixture, expected, {}}, first{fixture, expected, {}}, second{fixture, expected, {}};
    Program() {
        fixture.memory.Write(Zero, int64_t(0)); fixture.memory.Write(Short, int64_t(-50000)); fixture.memory.Write(Long, int64_t(-300000));
        fixture.memory.Write(Previous, uint64_t(0xAABBCCDDFFFFFFFFull)); fixture.memory.Write(Length, uint64_t(0x11223344FFFFFFFFull));
        fixture.memory.Write(Failed, Marker);
    }
    void Run() {
        for (const auto& pair : {std::pair<uint64_t,Code*>{Worker1,&first}, {Worker2,&second}})
            if (!pair.second->bytes.empty()) std::memcpy(static_cast<uint8_t*>(fixture.vm.RamData()) + pair.first, pair.second->bytes.data(), pair.second->bytes.size());
        fixture.Run(main.bytes);
        for (const auto& value : expected) if (static_cast<uint32_t>(fixture.Read(value.slot)) != value.status) {
            std::cerr << value.name << " expected=0x" << std::hex << value.status << " actual=0x" << fixture.Read(value.slot) << std::dec << '\n';
            throw std::runtime_error("guest synchronization status mismatch or continuation missing");
        }
    }
};
void SemaphoreValidation() {
    Program p; auto& main = p.main;
    main.Call("NtCreateSemaphore", {Failed,0x1F0003,0,UINT32_MAX}, {2}, 0xC000000D);
    main.Call("NtCreateSemaphore", {Failed,0x1F0003,0,3}, {2}, 0xC000000D);
    main.Call("NtCreateSemaphore", {Failed,0x1F0003,0,0}, {0}, 0xC000000D);
    main.Copy(Failed, Before);
    main.Call("NtCreateSemaphore", {Sem,0x1F0003,0,1}, {2});
    main.Call("NtDuplicateObject", {UINT64_MAX,Arg::At(Sem),UINT64_MAX,Restricted}, {0x100001,0,0});
    main.Call("NtDuplicateObject", {UINT64_MAX,Arg::At(Sem),UINT64_MAX,Alias}, {2,0,0});
    main.Call("NtReleaseSemaphore", {Arg::At(Restricted),1,0,0}, {}, 0xC0000022);
    main.Wait(Alias, Zero, 0xC0000022);
    main.Call("NtQuerySemaphore", {Arg::At(Alias),0,Basic,8}, {Length}, 0xC0000022);
    main.Call("NtReleaseMutant", {Arg::At(Sem),0,0,0}, {}, 0xC0000024);
    main.Wait(Restricted, Zero); main.Wait(Sem, Zero, 0x102);
    main.Call("NtReleaseSemaphore", {Arg::At(Alias),1,Previous,0});
    main.Call("NtReleaseSemaphore", {Arg::At(Sem),2,0,0}, {}, 0xC0000047);
    main.Call("NtReleaseSemaphore", {Arg::At(Sem),0,0,0}, {}, 0xC000000D);
    main.Call("NtReleaseSemaphore", {Arg::At(Sem),1,UINT64_MAX-3,0}, {}, 0xC0000005);
    main.Query("NtQuerySemaphore", Sem);
    main.Call("NtQuerySemaphore", {Arg::At(Sem),0,Basic+16,7}, {Length}, 0xC0000004);
    main.Call("NtQuerySemaphore", {Arg::At(Sem),0,Basic+16,8}, {Basic+16}, 0xC000000D);
    main.Call("NtOpenSemaphore", {Failed,0x1F0003,0,0}, {}, 0xC00000BB);
    main.Call("NtClose", {Arg::At(Sem),0,0,0}); main.Call("NtClose", {Arg::At(Alias),0,0,0}); main.Call("NtClose", {Arg::At(Restricted),0,0,0});
    p.Run();
    check(p.fixture.Read(Before) == Marker, "failed create published a handle");
    check(p.fixture.Read<int32_t>(Basic) == 1 && p.fixture.Read<int32_t>(Basic+4) == 2, "failed release modified semaphore");
    check(p.fixture.Read(Previous) == 0xAABBCCDD00000000ull && p.fixture.Read(Length) == 0x1122334400000008ull, "LONG/ULONG output overwrote canary");
    std::cout << "PASS: semaphore counts, reduced handle rights, shared aliases, overflow and checked ABI outputs\n";
}
void SemaphoreHandoff() {
    Program p; auto& main = p.main;
    main.Call("NtCreateSemaphore", {Sem,0x1F0003,0,0}, {2});
    main.Call("NtDuplicateObject", {UINT64_MAX,Arg::At(Sem),UINT64_MAX,Alias}, {0x100000,0,0});
    main.Thread(T1, Worker1); main.Thread(T2, Worker2); main.Call("NtYieldExecution");
    main.Call("NtSuspendThread", {Arg::At(T2),0,0,0});
    main.Call("NtClose", {Arg::At(Alias),0,0,0});
    main.Call("NtReleaseSemaphore", {Arg::At(Sem),2,Previous,0});
    main.Query("NtQuerySemaphore", Sem); main.Wait(Sem, Zero, 0x102);
    main.Call("NtYieldExecution"); main.Copy(Seen, Before);
    main.Call("NtResumeThread", {Arg::At(T2),0,0,0}); main.Wait(T2);
    main.Call("NtClose", {Arg::At(T1),0,0,0}); main.Call("NtClose", {Arg::At(T2),0,0,0}); main.Call("NtClose", {Arg::At(Sem),0,0,0});
    p.first.Wait(Alias); p.first.Put(Seen,1); p.first.Ret();
    p.second.Wait(Alias); p.second.Copy(Seen,Seen+8); p.second.Put(Seen,2); p.second.Ret();
    p.Run();
    check(p.fixture.Read<int32_t>(Basic) == 0 && p.fixture.Read(Before) == 1 && p.fixture.Read(Seen) == 2 && p.fixture.Read(Seen+8) == 1,
        "semaphore permits were stolen, over-issued or suspension/lifetime lost");
    std::cout << "PASS: two blocked waiters, exact permit reservation, suspended completion and closed wait-handle lifetime\n";
}
void SemaphoreTimeout() {
    Program p; auto& main = p.main;
    main.Call("NtCreateSemaphore", {Sem,0x1F0003,0,0}, {1}); main.Thread(T1,Worker1); main.Call("NtYieldExecution");
    main.Call("NtDelayExecution", {0,Long,0,0});
    main.Call("NtReleaseSemaphore", {Arg::At(Sem),1,0,0}); main.Query("NtQuerySemaphore",Sem);
    main.Wait(Sem,Zero); main.Wait(T1);
    p.first.Wait(Sem,Short,0x102); p.first.Ret(); p.Run();
    check(p.fixture.Read<int32_t>(Basic) == 1, "expired waiter consumed a later release");
    std::cout << "PASS: real timeout does not consume a later semaphore permit\n";
}
void MutexHandoff() {
    Program p; auto& main = p.main;
    main.Call("NtCreateMutant", {Mutex,0x1F0001,0,1});
    main.Call("NtDuplicateObject", {UINT64_MAX,Arg::At(Mutex),UINT64_MAX,Alias}, {0,0,0});
    main.Wait(Alias,Zero,0xC0000022);
    main.Call("NtQueryMutant", {Arg::At(Alias),0,Basic,8}, {Length}, 0xC0000022);
    main.Query("NtQueryMutant",Mutex,Basic); main.Wait(Mutex,Zero); main.Query("NtQueryMutant",Mutex,Basic+16);
    main.Thread(T1,Worker1); main.Call("NtYieldExecution");
    main.Call("NtReleaseMutant", {Arg::At(Mutex),Previous,0,0}); main.Copy(Previous,Before);
    main.Call("NtYieldExecution", {}, {}, 0x40000024);
    main.Call("NtReleaseMutant", {Arg::At(Mutex),Previous,0,0}); main.Wait(Mutex,Zero,0x102);
    main.Query("NtQueryMutant",Mutex,Basic+32); main.Wait(T1); main.Query("NtQueryMutant",Mutex,Basic+48);
    main.Call("NtReleaseMutant", {Arg::At(Mutex),0,0,0}, {}, 0xC0000046);
    p.first.Call("NtReleaseMutant", {Arg::At(Alias),0,0,0}, {}, 0xC0000046);
    p.first.Wait(Mutex); p.first.Query("NtQueryMutant",Mutex,Basic+64);
    p.first.Call("NtReleaseMutant", {Arg::At(Alias),0,0,0}); p.first.Ret();
    p.Run();
    check(p.fixture.Read<int32_t>(Basic) == 0 && p.fixture.Read<uint8_t>(Basic+4) == 1 && p.fixture.Read<int32_t>(Basic+16) == -1, "mutex recursive query wrong");
    check(p.fixture.Read(Before) == 0xAABBCCDDFFFFFFFFull && p.fixture.Read(Previous) == 0xAABBCCDD00000000ull, "previous mutant count ABI wrong");
    check(p.fixture.Read<uint8_t>(Basic+36) == 0 && p.fixture.Read<uint8_t>(Basic+68) == 1 && p.fixture.Read<int32_t>(Basic+48) == 1,
        "mutant was stolen before handoff or did not release after recursion");
    std::cout << "PASS: recursive mutex ownership, non-owner rejection, direct handoff and owner-only release through a zero-access alias\n";
}
void MutexAbandonment() {
    Program p; auto& main = p.main;
    main.Call("NtCreateMutant", {Mutex,0x1F0001,0,0}); main.Call("NtCreateEvent", {Event,0x1F0003,0,1}, {0});
    main.Thread(T1,Worker1); main.Wait(Event); main.Wait(Mutex,0,0x80);
    main.Query("NtQueryMutant",Mutex,Basic); main.Call("NtReleaseMutant", {Arg::At(Mutex),0,0,0});
    main.Wait(Mutex,Zero); main.Thread(T2,Worker2); main.Call("NtYieldExecution");
    main.Call("NtSuspendThread", {Arg::At(T2),0,0,0}); main.Call("NtReleaseMutant", {Arg::At(Mutex),0,0,0});
    main.Query("NtQueryMutant",Mutex,Basic+16);
    main.Call("NtTerminateThread", {Arg::At(T2),0x44,0,0}); main.Query("NtQueryMutant",Mutex,Basic+32);
    main.Wait(Mutex,Zero,0x80); main.Wait(Mutex,Zero); main.Call("NtReleaseMutant", {Arg::At(Mutex),0,0,0});
    main.Call("NtReleaseMutant", {Arg::At(Mutex),0,0,0}); main.Query("NtQueryMutant",Mutex,Basic+48);
    p.first.Wait(Mutex); p.first.Wait(Mutex); p.first.Call("NtSetEvent", {Arg::At(Event),0,0,0}); p.first.Call("NtYieldExecution"); p.first.Ret();
    // This waiter receives ownership while suspended, then is terminated before
    // its continuation runs. Its result slot must retain the untouched sentinel.
    p.second.Wait(Mutex,0,UINT32_MAX); p.second.Put(Seen,1); p.second.Ret();
    p.Run();
    check(p.fixture.Read<uint8_t>(Basic+4) == 1 && p.fixture.Read<uint8_t>(Basic+5) == 0, "natural exit did not transfer abandoned ownership");
    check(p.fixture.Read<int32_t>(Basic+16) == 0 && p.fixture.Read<uint8_t>(Basic+20) == 0, "suspended waiter did not own handed-off mutex");
    check(p.fixture.Read<int32_t>(Basic+32) == 1 && p.fixture.Read<uint8_t>(Basic+37) == 1 && p.fixture.Read(Seen) == 0,
        "forced termination did not abandon suspended ownership");
    check(p.fixture.Read<int32_t>(Basic+48) == 1 && p.fixture.Read<uint8_t>(Basic+53) == 0, "abandoned status persisted after recovery");
    std::cout << "PASS: abandonment on routine return and forced termination, including ownership handed to a suspended waiter\n";
}
}
int main() {
    BOOL available = FALSE; UINT32 written = 0;
    if (FAILED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &available, sizeof(available), &written)) || !available) {
        std::cout << "SKIP: WHP unavailable; no settings changed\n"; return 77;
    }
    try { SemaphoreValidation(); SemaphoreHandoff(); SemaphoreTimeout(); MutexHandoff(); MutexAbandonment(); return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
