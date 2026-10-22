// MicroVmMonitor.h - the MicroVM engine (roadmap step 2 substrate).
//
// Generalizes the WhpPoc proof of concept into the architecture from
// docs/ARCHITECTURE-microvm-sandbox.md: a `MicroVm` owns a WHP partition, its
// guest RAM and one vCPU, and dispatches the guest's device I/O (VM-exits) to
// pluggable `IoDevice` models (PvConsole, PvDisk, ...). The guest can never
// touch the host: every OUT/IN traps here and is serviced in user mode.
#pragma once

#include "../state/State.h"
#include <windows.h>
#include <winhvplatform.h>

#include <cstdint>
#include <string>
#include <map>
#include <functional>
#include <vector>
#include <atomic>

namespace av {
namespace microvm {

// A paravirtual device. The guest reaches it through I/O ports; the host
// services the access. Real device models (PvConsole, PvDisk) derive from this.
struct IoDevice {
    virtual ~IoDevice() = default;
    virtual void OnOut(uint16_t port, uint8_t value) = 0;     // guest -> device
    virtual uint8_t OnIn(uint16_t /*port*/) { return 0xFF; }  // device -> guest
};

class MicroVm {
public:
    MicroVm() = default;
    ~MicroVm();

    MicroVm(const MicroVm&) = delete;
    MicroVm& operator=(const MicroVm&) = delete;

    // Create the partition and guest RAM. Existing image contracts use one CPU;
    // the resource kernel can provision up to four real processors.
    // `exceptionExitBitmap` (bit N = CPU exception vector N) requests that those
    // exceptions trap to the VMM instead of the guest IDT -- e.g. (1<<6)|(1<<13)
    // to catch #UD (a SYSCALL with SCE off) and #GP (a privileged-op attempt).
    bool Create(size_t ramBytes = 0x100000 /* 1 MiB */, uint64_t exceptionExitBitmap = 0, uint32_t cpuCount = 1);

    // Copy 16-bit real-mode code into guest RAM at `gpa` and point CS:RIP at it.
    bool LoadRealModeCode(const void* code, size_t len, uint64_t gpa = 0x1000);

    // Bring the vCPU up in 64-bit long mode with identity-mapped paging, and set
    // RIP/RSP. Copy the guest code into RAM yourself (via RamData()) first. When
    // `cpl3` is set, the guest runs as user (ring 3): privileged instructions
    // fault, and SYSCALL (SCE is off) raises #UD -- both trappable hardening.
    bool SetupLongMode(uint64_t entryGpa, uint64_t stackGpa, bool cpl3 = false);

    // Point the GS base at the guest's TEB (so GS:[0x30]/GS:[0x60] work like
    // real ntdll/CRT code). Call after SetupLongMode.
    bool SetGsBase(uint64_t base);

    // Map one extra 2 MiB page at `guestVa` (2 MiB aligned, in the 1..2 GiB
    // range) backed by `backingGpa` in guest RAM. Used to place KUSER_SHARED_DATA
    // at its fixed address 0x7FFE0000, which real ntdll syscall stubs read.
    bool MapPage2MB(uint64_t guestVa, uint64_t backingGpa);

    // Change SLAT permissions on page-aligned guest physical RAM, while stopped.
    // Bits: read=1, write=2, execute=4; zero unmaps. No host page permissions change.
    bool SetMemoryAccess(uint64_t gpa, uint64_t bytes, uint8_t access);
    bool CanAccessMemory(uint64_t gpa, uint64_t bytes, uint8_t access) const;
    void FailMemoryMappings() { memoryFatal_ = true; stopRequested_ = true; }
    bool MemoryMappingsFailed() const { return memoryFatal_; }
    bool LastMemoryAccessFault() const { return memoryAccessFault_; }
    uint64_t LastMemoryAccessGpa() const { return memoryAccessGpa_; }
    uint8_t LastMemoryAccessType() const { return memoryAccessType_; }

    // Anti-VM: trap CPUID and present a believable bare-metal CPU -- clear the
    // hypervisor-present bit (leaf 1 ECX[31]), blank the hypervisor vendor leaf
    // (0x40000000), and spoof the vendor (leaf 0) + brand string (0x80000002..4).
    // `logicalCpus` is also reported in leaf 1 EBX[23:16]. Call BEFORE Create().
    void SpoofCpuid(const std::string& vendor12, const std::string& brand48,
                    uint32_t logicalCpus = 8);
    void ConfigureCpuIdentity(const std::string& brand) {
        state::Require(!part_&&!spoofCpuid_&&!brand.empty()&&brand.size()<=48,"CPU identity must be configured before VM creation");
        cpuIdentity_=brand;
    }

    // Register the synthetic-kernel handler for a trapped SYSCALL (#UD 0F 05).
    using SyscallHandler = std::function<void(MicroVm&)>;
    void OnSyscall(SyscallHandler fn) { onSyscall_ = std::move(fn); }

    // A syscall handler calls this to make Run() stop cleanly (e.g. ExitProcess).
    void RequestStop() { stopRequested_ = true; }
    // A scheduler restored another thread's RIP; skip the usual SYSCALL +2.
    void RedirectSyscall() { syscallRedirected_ = true; }
    void FailExecution(const std::wstring& reason) { executionFatal_ = true; stopRequested_ = true; error_ = reason; }
    bool CancelRequested() const { return cancelRequested_.load(); }
    struct ThreadContext { std::vector<WHV_REGISTER_VALUE> registers; };
    static const std::vector<WHV_REGISTER_NAME>& ThreadRegisters();
    bool CaptureThread(ThreadContext& context);
    bool RestoreThread(const ThreadContext& context);

