#pragma once
#ifdef __linux__
#include "../../linux/KvmMachine.h"
#else
#include "MicroVmMonitor.h"
#endif
#include <array>
#include <atomic>
#include <thread>
namespace av::microvm {
// Additional processors share this VM's RAM and partition; no nested emulator.
// Device exits belong to the bootstrap CPU. APs execute the resource kernel.
class SmpMachine {
  public:
    ~SmpMachine();
    void Create(uint64_t ramBytes, uint32_t count, const std::string &brand);
    void Entry(uint64_t entry, uint64_t boot);
    bool Run(uint64_t budget);
    bool Pause() { return primary_.RequestPause(); }
    bool Cancel() { return primary_.Cancel(); }
    bool Capture(state::MachineState &);
    bool Restore(const state::MachineState &);
    MicroVm &Primary() { return primary_; }
    uint32_t Count() const { return count_; }

  private:
    void RunAp(uint32_t);
    void StopAps();
    template <class F> bool Select(uint32_t, F);
    MicroVm primary_;
    uint32_t count_ = 0;
    std::atomic<bool> stop_{false}, failed_{false};
    struct Ap {
        std::thread thread;
        std::atomic<bool> finished{true};
#ifdef __linux__
        int fd = -1;
        kvm_run *run = nullptr;
        std::atomic<pthread_t> owner{};
        std::atomic<bool> ready{false};
#endif
    };
    std::array<Ap, 3> aps_;
};
} // namespace av::microvm
