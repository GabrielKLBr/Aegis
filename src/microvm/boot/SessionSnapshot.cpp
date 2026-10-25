#include "KernelBoot.h"
#include "guardian/SystemClock.h"
namespace av::microvm::boot {
namespace {
uint32_t Options(CpuOptions c) {
    return uint32_t(c.enabled) | (uint32_t(c.driveTimer) << 1) | (uint32_t(c.memory) << 2) |
           (uint32_t(c.threads) << 3) | (uint32_t(c.processes) << 4) | (uint32_t(c.io) << 5) |
           (uint32_t(c.storage) << 6) | (uint32_t(c.network) << 7) | (uint32_t(c.graphics) << 8) | (uint32_t(c.driver)<<9);
}
} // namespace
SessionSnapshot KernelBootSession::Capture() {
    state::Require(!desktopBackend_,"Desktop checkpoints are not implemented");
    state::Require(!externalGuiKernel_&&!externalPeKernel_,"External PE snapshots are not implemented");
    if(resourceSession_)return resourceSession_->Capture();
    state::Require(prepared_ && (!ran_ || paused_), "Snapshot requires a prepared or paused guest");
    SessionSnapshot out;
    if (paused_)
        out.machine = pausedMachine_;
    else
        state::Require(vm_.Capture(out.machine), "Machine capture failed");
    state::Writer w;
    w.Pod(bootId_);
    w.Pod(ramBytes_);
    w.Pod(imageHash_);
    w.Pod(exceptionExitBitmap_);
    w.Pod(Options(cpuOptions_));
    const uint32_t flags = uint32_t(timerArmed_.load()) | (uint32_t(timerInFlight_) << 1) |
                           (uint32_t(userCodeSealed_) << 2) | (uint32_t(cpuReportValid_) << 3) |
                           (uint32_t(memoryReportValid_) << 4) | (uint32_t(threadReportValid_) << 5) |
                           (uint32_t(processReportValid_) << 6) | (uint32_t(ioReportValid_) << 7) |
                           (uint32_t(storageReportValid_) << 8);
    w.Pod(flags);
    w.Pod(elapsedNs_);
    w.Pod(timerQueued_);
    w.Pod(timerAcknowledged_);
    w.Pod(nextTimerNs_);
    w.Pod(clockSequence_);
    w.Pod(physicalRequests_);
    w.Pod(cpuReport_);
    w.Pod(memoryReport_);
    w.Pod(threadReport_);
    w.Pod(processReport_);
    w.Pod(ioReport_);
    w.Pod(storageReport_);
    channel_.Save(w);
    w.Text(console_);
    if (ioBackend_)
        ioBackend_->Save(w);
    if (networkBackend_){w.Pod(uint32_t(networkReportValid_));w.Pod(networkReport_);networkBackend_->Save(w);}
    if(graphicsBackend_){w.Pod(uint32_t(graphicsReportValid_));w.Pod(graphicsReport_);graphicsBackend_->Save(w);}
    if(driverBackend_)driverBackend_->Save(w);
    if(telemetryEnabled_)telemetry_.Save(w);
    if (storageBackend_)
        out.disk = storageBackend_->Snapshot();
    out.metadata = std::move(w.data);
    return out;
}
void KernelBootSession::Restore(const SessionSnapshot &saved) {
    state::Require(!desktopBackend_,"Desktop checkpoints are not implemented");
    state::Require(!externalGuiKernel_&&!externalPeKernel_,"External PE snapshots are not implemented");
    if(resourceSession_){resourceSession_->Restore(saved);return;}
    state::Require(prepared_ && !ran_, "Restore requires a fresh candidate session");
    if(saved.machine.ram.size()>=guardian::nt::InfoGpa+sizeof(ntInfo_)){
        guardian::nt::Info info{};std::memcpy(&info,saved.machine.ram.data()+guardian::nt::InfoGpa,sizeof(info));
        if(ntEnabled_)state::Require(saved.machine.access.size()>guardian::nt::InfoGpa/4096&&saved.machine.access[guardian::nt::InfoGpa/4096]==1&&std::memcmp(&info,&ntInfo_,sizeof(info))==0,"Snapshot NT profile/runtime/manifest mismatch");
        else state::Require(info.magic!=guardian::nt::Magic,"Snapshot requires its NT compatibility manifest");
    }else state::Require(!ntEnabled_,"Snapshot NT binding page missing");
    if(driverBackend_)driverBackend_->ValidateSnapshot(saved.machine);
    if(cpuOptions_.storage&&storageQuotaContract_){
        const auto gpa=guardian::storage::QuotaGpa;
        state::Require(saved.machine.ram.size()>=gpa+sizeof(storageQuota_) && std::memcmp(saved.machine.ram.data()+gpa,&storageQuota_,sizeof(storageQuota_))==0,"Snapshot volume quota mismatch");
        state::Require(saved.machine.access.size()>gpa/4096 && saved.machine.access[gpa/4096]==1,"Snapshot quota page permissions rejected");
    }
    if(hardwareInfo_.magic){
        const auto gpa=guardian::hardware::InfoGpa;
        state::Require(gpa<=saved.machine.ram.size()&&sizeof(hardwareInfo_)<=saved.machine.ram.size()-gpa,
            "Snapshot hardware inventory is missing");
        guardian::hardware::Info info{};
        std::memcpy(&info,saved.machine.ram.data()+gpa,sizeof(info));
        state::Require(guardian::hardware::Valid(info,ramBytes_)&&std::memcmp(&info,&hardwareInfo_,sizeof(info))==0,
            "Snapshot hardware profile mismatch");
        const auto page=gpa/guardian::abi::PageBytes;
        state::Require(page<saved.machine.access.size()&&saved.machine.access[size_t(page)]==1,
            "Snapshot hardware page permissions rejected");
    }else if(saved.machine.ram.size()>=guardian::hardware::InfoGpa+sizeof(guardian::hardware::Info)){
        guardian::hardware::Info info{};
        std::memcpy(&info,saved.machine.ram.data()+guardian::hardware::InfoGpa,sizeof(info));
        state::Require(info.magic!=guardian::hardware::Magic,"Snapshot requires its configured hardware profile");
    }
    if(saved.machine.ram.size()>=guardian::environment::InfoGpa+sizeof(environmentInfo_)){
        guardian::environment::Info info{};std::memcpy(&info,saved.machine.ram.data()+guardian::environment::InfoGpa,sizeof(info));
        if(environmentEnabled_) state::Require(std::memcmp(&info,&environmentInfo_,sizeof(info))==0 && saved.machine.access[guardian::environment::InfoGpa/4096]==1,"Snapshot synthetic environment policy mismatch");
        else state::Require(info.magic!=guardian::environment::Magic,"Snapshot requires its synthetic environment policy");
    }else state::Require(!environmentEnabled_,"Snapshot synthetic environment page missing");
    if(telemetryEnabled_){
        const auto at=guardian::telemetry::InfoGpa;
        state::Require(saved.machine.ram.size()>=at+sizeof(telemetryInfo_)&&saved.machine.access.size()>at/4096&&saved.machine.access[at/4096]==1&&std::memcmp(saved.machine.ram.data()+at,&telemetryInfo_,sizeof(telemetryInfo_))==0,"Snapshot analysis identity/scenario rejected");
    }
    state::Reader r(saved.metadata);
    auto boot = r.Pod<uint64_t>(), ram = r.Pod<uint64_t>(), image = r.Pod<uint64_t>(),
         bitmap = r.Pod<uint64_t>();
    auto options = r.Pod<uint32_t>(), flags = r.Pod<uint32_t>();
    state::Require(boot == bootId_ && ram == ramBytes_ && image == imageHash_ &&
                       bitmap == exceptionExitBitmap_ && options == Options(cpuOptions_) && flags <= 511,
                   "Session identity/configuration mismatch");
    elapsedNs_ = r.Pod<uint64_t>();
    timerQueued_ = r.Pod<uint64_t>();
    timerAcknowledged_ = r.Pod<uint64_t>();
    nextTimerNs_ = r.Pod<uint64_t>();
    clockSequence_ = r.Pod<uint64_t>();
    physicalRequests_ = r.Pod<uint64_t>();
    state::Require(elapsedNs_ < guardian::session::MaxElapsedNs && timerAcknowledged_ <= timerQueued_ &&
                       !(clockSequence_ & 1),
                   "Invalid virtual clock state");
    cpuReport_ = r.Pod<guardian::cpu::Report>();
    memoryReport_ = r.Pod<guardian::memory::Report>();
    threadReport_ = r.Pod<guardian::threads::Report>();
    processReport_ = r.Pod<guardian::processes::Report>();
    ioReport_ = r.Pod<guardian::io::Report>();
    storageReport_ = r.Pod<guardian::storage::Report>();
    channel_.Load(r, bootId_);
    console_ = r.Text();
    if (ioBackend_)
        ioBackend_->Load(r);
    if(networkBackend_){auto valid=r.Pod<uint32_t>();state::Require(valid<=1,"Network report flag rejected");networkReportValid_=valid!=0;networkReport_=r.Pod<guardian::net::Report>();networkBackend_->Load(r);}
    if(graphicsBackend_){const auto valid=r.Pod<uint32_t>();state::Require(valid<=1,"Graphics report flag rejected");graphicsReportValid_=valid!=0;graphicsReport_=r.Pod<guardian::graphics::Report>();graphicsBackend_->Load(r);state::Require(graphicsBackend_->LastNs()<=elapsedNs_,"Graphics checkpoint time exceeds the session clock");}
    if(driverBackend_)driverBackend_->Load(r);
    if(telemetryEnabled_)telemetry_.Load(r,bootId_,telemetry_.Configuration());
    r.End();
    state::Require(storageBackend_ ? storageBackend_->Matches(saved.disk) : saved.disk.empty(),
                   "Snapshot disk and memory are not the same generation");
    if (cpuOptions_.enabled) {
        state::Require(saved.machine.ram.size() >= guardian::cpu::ClockGpa + sizeof(guardian::cpu::Clock),
                       "Snapshot clock page missing");
        guardian::cpu::Clock clock{};
        std::memcpy(&clock, saved.machine.ram.data() + guardian::cpu::ClockGpa, sizeof(clock));
        // Both zero fields identify a kernel made before the time API extension.
        if (clockApiContract_ || clock.utcEpoch100ns || clock.performanceFrequency) {
            guardian::time::Sample sample{};
            state::Require(guardian::time::SampleAt(clock, bootId_, elapsedNs_, sample),
                           "Snapshot time origin/frequency rejected");
            utcEpoch100ns_ = clock.utcEpoch100ns;
        }
    }
    state::Require(vm_.Restore(saved.machine), "Machine state restore failed");
    pausedMachine_ = saved.machine;
    paused_ = true;
    timerArmed_ = (flags & 1) != 0;
    timerInFlight_ = (flags & 2) != 0;
    userCodeSealed_ = (flags & 4) != 0;
    cpuReportValid_ = (flags & 8) != 0;
    memoryReportValid_ = (flags & 16) != 0;
    threadReportValid_ = (flags & 32) != 0;
    processReportValid_ = (flags & 64) != 0;
    ioReportValid_ = (flags & 128) != 0;
    storageReportValid_ = (flags & 256) != 0;
}
} // namespace av::microvm::boot