    // Force a running vCPU to exit (from another thread) -- a watchdog for guests
    // that spin with no VM-exits. Run() then reports the RIP it was canceled at.
    bool Cancel();
    bool RequestPause();
    bool Paused() const {return paused_;}
    bool Capture(state::MachineState&);
    bool Restore(const state::MachineState&);
    // A transient service wake does not terminate the guest. The callback runs
    // on the Run thread with the vCPU stopped, before resuming execution.
    bool WakeForService();
    void OnServiceWake(std::function<void(MicroVm&)> fn) { serviceWake_ = std::move(fn); }

    // If Run() stopped on an un-handled guest exception, these describe it.
    uint8_t  LastExceptionVector() const { return lastExcVector_; }
    uint64_t LastExceptionRip()    const { return lastExcRip_; }

    // Route a guest I/O port to a device model (call before Run).
    void MapIoPort(uint16_t port, IoDevice* dev) { ioMap_[port] = dev; }

    // A richer trap than IoDevice: when the guest touches `port`, the handler is
    // invoked with the whole VM so it can read/write registers and guest memory.
    // This is how the syscall "doorbell" of the NT personality is serviced.
    using PortTrap = std::function<void(MicroVm&)>;
    void MapIoTrap(uint16_t port, PortTrap fn) { ioTraps_[port] = std::move(fn); }
    struct IoAccess { uint16_t port; uint8_t bytes; bool write, string, repeat; uint32_t value; uint64_t rip; };
    using DetailedPortTrap = std::function<void(MicroVm&, const IoAccess&)>;
    void MapIoTrapDetailed(uint16_t port, DetailedPortTrap fn) { detailedIoTraps_[port] = std::move(fn); }
    void StrictIo(bool enabled) { strictIo_ = enabled; }
    bool SetPortInput(uint32_t value,uint8_t bytes) {return bytes==4&&SetReg(WHvX64RegisterRax,value);}

    // Execute until the guest HLTs (returns true) or an error/budget stop
    // (returns false; see Error()).
    bool Run(uint64_t maxExits = 1'000'000);

    const std::wstring& Error() const { return error_; }
    uint64_t ExitCount() const { return exits_; }
    // All successful WHvRunVirtualProcessor returns, including terminal exits.
    uint64_t TotalExitCount() const { return totalExits_; }

    // --- snapshot support (used by SnapshotManager) ---
    // Host-side view of guest RAM (GPA 0 .. RamSize()); read/write directly.
    size_t      RamSize() const { return ramBytes_; }
    void*       RamData()       { return ram_; }
    const void* RamData() const { return ram_; }
    bool GetRegisters(const WHV_REGISTER_NAME* names, uint32_t count, WHV_REGISTER_VALUE* out);
    bool SetRegisters(const WHV_REGISTER_NAME* names, uint32_t count, const WHV_REGISTER_VALUE* in);

private:
    friend class SmpMachine;
    uint32_t registerIndex_=0,cpuCount_=1,createdCount_=0;
    bool SetReg(WHV_REGISTER_NAME name, uint64_t value);
    bool Fail(const char* step, HRESULT hr);
    bool HandleCpuid(const WHV_RUN_VP_EXIT_CONTEXT& exit,uint32_t index=0);
    bool CreateMemory();
    void ReleaseMemory();
    bool MapMemory(uint64_t gpa,uint64_t bytes,uint8_t access);
    bool QueueInterrupt(const WHV_REGISTER_VALUE&);
    bool DeliverQueuedInterrupt();

    WHV_PARTITION_HANDLE part_ = nullptr;
    void*    ram_      = nullptr;
    size_t   ramBytes_ = 0;
    HANDLE memoryProcess_=nullptr,memoryMapping_=nullptr,memoryStop_=nullptr;
    void* memoryView_=nullptr;
    void* memorySource_=nullptr;
    WHV_REGISTER_VALUE queuedInterrupt_{};
    uint64_t codeGpa_  = 0x1000;
    std::map<uint16_t, IoDevice*> ioMap_;
    std::map<uint16_t, PortTrap>  ioTraps_;
    std::map<uint16_t, DetailedPortTrap> detailedIoTraps_;
    bool strictIo_ = false;
    SyscallHandler onSyscall_;
    std::atomic<bool> pauseRequested_{false};
    bool paused_=false;
    bool     stopRequested_ = false;
    bool     syscallRedirected_ = false, executionFatal_ = false;
    std::atomic<bool> cancelRequested_{false};
    std::function<void(MicroVm&)> serviceWake_;
    bool     spoofCpuid_ = false;
    uint32_t cpuidCpus_ = 8;
    std::string cpuidVendor_, cpuidBrand_;
    std::string cpuIdentity_;
    uint8_t  lastExcVector_ = 0;
    uint64_t lastExcRip_ = 0;
    std::wstring error_;
    uint64_t exits_ = 0;
    uint64_t totalExits_ = 0;
    std::vector<uint8_t> memoryAccess_;
    bool memoryFatal_ = false, memoryAccessFault_ = false;
    uint64_t memoryAccessGpa_ = 0;
    uint8_t memoryAccessType_ = 0;
};

} // namespace microvm
} // namespace av
