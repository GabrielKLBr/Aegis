// MiniNt.h - a "mini NT kernel" that services the REAL ntdll syscall numbers.
//
// Model B: instead of our own syscall numbers + synthetic stubs, this dispatches
// by the actual SSNs the live ntdll uses (read from the loaded ntdll at startup),
// implementing a core of Nt* with real semantics. That lets the REAL ntdll.dll be
// mapped into the guest and its stubs executed -- the path that scales to loading
// real DLLs. GUI DLLs (user32/gdi32) still need win32k and are out of scope here.
#pragma once

#include "../vmm/MicroVmMonitor.h"
#include "ReportBuilder.h"
#include "VirtualFs.h"
#include "GuestHeap.h"
#include "GuestVirtualMemory.h"
#include "GuestObjects.h"
#include "GuestScheduler.h"
#include "GuestSynchronization.h"
#include "../../../shared/guardian/HardwareProjection.h"
#include <optional>

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <functional>

namespace av {
namespace microvm {

class MiniNt {
public:
    MiniNt();
    ~MiniNt();

    // Resolve a name<->SSN both ways from the live ntdll (build-portable).
    int  SsnOf(const std::string& ntName) const;     // -1 if not found
    std::string NameOf(uint32_t ssn) const;          // "" if unknown

    void Dispatch(MicroVm& vm);                       // OnSyscall handler

    int ExitCode() const { return exitCode_; }
    const ReportBuilder& Report() const { return report_; }
    VirtualFs& Fs() { return fs_; }
    void ConfigureHardware(const guardian::hardware::Info&);

    // The loader needs far more than the default 4 MiB arena, and we don't want
    // to stop at the first unimplemented call -- we want the whole trace.
    void SetArena(uint64_t base, uint64_t end) { virtualMemory_.Configure(base, end); }
    void SetLoaderMode(bool on) { loaderMode_ = on; }
    // Zero the per-call transient counters (for fuzzers driving many calls).
    void ResetTransient() { unimpl_ = 0; reachedContinue_ = false; exitCode_ = -1; }
    // A single configured heap serviced by our allocator. This deliberately
    // bypasses ntdll's uninitialized native heap manager; it is not that manager.
    void SetHeapRegion(uint64_t base, uint64_t end) {
        if (virtualMemory_.Bound() && base < virtualMemory_.End() && virtualMemory_.Base() < end)
            throw std::invalid_argument("Heap overlaps the active virtual memory arena");
        heap_.Configure(base, end);
    }
    bool ReachedContinue() const { return reachedContinue_; }
    uint64_t ContinueRip() const { return continueRip_; }

private:
    void LoadSyscallTable();                           // parse live ntdll
    void SeedRegistry();                               // seed the NT \Registry namespace
    void SeedFilesystem();                             // seed normal files + block VM artifacts
public:
    // Seed KUSER_SHARED_DATA with a believable uptime (anti-VM timing). The page
    // is mapped at 0x7FFE0000; pass the GPA backing that 2 MiB page.
    void SeedSharedData(MicroVm& vm, uint64_t backingGpa);
private:
    // Seed a believable bare-metal "victim machine" identity (anti-VM personality).
    void SeedPersonality(const std::function<std::vector<uint8_t>(const std::string&)>& sz,
                         const std::function<std::vector<uint8_t>(uint32_t)>& dw);
    void DispatchHeap(MicroVm& vm, uint32_t op);       // sentinel heap ops (hot-patch)
    bool DispatchVirtualMemory(MicroVm& vm, const std::string& name);
    bool DispatchObjects(MicroVm& vm, const std::string& name);
    bool DispatchThreads(MicroVm& vm, const std::string& name);
    bool DispatchSynchronization(MicroVm& vm, const std::string& name);
    void AbandonMutants(MicroVm& vm, uint64_t thread);
    bool StartThreading(MicroVm& vm, uint32_t& status);
    bool SaveCurrentThread(MicroVm& vm);
    void ScheduleThread(MicroVm& vm);
    bool BlockThread(MicroVm& vm, std::shared_ptr<GuestObject> object, bool hasTimeout, int64_t timeout, uint32_t& failure, bool delay = false);
    size_t WakeThreads(const std::shared_ptr<GuestObject>& object, bool all);
    bool ThreadMemoryOverlap(uint64_t address, uint64_t bytes) const;
    void ReleaseThreadMemory(MicroVm& vm, uint64_t id);
    void StopThreads(MicroVm& vm, uint32_t exitCode);
    static uint64_t ImageSize(const std::vector<uint8_t>& file);

