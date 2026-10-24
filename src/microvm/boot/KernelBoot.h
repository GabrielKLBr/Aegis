#pragma once
#include "KernelImage.h"
#include "../analysis/Analysis.h"
#include "ResourceSession.h"
#include "../hardware/Profile.h"
#include "../compatibility/Binding.h"
#include "../hardware/DriverBackend.h"
#include "../gui/Backend.h"
#include "../net/Network.h"
#include "SessionSnapshot.h"
#include "BootChannel.h"
#include "guardian/Cpu.h"
#include "guardian/Environment.h"
#include "guardian/ExternalGui.h"
#include "guardian/Memory.h"
#include "guardian/Threads.h"
#include "guardian/Processes.h"
#include "IoBackend.h"
#include "PersistentBlockStore.h"
#include <memory>
#if defined(__linux__)
#include "../../linux/KvmMachine.h"
#else
#include "../vmm/MicroVmMonitor.h"
#endif
#include <chrono>
#include <vector>
namespace av::microvm::boot {
inline constexpr uint64_t BootGpa = 0x100000, MapGpa = 0x101000, DevicesGpa = 0x102000;
inline constexpr uint64_t ChannelGpa = 0x103000, ReturnGpa = 0x104000, StackGpa = 0x400000, StackBytes = 0x10000;
struct BootResult {
    std::string ntCompatibility;
    bool resourceMode=false,resourceReportValid=false;
    guardian::resource::Report resource{};
    bool driverMode=false,driverReportValid=false;
    guardian::driver::Report driver{};
    bool graphicsMode=false,graphicsReportValid=false;
    guardian::graphics::Report graphics{};
    uint64_t graphicsFrames=0,graphicsInputs=0,graphicsHash=0;
    bool networkMode=false,networkReportValid=false;
    guardian::net::Report network{};
    uint64_t networkSent=0,networkReceived=0,networkDenied=0;
    bool paused=false;
    bool success = false, timedOut = false, privateBytesValid = false, memoryFault = false;
    uint64_t bootId = 0, ramBytes = 0, exits = 0, hostPrivateBytes = 0, rip = 0, faultGpa = 0, exceptionExitBitmap = 0;
    uint32_t stopStatus = UINT32_MAX, exception = 0, cpl = UINT32_MAX;
    double wallMs = 0;
    std::string diagnostic, console, error;
    bool cpuMode = false, cpuReportValid = false;
    guardian::cpu::Report cpu{};
    uint64_t timerQueued = 0, timerAcknowledged = 0;
    bool memoryMode = false, memoryReportValid = false;
    guardian::memory::Report memory{};
    uint64_t physicalRequests = 0;
    bool threadMode = false, threadReportValid = false;
    guardian::threads::Report threads{};
    bool processMode=false,processReportValid=false;
    guardian::processes::Report processes{};
    bool ioMode=false,ioReportValid=false;
    guardian::io::Report io{};
    uint64_t ioHostRequests=0,ioHostConsole=0,ioHostChannel=0,ioHostBlock=0;
    std::string ioConsole;
    bool storageMode=false,storageReportValid=false;
    guardian::storage::Report storage{};
    uint64_t storageRecords=0,storageRecoveredTail=0;
};
struct CpuOptions { bool enabled = false, driveTimer = true, memory = false, threads = false, processes=false,io=false,storage=false,network=false,graphics=false,driver=false,resources=false; uint32_t vcpus=1; };
bool WhpAvailable();
void WriteBootReport(const std::filesystem::path& path, const std::vector<BootResult>& rows);
class KernelBootSession {
public:
    void Prepare(const KernelImage& image, uint64_t ramBytes, uint64_t bootId, CpuOptions cpu = {},const StorageOptions& storage={},const net::Options& networking={},const gui::Options& graphics={},const hardware::Profile* hardware=nullptr,const hardware::DriverOptions& driver={},const storage::SparseOptions* scalable=nullptr,const guardian::environment::Info* environment=nullptr,const guardian::telemetry::Policy* analysisPolicy=nullptr,uint32_t analysisScenario=0,std::span<const uint8_t> externalGui={},const guardian::nt::Info* compatibility=nullptr);
    BootResult Run(std::chrono::milliseconds timeout = std::chrono::seconds(3), bool suspendAtDeadline = false);
    void PauseAtGraphicsRequest(uint64_t n){pauseAtGraphics_=n;}
    void PauseAtDriverWrite(uint32_t n){pauseAtDriverWrite_=n;}
    void PauseAtNetworkRequest(uint64_t n){pauseAtNetwork_=n;}
    void PauseAtIoRequest(uint64_t request){pauseAtIo_=request;}
    void PauseAtIoConsole(std::string marker){pauseAtIoConsole_=std::move(marker);}
    bool Pause(){return resourceSession_?resourceSession_->Pause():vm_.RequestPause();}
    uint64_t ElapsedNs() const { return resourceSession_?resourceSession_->ElapsedNs():elapsedNs_; }
    bool Paused() const {return resourceSession_?resourceSession_->Paused():paused_;}
    SessionSnapshot Capture();
    bool QueryResourceSpace(guardian::scalable::Space& out,uint64_t* charge=nullptr){return resourceSession_&&resourceSession_->QuerySpace(out,charge);}
    std::vector<uint8_t> DiskSnapshot(){return resourceSession_?resourceSession_->DiskSnapshot():storageBackend_?storageBackend_->Snapshot():std::vector<uint8_t>{};}
    void Restore(const SessionSnapshot&);
    // Stopped-VM access for the owned-program integration tests only.
    MicroVm& Machine() { return resourceSession_?resourceSession_->Machine():vm_; }
    void FailIoDevice(uint32_t device);
    const analysis::Trace* Analysis() const {return telemetryEnabled_?&telemetry_:nullptr;}
private:
    void Control(const MicroVm::IoAccess& access);
    bool ValidIo(const MicroVm::IoAccess& access);
    void CpuControl(const MicroVm::IoAccess& access);
    void MemoryControl(const MicroVm::IoAccess& access);
    void ThreadControl(const MicroVm::IoAccess& access);
    void ProcessControl(const MicroVm::IoAccess& access);
    void IoControl(const MicroVm::IoAccess& access);
    void GraphicsControl(const MicroVm::IoAccess&);
    void NetworkControl(const MicroVm::IoAccess&);
    void StorageControl(const MicroVm::IoAccess& access);
    void ClockService();
    void TelemetryControl(const MicroVm::IoAccess&);
    bool telemetryEnabled_=false,analysisKernel_=false;
    bool externalGuiKernel_=false;
    guardian::nt::Info ntInfo_{};
    bool ntEnabled_=false;
    guardian::telemetry::Info telemetryInfo_{};
    analysis::Trace telemetry_;
    bool PendingTimer(bool set);
    std::unique_ptr<ResourceSession> resourceSession_;
    MicroVm vm_;
    guardian::hardware::Info hardwareInfo_{};
    std::unique_ptr<hardware::DriverBackend> driverBackend_;
    uint32_t pauseAtDriverWrite_=0;
    BootChannel channel_{0};
    uint64_t ramBytes_ = 0, bootId_ = 0, exceptionExitBitmap_ = 0;
    std::unique_ptr<gui::Backend> graphicsBackend_;
    guardian::graphics::Report graphicsReport_{};
    bool graphicsReportValid_=false;
    uint64_t pauseAtGraphics_=0;
    bool networkReportValid_=false;
    uint64_t pauseAtNetwork_=0;
    guardian::net::Report networkReport_{};
    std::unique_ptr<net::Backend> networkBackend_;
    bool paused_=false;
    uint64_t pauseAtIo_=0;
    std::string pauseAtIoConsole_;
    uint64_t elapsedNs_=0,imageHash_=0,utcEpoch100ns_=0;
    guardian::environment::Info environmentInfo_{};
    bool environmentEnabled_=false;
    bool clockApiContract_=false;
    state::MachineState pausedMachine_;
    bool attempted_ = false, prepared_ = false, ran_ = false;
    std::string console_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point runStart_;
    CpuOptions cpuOptions_;
    std::atomic<bool> timerArmed_{false};
    bool timerInFlight_ = false, userCodeSealed_ = false, cpuReportValid_ = false;
    uint64_t timerQueued_ = 0, timerAcknowledged_ = 0, nextTimerNs_ = 0, clockSequence_ = 0;
    guardian::cpu::Report cpuReport_{};
    std::vector<guardian::abi::MemoryRegion> regions_;
    bool memoryReportValid_ = false;
    uint64_t physicalRequests_ = 0;
    guardian::memory::Report memoryReport_{};
    bool threadReportValid_ = false;
    guardian::threads::Report threadReport_{};
    bool processReportValid_=false;
    guardian::processes::Report processReport_{};
    std::unique_ptr<guardian::io::Backend> ioBackend_;
    guardian::io::Report ioReport_{};
    bool ioReportValid_=false;
    std::unique_ptr<PersistentBlockStore> storageBackend_;
    guardian::storage::Report storageReport_{};
    bool storageReportValid_=false;
    guardian::storage::QuotaInfo storageQuota_{};
    bool storageQuotaContract_=false;
};
}
