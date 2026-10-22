#include "SmpMachine.h"
#include "guardian/CpuTopology.h"
#include <cstring>
#ifdef __linux__
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace av::microvm {
using state::Require;
SmpMachine::~SmpMachine() {
    StopAps();
#ifdef __linux__
    for (auto &ap : aps_) {
        if (ap.run)
            munmap(ap.run, primary_.runBytes_);
        if (ap.fd >= 0)
            ::close(ap.fd);
    }
#endif
}
template <class F> bool SmpMachine::Select(uint32_t id, F fn) {
#ifdef __linux__
    const auto cpu = primary_.cpu_;
    auto *run = primary_.run_;
    const auto topology = primary_.topologyId_;
    const auto pending = primary_.ioPending_;
    const auto ran = primary_.hasRun_;
    if (id) {
        primary_.cpu_ = aps_[id - 1].fd;
        primary_.run_ = aps_[id - 1].run;
        primary_.ioPending_ = false;
        primary_.hasRun_ = false;
    }
    primary_.topologyId_ = id;
    auto restore = [&] {
        primary_.cpu_ = cpu;
        primary_.run_ = run;
        primary_.topologyId_ = topology;
        primary_.ioPending_ = pending;
        primary_.hasRun_ = ran;
    };
#else
    const auto cpu = primary_.registerIndex_;
    primary_.registerIndex_ = id;
    auto restore = [&] { primary_.registerIndex_ = cpu; };
#endif
    try {
        const auto result = fn();
        restore();
        return result;
    } catch (...) {
        restore();
        throw;
    }
}
void SmpMachine::Create(uint64_t ram, uint32_t count, const std::string &brand) {
    Require(!count_ && count >= 1 && count <= 4 && ram >= 16ull << 20 && ram <= 1ull << 30 && !(ram & 4095),
            "SMP VM bounds rejected");
    primary_.ConfigureCpuIdentity(brand);
#ifdef __linux__
    primary_.topologyCpus_ = count;
    primary_.topologyEnabled_ = true;
    Require(primary_.Create(size_t(ram)), "SMP KVM creation failed");
    count_ = count;
    for (uint32_t id = 1; id < count; ++id) {
        auto &ap = aps_[id - 1];
        ap.fd = ioctl(primary_.vm_, KVM_CREATE_VCPU, id);
        Require(ap.fd >= 0, "Cannot create additional KVM processor");
        auto *map = mmap(nullptr, primary_.runBytes_, PROT_READ | PROT_WRITE, MAP_SHARED, ap.fd, 0);
        Require(map != MAP_FAILED, "Cannot map additional KVM processor");
        ap.run = static_cast<kvm_run *>(map);
        std::vector<uint8_t> bytes(sizeof(kvm_cpuid2) + 256 * sizeof(kvm_cpuid_entry2));
        auto *table = reinterpret_cast<kvm_cpuid2 *>(bytes.data());
        table->nent = 256;
        Require(ioctl(primary_.cpu_, KVM_GET_CPUID2, table) == 0, "Cannot obtain SMP CPUID");
        for (uint32_t i = 0; i < table->nent; ++i) {
            auto &e = table->entries[i];
            guardian::hardware::CpuTopology(count, id, e.function, e.index, e.eax, e.ebx, e.ecx, e.edx);
        }
        Require(ioctl(ap.fd, KVM_SET_CPUID2, table) == 0, "Cannot configure SMP CPUID");
    }
#else
    Require(primary_.Create(size_t(ram), 0, count), "SMP WHP creation failed");
    count_ = count;
#endif
}
void SmpMachine::Entry(uint64_t entry, uint64_t boot) {
    for (uint32_t id = 0; id < count_; ++id) {
        const uint64_t stack = 0x400000 + uint64_t(id + 1) * 0x20000 - 0x28;
        Require(Select(id,
                       [&] {
                           if (!primary_.SetupLongMode(entry, stack))
                               return false;
#ifdef __linux__
                           return primary_.BootArguments(boot, id);
#else
            const WHV_REGISTER_NAME names[]{WHvX64RegisterRcx,WHvX64RegisterRdx};WHV_REGISTER_VALUE values[2]{};values[0].Reg64=boot;values[1].Reg64=id;return primary_.SetRegisters(names,2,values);
#endif
                       }),
                "SMP entry setup failed");
        Require(primary_.SetMemoryAccess(0x400000 + uint64_t(id) * 0x20000, 4096, 0),
                "SMP stack guard failed");
    }
}
void SmpMachine::RunAp(uint32_t id) {
#ifdef __linux__
    auto &ap = aps_[id - 1];
    sigset_t signal, previous;
    sigemptyset(&signal);
    sigaddset(&signal, SIGRTMIN + 5);
    pthread_sigmask(SIG_UNBLOCK, &signal, &previous);
    ap.owner = pthread_self();
    ap.ready = true;
    while (!stop_) {
        __atomic_store_n(&ap.run->immediate_exit, 0, __ATOMIC_RELAXED);
        if (stop_)
            break;
        const auto rc = ioctl(ap.fd, KVM_RUN, 0);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0 || (ap.run->exit_reason != KVM_EXIT_INTR)) {
            failed_ = true;
            primary_.Cancel();
            break;
        }
    }
    ap.ready = false;
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
#else
    while (!stop_) {
        WHV_RUN_VP_EXIT_CONTEXT exit{};
        const auto hr = WHvRunVirtualProcessor(primary_.part_, id, &exit, sizeof(exit));
        if (FAILED(hr)) {
            failed_ = true;
            primary_.Cancel();
            break;
        }
        if (exit.ExitReason == WHvRunVpExitReasonCanceled)
            continue;
        if (exit.ExitReason == WHvRunVpExitReasonX64Cpuid) {
            if (!primary_.HandleCpuid(exit, id)) {
                failed_ = true;
                primary_.Cancel();
                break;
            }
            const auto name = WHvX64RegisterRip;
            WHV_REGISTER_VALUE next{};
            next.Reg64 = exit.VpContext.Rip + exit.VpContext.InstructionLength;
            if (FAILED(WHvSetVirtualProcessorRegisters(primary_.part_, id, &name, 1, &next))) {
                failed_ = true;
                primary_.Cancel();
                break;
            }
        } else {
            failed_ = true;
            primary_.Cancel();
            break;
        }
    }
#endif
}
void SmpMachine::StopAps() {
    stop_ = true;
    bool complete = false;
    while (!complete) {
        complete = true;
        for (uint32_t id = 1; id < count_; ++id) {
            auto &ap = aps_[id - 1];
            if (!ap.thread.joinable() || ap.finished)
                continue;
            complete = false;
#ifdef __linux__
            if (ap.ready) {
                __atomic_store_n(&ap.run->immediate_exit, 1, __ATOMIC_RELAXED);
                pthread_kill(ap.owner.load(), SIGRTMIN + 5);
            }
#else
            WHvCancelRunVirtualProcessor(primary_.part_, id, 0);
#endif
        }
        if (!complete)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (auto &ap : aps_)
        if (ap.thread.joinable())
            ap.thread.join();
}
bool SmpMachine::Run(uint64_t budget) {
    Require(count_ > 0, "SMP VM not initialized");
    stop_ = false;
    failed_ = false;
    try {
        for (uint32_t id = 1; id < count_; ++id) {
            aps_[id - 1].finished = false;
            try {
                aps_[id - 1].thread = std::thread([this, id] {
                    RunAp(id);
                    aps_[id - 1].finished = true;
                });
            } catch (...) {
                aps_[id - 1].finished = true;
                throw;
            }
        }
        const auto result = primary_.Run(budget);
        StopAps();
        if (failed_)
            primary_.FailExecution(L"Secondary processor execution failed");
        return result && !failed_;
    } catch (...) {
        StopAps();
        throw;
    }
}
bool SmpMachine::Capture(state::MachineState &saved) {
    state::Writer cpus;
    cpus.Pod(uint64_t(0x32504D53564147ull));
    cpus.Pod(count_);
    state::MachineState out;
    for (uint32_t id = 0; id < count_; ++id) {
        state::MachineState part;
        Require(Select(id, [&] { return primary_.Capture(part); }), "SMP processor capture failed");
        cpus.Bytes(part.processor);
        if (!id)
            out = std::move(part);
    }
    out.processor = std::move(cpus.data);
    saved = std::move(out);
    return true;
}
bool SmpMachine::Restore(const state::MachineState &saved) {
    saved.Validate();
    state::Reader r(saved.processor);
    Require(r.Pod<uint64_t>() == 0x32504D53564147ull && r.Pod<uint32_t>() == count_,
            "SMP checkpoint topology rejected");
    std::vector<std::vector<uint8_t>> cpus;
    for (uint32_t id = 0; id < count_; ++id)
        cpus.push_back(r.Bytes(128ull << 10));
    r.End();
    state::MachineState part = saved;
    for (uint32_t id = 0; id < count_; ++id) {
        part.processor = std::move(cpus[id]);
        Require(Select(id, [&] { return primary_.Restore(part); }), "SMP processor restore failed");
    }
    return true;
}
} // namespace av::microvm