    std::map<uint32_t, std::string> ssnToName_;
    std::map<std::string, uint32_t> nameToSsn_;
    ReportBuilder report_;
    VirtualFs fs_;

    GuestVirtualMemory virtualMemory_;                  // private pages + section views

    GuestHeap heap_;                                      // hot-patched Rtl*Heap

    struct FileObject : GuestObject {
        static constexpr ObjectKind Kind = ObjectKind::File;
        FileObject(VirtualFs& owner, uint64_t value, std::string name)
            : GuestObject(Kind), fs(owner), handle(value), path(std::move(name)) {}
        ~FileObject() override { fs.Close(handle); }
        VirtualFs& fs; uint64_t handle; std::string path; uint64_t position = 0;
    };
    struct SectionObject : GuestObject {
        static constexpr ObjectKind Kind = ObjectKind::Section;
        SectionObject() : GuestObject(Kind) {}
        std::shared_ptr<FileObject> file;
        uint64_t size = 0; bool image = false; uint32_t protect = 0;
    };
    struct EventObject : GuestObject {
        static constexpr ObjectKind Kind = ObjectKind::Event;
        EventObject(bool state, bool automatic) : GuestObject(Kind), signaled(state), autoReset(automatic) {}
        bool signaled, autoReset;
    };
    struct KeyObject : GuestObject {
        static constexpr ObjectKind Kind = ObjectKind::Key;
        explicit KeyObject(std::string value) : GuestObject(Kind), path(std::move(value)) {}
        std::string path;
    };
    struct SemaphoreObject : GuestObject {
        static constexpr ObjectKind Kind = ObjectKind::Semaphore;
        SemaphoreObject(int32_t count, int32_t maximum) : GuestObject(Kind), state(count, maximum) {}
        GuestSemaphore state;
    };
    struct MutantObject : GuestObject {
        static constexpr ObjectKind Kind = ObjectKind::Mutant;
        MutantObject() : GuestObject(Kind) {}
        GuestMutant state;
    };
    uint32_t AcquireMutant(const std::shared_ptr<MutantObject>& object, uint64_t thread);
    void HandoffMutant(MicroVm& vm, const std::shared_ptr<MutantObject>& object);
    struct ThreadObject : GuestObject {
        static constexpr ObjectKind Kind = ObjectKind::Thread;
        explicit ThreadObject(uint64_t value) : GuestObject(Kind), id(value) {}
        uint64_t id, teb = 0, stack = 0;
        bool owned = false;
        MicroVm::ThreadContext cpu;
        std::shared_ptr<GuestObject> waitingOn;
    };
    // Declared after fs_: all handle/view references are destroyed before fs_.
    GuestObjects objects_;
    std::map<uint64_t, std::shared_ptr<SectionObject>> views_;
    GuestScheduler scheduler_;
    std::map<uint64_t, std::shared_ptr<ThreadObject>> threads_;
    // Ownership retains a mutant even after its final handle closes. Bounded
    // separately because guest code may close handles while retaining ownership.
    std::map<MutantObject*, std::shared_ptr<MutantObject>> ownedMutants_;
    uint64_t nextThreadId_ = 0x1004, threadReturn_ = 0;
    bool threading_ = false, threadsStopped_ = false;

    // NT registry: \Registry\... key (lowercased) -> value name (lowercased) ->
    // typed value. Seeded with the system "personality"; writes captured here.
    struct RegVal { uint32_t type; std::vector<uint8_t> bytes; };
    std::map<std::string, std::map<std::string, RegVal>> registry_;
    std::optional<guardian::hardware::Info> hardware_;

    bool     loaderMode_      = false;
    bool     reachedContinue_ = false;
    uint64_t continueRip_     = 0;
    int      exitCode_  = -1;
    int      unimpl_    = 0;
};

} // namespace microvm
} // namespace av
