#pragma once
#include "../storage/SparseDisk.h"
#include "../vmm/SmpMachine.h"
#include "KernelImage.h"
#include "SessionSnapshot.h"
#include "guardian/ResourceAbi.h"
#include <chrono>
namespace av::microvm::boot {
struct BootResult;
class ResourceSession {
  public:
    void Prepare(const KernelImage &, uint64_t, uint64_t, uint32_t, const storage::SparseOptions &, uint64_t);
    BootResult Run(std::chrono::milliseconds, bool suspendAtDeadline = false);
    bool Pause() { return machine_.Pause(); }
    uint64_t ElapsedNs() const { return elapsedNs_; }
    bool Paused() const { return paused_; }
    void PauseAtRequest(uint64_t n) { pauseAt_ = n; }
    SessionSnapshot Capture();
    void Restore(const SessionSnapshot &);
    std::vector<uint8_t> DiskSnapshot() { return disk_->Snapshot(); }
    bool QuerySpace(guardian::scalable::Space &, uint64_t *charge = nullptr);
    MicroVm &Machine() { return machine_.Primary(); }

  private:
    void Control(const MicroVm::IoAccess &);
    SmpMachine machine_;
    std::unique_ptr<storage::SparseDisk> disk_;
    guardian::resource::Boot boot_{};
    guardian::resource::Report report_{};
    uint64_t imageHash_ = 0, sequence_ = 0, elapsedNs_ = 0, pauseAt_ = 0;
    bool prepared_ = false, ran_ = false, paused_ = false, reported_ = false;
};
} // namespace av::microvm::boot
