#include "MiniNt.h"
#include "GuestCall.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace av { namespace microvm {
namespace {
using S = GuestScheduler;
using O = GuestObjects;
constexpr uint64_t StackBytes = 0x10000, TebBytes = 0x2000, CurrentThread = UINT64_MAX - 1;
uint64_t Now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 100);
}
WHV_REGISTER_VALUE& R(MicroVm::ThreadContext& cpu, WHV_REGISTER_NAME name) {
    const auto& names = MicroVm::ThreadRegisters();
    return cpu.registers.at(static_cast<size_t>(std::find(names.begin(), names.end(), name) - names.begin()));
}
void Return(MicroVm& vm, uint32_t status) {
    const WHV_REGISTER_NAME name = WHvX64RegisterRax; WHV_REGISTER_VALUE value{}; value.Reg64 = status;
    if (!vm.SetRegisters(&name, 1, &value)) vm.FailExecution(L"Unable to return thread syscall status");
}
struct Backend : VirtualMemoryBackend {
    explicit Backend(MicroVm& machine) : vm(machine) {}
    GuestMemory Memory() const override { return GuestMemory(vm.RamData(), vm.RamSize()); }
    bool SetAccess(uint64_t address, uint64_t bytes, uint8_t access) override { return vm.SetMemoryAccess(address, bytes, access); }
    void FailClosed() override { vm.FailMemoryMappings(); }
    MicroVm& vm;
};
bool Deadline(bool present, int64_t timeout, uint64_t& deadline) {
    deadline = S::Never; if (!present) return true;
    uint64_t delta = 0;
    if (timeout < 0) delta = uint64_t(0) - static_cast<uint64_t>(timeout);
    else if (timeout) {
        FILETIME fileTime{}; GetSystemTimePreciseAsFileTime(&fileTime);
        const auto wall = (uint64_t(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
        delta = static_cast<uint64_t>(timeout) > wall ? static_cast<uint64_t>(timeout) - wall : 0;
    }
    const auto now = Now(); if (delta >= S::Never - now) return false;
    deadline = now + delta; return true;
}
}

MiniNt::~MiniNt() {
    // Mutual thread waits may form reference cycles. Process teardown breaks
    // all wait references while the filesystem and object table still exist.
    for (auto& pair : threads_) pair.second->waitingOn.reset();
}
bool MiniNt::StartThreading(MicroVm& vm, uint32_t& status) {
    if (threading_) { status = threadsStopped_ ? 0xC000010A : 0; return !status; }
    if (heap_.Base() && heap_.Base() < virtualMemory_.End() && virtualMemory_.Base() < heap_.End()) { status = GuestVirtualMemory::Conflict; return false; }
    status = O::NoMemory;
    Backend backend(vm);
    try {
        auto initial = std::make_shared<ThreadObject>(nextThreadId_);
        if (!vm.CaptureThread(initial->cpu)) { status = 0xC0000001; return false; }
        // AVX/XSAVE and interrupt-driven execution are outside this cooperative
        // x87/SSE context format. Do not silently lose extended state.
        if ((R(initial->cpu, WHvX64RegisterCr4).Reg64 & (1ull << 18)) ||
            (R(initial->cpu, WHvX64RegisterRflags).Reg64 & 0x200) ||
            (R(initial->cpu, WHvX64RegisterCs).Segment.Selector & 3) != 3) { status = O::NotSupported; return false; }
        const int terminate = SsnOf("NtTerminateThread"); if (terminate < 0) { status = O::NotSupported; return false; }
        uint64_t base = 0, bytes = 4096;
        status = virtualMemory_.Allocate(backend, base, bytes, 0x3000, 4); if (status) return false;
        threadReturn_ = base;
        // A start routine may return NTSTATUS in EAX. Its return address enters
        // this RX stub: NtTerminateThread(NtCurrentThread(), EAX), then UD2.
        uint8_t code[]{0x89,0xC2,0x48,0xC7,0xC1,0xFE,0xFF,0xFF,0xFF,0xB8,0,0,0,0,0x0F,0x05,0x0F,0x0B};
        const auto ssn = static_cast<uint32_t>(terminate); std::memcpy(code + 10, &ssn, 4);
        std::memcpy(static_cast<uint8_t*>(vm.RamData()) + base, code, sizeof(code));
        uint32_t old = 0; status = virtualMemory_.Protect(backend, base, bytes, 0x20, old);
        if (!status) {
            initial->teb = R(initial->cpu, WHvX64RegisterGs).Segment.Base;
            threads_.emplace(initial->id, initial);
            if (scheduler_.Add(initial->id) && scheduler_.Start(initial->id)) {
                nextThreadId_ += 4; threading_ = true; status = 0; return true;
            }
            status = O::NoMemory;
        }
    } catch (const std::bad_alloc&) { status = O::NoMemory; }
    if (threadReturn_) { uint64_t bytes = 0; virtualMemory_.Free(backend, threadReturn_, bytes, GuestVirtualMemory::Release); threadReturn_ = 0; }
    threads_.clear(); scheduler_ = GuestScheduler{}; return false;
}
bool MiniNt::SaveCurrentThread(MicroVm& vm) {
    const auto it = threads_.find(scheduler_.Current());
    if (it == threads_.end() || !vm.CaptureThread(it->second->cpu)) { vm.FailExecution(L"Unable to capture current guest thread"); return false; }
    auto& rip = R(it->second->cpu, WHvX64RegisterRip).Reg64;
    if (rip > UINT64_MAX - 2) { vm.FailExecution(L"Thread continuation RIP overflow"); return false; }
    rip += 2; return true;
}
void MiniNt::ScheduleThread(MicroVm& vm) {
    for (;;) {
        if (vm.CancelRequested()) { vm.FailExecution(L"Guest scheduler canceled while idle; recreate the VM"); return; }
        const auto next = scheduler_.Select(Now());
        for (auto& pair : threads_) if (scheduler_.Get(pair.first)->state != S::State::Waiting) pair.second->waitingOn.reset();
        if (next) {
            auto& cpu = threads_.at(next)->cpu;
            R(cpu, WHvX64RegisterRax).Reg64 = scheduler_.Get(next)->result;
            vm.RestoreThread(cpu); return;
        }
        if (scheduler_.AllTerminated()) { threadsStopped_ = true; vm.RequestStop(); return; }
        if (scheduler_.NextDeadline() == S::Never) {
            vm.FailExecution(L"Guest scheduler deadlock: no runnable thread and no pending deadline"); return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
bool MiniNt::BlockThread(MicroVm& vm, std::shared_ptr<GuestObject> object, bool hasTimeout, int64_t timeout, uint32_t& failure, bool delay) {
    uint64_t deadline = 0;
    if (!Deadline(hasTimeout, timeout, deadline)) { failure = O::InvalidParameter; return false; }
    if (!threading_ && !hasTimeout) { failure = O::NotSupported; return false; }
    if (!StartThreading(vm, failure)) return false;
    const auto current = scheduler_.Current();
    if (!SaveCurrentThread(vm)) return true;
    threads_.at(current)->waitingOn = object;
    if (!scheduler_.Wait(reinterpret_cast<uint64_t>(object.get()), deadline, delay)) { vm.FailExecution(L"Invalid guest thread wait transition"); return true; }
    ScheduleThread(vm); return true;
}
size_t MiniNt::WakeThreads(const std::shared_ptr<GuestObject>& object, bool all) {
    return threading_ ? scheduler_.Signal(reinterpret_cast<uint64_t>(object.get()), all, Now()) : 0;
}
bool MiniNt::ThreadMemoryOverlap(uint64_t address, uint64_t bytes) const {
    if (!threading_) return false;
    if (bytes > UINT64_MAX - address) return true;
    const auto overlaps = [&](uint64_t base, uint64_t length) { return base && address < base + length && base < address + bytes; };
    if (overlaps(threadReturn_, 4096)) return true;
    for (const auto& pair : threads_) if (pair.second->owned &&
        (overlaps(pair.second->stack, StackBytes) || overlaps(pair.second->teb, TebBytes))) return true;
    return false;
}
void MiniNt::ReleaseThreadMemory(MicroVm& vm, uint64_t id) {
    const auto& thread = threads_.at(id);
    thread->waitingOn.reset(); if (!thread->owned) { thread->teb = 0; return; }
    Backend backend(vm);
    for (auto* address : {&thread->stack, &thread->teb}) if (*address) {
        uint64_t bytes = 0;
        if (virtualMemory_.Free(backend, *address, bytes, GuestVirtualMemory::Release)) vm.FailExecution(L"Unable to release guest thread memory");
        *address = 0;
    }
}
void MiniNt::StopThreads(MicroVm& vm, uint32_t code) {
    for (const auto& pair : threads_) { scheduler_.Terminate(pair.first, code); ReleaseThreadMemory(vm, pair.first); }
    for (const auto& pair : ownedMutants_) pair.second->state.Abandon(pair.second->state.Owner());
    ownedMutants_.clear();
    threadsStopped_ = true;
}

bool MiniNt::DispatchThreads(MicroVm& vm, const std::string& name) {
    const bool create = name == "NtCreateThreadEx", suspend = name == "NtSuspendThread", resume = name == "NtResumeThread";
    if (!create && !suspend && !resume && name != "NtTerminateThread" && name != "NtYieldExecution" && name != "NtDelayExecution" && name != "NtQueryInformationThread") return false;
    detail::ObjectCall call(vm, virtualMemory_);
    const auto finish = [&](uint32_t status) { call.Status(status); report_.Add(ReportBuilder::Cat::Process, name, "", "NTSTATUS " + std::to_string(status)); return true; };
    if (!call.valid) return finish(0xC0000001);
    if (threading_) scheduler_.Expire(Now());
    const auto a1 = call.Arg(1), a2 = call.Arg(2), a3 = call.Arg(3), a4 = call.Arg(4);
    try {
        if (create) {
            if (!call.Access(a1, 8, 2)) return finish(0xC0000005);
            if (a4 != UINT64_MAX) return finish(O::InvalidHandle);
            uint64_t args[7]{};
            for (unsigned i = 0; i < 7; ++i) if (!call.Stack(i + 5, args[i])) return finish(0xC0000005);
            if ((static_cast<uint32_t>(args[2]) & ~uint32_t(1)) || args[3] || args[4] || args[5] || args[6]) return finish(O::NotSupported);
            if (!call.Access(args[0], 1, 4)) return finish(0xC0000005);
            uint32_t attributes = 0, desired = 0; uint64_t root = 0; std::string objectName;
            auto status = call.Attributes(a3, attributes, root, objectName); if (status) return finish(status);
            if (root || !objectName.empty()) return finish(O::NotSupported);
            status = O::Normalize(ObjectKind::Thread, static_cast<uint32_t>(a2), desired); if (status) return finish(status);
            if (!objects_.HasRoom()) return finish(O::NoMemory);
            if (!StartThreading(vm, status)) return finish(status);
            for (auto it = threads_.begin(); it != threads_.end();) {
                if (scheduler_.Get(it->first)->state == S::State::Terminated && it->second.use_count() == 1) { scheduler_.Remove(it->first); it = threads_.erase(it); }
                else ++it;
            }
            if (threads_.size() >= 64 || nextThreadId_ > UINT64_MAX - 4) return finish(O::NoMemory);
            auto thread = std::make_shared<ThreadObject>(nextThreadId_); nextThreadId_ += 4;
            if (!vm.CaptureThread(thread->cpu)) return true;
            thread->owned = true; Backend backend(vm); uint64_t bytes = StackBytes;
            status = virtualMemory_.Allocate(backend, thread->stack, bytes, 0x3000, 4);
            if (status) return finish(status);
            bytes = TebBytes; status = virtualMemory_.Allocate(backend, thread->teb, bytes, 0x3000, 4);
            uint64_t handle = 0; bool inserted = false, added = false;
            const auto rollback = [&] {
                if (added) scheduler_.Remove(thread->id);
                if (inserted) threads_.erase(thread->id);
                for (auto address : {thread->stack, thread->teb}) if (address) { uint64_t size = 0; virtualMemory_.Free(backend, address, size, GuestVirtualMemory::Release); }
            };
            if (!status) {
                uint64_t bottom = thread->stack, guard = 4096; uint32_t previous = 0;
                status = virtualMemory_.Protect(backend, bottom, guard, 1, previous);
            }
            if (status) { rollback(); return finish(status); }
            try {
                // Keep the process paging/segment setup, but start with clean
                // GPR, x87 and SSE state and the x64 routine-entry ABI.
                for (size_t i = 0; i < 18; ++i) thread->cpu.registers[i] = {};
                for (size_t i = 36; i < thread->cpu.registers.size(); ++i) thread->cpu.registers[i] = {};
                R(thread->cpu, WHvX64RegisterFpControlStatus).FpControlStatus.FpControl = 0x37F;
                R(thread->cpu, WHvX64RegisterXmmControlStatus).XmmControlStatus.XmmStatusControl = 0x1F80;
                R(thread->cpu, WHvX64RegisterXmmControlStatus).XmmControlStatus.XmmStatusControlMask = 0xFFFF;
                R(thread->cpu, WHvX64RegisterRip).Reg64 = args[0]; R(thread->cpu, WHvX64RegisterRcx).Reg64 = args[1];
                R(thread->cpu, WHvX64RegisterRsp).Reg64 = thread->stack + StackBytes - 0x28;
                R(thread->cpu, WHvX64RegisterRflags).Reg64 = 2;
                R(thread->cpu, WHvX64RegisterGs).Segment.Base = thread->teb;
                call.memory.Write(thread->stack + StackBytes - 0x28, threadReturn_);
                call.memory.Write(thread->teb + 8, thread->stack + StackBytes);
                call.memory.Write(thread->teb + 0x10, thread->stack + 4096);
                call.memory.Write(thread->teb + 0x30, thread->teb);
                call.memory.Write(thread->teb + 0x40, uint64_t(0x1000)); call.memory.Write(thread->teb + 0x48, thread->id);
                uint64_t peb = 0; const auto parentTeb = threads_.at(scheduler_.Current())->teb;
                if (parentTeb && parentTeb <= UINT64_MAX - 0x60 && call.Access(parentTeb + 0x60, 8, 1)) call.memory.Read(parentTeb + 0x60, peb);
                call.memory.Write(thread->teb + 0x60, peb); call.memory.Write(thread->teb + 0x1478, thread->stack);
                threads_.emplace(thread->id, thread); inserted = true;
                added = scheduler_.Add(thread->id, (args[2] & 1) != 0);
                if (!added) status = O::NoMemory;
                else status = objects_.Open(thread, desired, attributes, handle);
            } catch (const std::bad_alloc&) { status = O::NoMemory; }
            if (status) { rollback(); return finish(status); }
            call.memory.Write(a1, handle); return finish(0);
        }
        if (name == "NtYieldExecution") {
            if (!threading_ || !scheduler_.OtherReady()) return finish(0x40000024);
            Return(vm, 0); if (!SaveCurrentThread(vm)) return true;
            scheduler_.Relinquish(); ScheduleThread(vm); return true;
        }
        if (name == "NtDelayExecution") {
            if (a1 & 0xFF) return finish(O::NotSupported);
            int64_t interval = 0; if (!call.Read(a2, interval)) return finish(0xC0000005);
            if (!interval) {
                if (threading_ && scheduler_.OtherReady()) { Return(vm, 0); if (SaveCurrentThread(vm)) { scheduler_.Relinquish(); ScheduleThread(vm); } return true; }
                return finish(0);
            }
            uint32_t failure = 0;
            if (BlockThread(vm, {}, true, interval, failure, true)) return true;
            return finish(failure);
        }
        if (!threading_) return finish(O::NotSupported);
        std::shared_ptr<ThreadObject> thread;
        if (a1 == CurrentThread) thread = threads_.at(scheduler_.Current());
        else {
            const auto required = name == "NtQueryInformationThread" ? 0x40u : name == "NtTerminateThread" ? 1u : 2u;
            const auto status = objects_.Reference(a1, required, thread); if (status) return finish(status);
        }
        if (name == "NtQueryInformationThread") {
            if (static_cast<uint32_t>(a2) != 0) return finish(0xC0000003);
            uint64_t lengthOut = 0; if (!call.Stack(5, lengthOut) || (lengthOut && !call.Access(lengthOut, 4, 2))) return finish(0xC0000005);
            if (static_cast<uint32_t>(a4) < 48) { if (lengthOut) call.memory.Write(lengthOut, uint32_t(48)); return finish(0xC0000004); }
            if (!call.Access(a3, 48, 2)) return finish(0xC0000005);
            if (lengthOut && a3 < lengthOut + 4 && lengthOut < a3 + 48) return finish(O::InvalidParameter);
            struct Basic { uint32_t exitCode, padding; uint64_t teb, process, thread, affinity; int32_t priority, basePriority; };
            static_assert(sizeof(Basic) == 48, "x64 THREAD_BASIC_INFORMATION");
            const Basic information{scheduler_.Get(thread->id)->exitCode, 0, thread->teb, 0x1000, thread->id, 1, 8, 8};
            call.memory.Write(a3, information); if (lengthOut) call.memory.Write(lengthOut, uint32_t(48)); return finish(0);
        }
        if (suspend || resume) {
            if (a2 && !call.Access(a2, 4, 2)) return finish(0xC0000005);
            const bool self = thread->id == scheduler_.Current();
            if (suspend && self) { Return(vm, 0); if (!SaveCurrentThread(vm)) return true; }
            uint32_t previous = 0; const auto status = suspend ? scheduler_.Suspend(thread->id, previous) : scheduler_.Resume(thread->id, previous);
            if (!status && a2) call.memory.Write(a2, previous);
            if (!status && suspend && self) { ScheduleThread(vm); return true; }
            return finish(status);
        }
        if (name == "NtTerminateThread") {
            const bool self = thread->id == scheduler_.Current();
            if (!scheduler_.Terminate(thread->id, static_cast<uint32_t>(a2))) return finish(0xC000004B);
            AbandonMutants(vm, thread->id);
            ReleaseThreadMemory(vm, thread->id); WakeThreads(thread, true);
            if (self) { if (scheduler_.AllTerminated()) exitCode_ = static_cast<int>(a2); ScheduleThread(vm); return true; }
            return finish(0);
        }
    } catch (const std::bad_alloc&) { return finish(O::NoMemory); }
    return false;
}
} }
