#include "ResourceSession.h"
#include "../../../guest/ScalableVolume.h"
#include "KernelBoot.h"
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
namespace av::microvm::boot {
using state::Require;
namespace gr = guardian::resource;
namespace gs = guardian::scalable;
bool ResourceSession::QuerySpace(gs::Space &space, uint64_t *charge) {
    auto one = std::make_unique<gs::Metadata>(), two = std::make_unique<gs::Metadata>();
    auto scratch = std::make_unique<uint8_t[]>(gs::ChunkBytes);
    gs::Volume volume;
    if (!volume.Configure(disk_.get(), storage::SparseDisk::Block, storage::SparseDisk::Space, one.get(),
                          two.get(), scratch.get(), boot_.capacity, boot_.quota) ||
        volume.Mount(false, false) != gs::Status::Ok)
        return false;
    space = volume.QuerySpace();
    if (charge)
        *charge = disk_->SnapshotCharge();
    return true;
}
void ResourceSession::Prepare(const KernelImage &image, uint64_t ram, uint64_t id, uint32_t count,
                              const storage::SparseOptions &options, uint64_t quota) {
    Require(!prepared_ && id && count >= 1 && count <= 4 && ram >= 16ull << 20 && ram <= 1ull << 30 &&
                !(ram & 4095),
            "Resource boot bounds rejected");
    Require(image.SupportsResources(), "Imported kernel lacks the SMP/scalable resource contract");
    Require(quota >= gs::DataStart && quota <= options.capacity && quota % gs::ChunkBytes == 0,
            "Scalable volume quota rejected");
    imageHash_ = state::Hash(image.FileBytes());
    boot_.bootId = id;
    boot_.ramBytes = ram;
    boot_.capacity = options.capacity;
    boot_.quota = quota;
    boot_.vcpus = count;
    disk_ = std::make_unique<storage::SparseDisk>(options);
    machine_.Create(ram, count, "Guardian resource CPU");
    auto &vm = machine_.Primary();
    auto *memory = static_cast<uint8_t *>(vm.RamData());
    image.Map({memory, vm.RamSize()});
    std::memcpy(memory + gr::BootGpa, &boot_, sizeof(boot_));
    Require(vm.SetMemoryAccess(0, ram, 3) && vm.SetMemoryAccess(image.Base(), image.Size(), 1),
            "Resource initial permissions failed");
    for (const auto &section : image.Sections())
        Require(vm.SetMemoryAccess(image.Base() + section.rva, section.mappedBytes, section.access),
                "Resource image permission failed");
    machine_.Entry(image.Entry(), gr::BootGpa);
    Require(vm.SetMemoryAccess(gr::BootGpa, 4096, 1), "Resource boot permission failed");
    vm.StrictIo(true);
    vm.MapIoTrapDetailed(gr::Port, [this](MicroVm &, const MicroVm::IoAccess &io) { Control(io); });
    prepared_ = true;
}
void ResourceSession::Control(const MicroVm::IoAccess &io) {
    auto &vm = machine_.Primary();
    auto *memory = static_cast<uint8_t *>(vm.RamData());
    auto reject = [&] { vm.FailExecution(L"Resource block request rejected"); };
    if (!io.write || io.bytes != 4 || io.string || io.repeat || io.value != 1 ||
        !vm.CanAccessMemory(gr::RequestGpa, sizeof(gr::Request), 3)) {
        reject();
        return;
    }
    gr::Request r{};
    std::memcpy(&r, memory + gr::RequestGpa, sizeof(r));
    if (sequence_ == UINT64_MAX || r.sequence != sequence_ + 1 || r.reserved || r.buffer != gr::DataGpa ||
        r.bytes != gs::ChunkBytes) {
        reject();
        return;
    }
    ++sequence_;
    if (r.operation == gr::ReportOperation) {
        if (reported_ || !vm.CanAccessMemory(gr::ReportGpa, sizeof(report_), 1)) {
            reject();
            return;
        }
        std::memcpy(&report_, memory + gr::ReportGpa, sizeof(report_));
        const auto mask = (1u << boot_.vcpus) - 1;
        if (report_.bootId != boot_.bootId || report_.vcpus != boot_.vcpus ||
            report_.capacity != boot_.capacity || report_.readyMask != mask || report_.topologyMask != mask ||
            report_.doneMask != mask || report_.fpuMask != mask ||
            report_.counter != uint64_t(boot_.vcpus) * gr::Iterations) {
            reject();
            return;
        }
        reported_ = true;
        vm.RequestStop();
        return;
    }
    if (r.operation == gr::SmpCheckpointOperation) {
        r.status = uint32_t(gs::Status::Ok);
    } else if (r.operation == gr::SpaceOperation) {
        r.available = disk_->Available();
        r.status = uint32_t(gs::Status::Ok);
    } else {
        if (r.operation > uint32_t(gs::BlockOp::AllocationMap) ||
            !vm.CanAccessMemory(gr::DataGpa, gs::ChunkBytes, 3)) {
            reject();
            return;
        }
        r.status = uint32_t(disk_->Io(gs::BlockOp(r.operation), r.offset, memory + gr::DataGpa));
    }
    std::memcpy(memory + gr::RequestGpa, &r, sizeof(r));
    if (pauseAt_ && sequence_ == pauseAt_) {
        pauseAt_ = 0;
        machine_.Pause();
    }
}
BootResult ResourceSession::Run(std::chrono::milliseconds timeout, bool suspendAtDeadline) {
    Require(prepared_ && (!ran_ || paused_) && timeout.count() >= 1 && timeout <= std::chrono::seconds(30),
            "Resource run state/deadline rejected");
    ran_ = true;
    paused_ = false;
    const auto started = std::chrono::steady_clock::now();
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    std::atomic<bool> timedOut = false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, timeout, [&] { return done; })) {
            timedOut = true;
            // Repeat until Run acknowledges the deadline, including a request
            // racing processor startup or entry into the hypervisor.
            do {
                if (suspendAtDeadline) machine_.Pause(); else machine_.Cancel();
            } while (!changed.wait_for(lock, std::chrono::milliseconds(5), [&] { return done; }));
        }
    });
    bool ok = false;
    try {
        ok = machine_.Run(1000000);
    } catch (...) {
        {
            std::lock_guard lock(mutex);
            done = true;
        }
        changed.notify_one();
        watchdog.join();
        throw;
    }
    {
        std::lock_guard lock(mutex);
        done = true;
    }
    changed.notify_one();
    watchdog.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    elapsedNs_ += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    auto &vm = machine_.Primary();
    paused_ = vm.Paused() && ok;
    BootResult result;
    result.paused = paused_;
    result.timedOut = timedOut && !suspendAtDeadline;
    result.bootId = boot_.bootId;
    result.ramBytes = boot_.ramBytes;
    result.exits = vm.TotalExitCount();
    result.wallMs = std::chrono::duration<double, std::milli>(elapsed).count();
    result.resourceMode = true;
    result.resourceReportValid = reported_;
    result.resource = report_;
    result.success = ok && !paused_ && reported_ && !report_.status && report_.checks == gr::RequiredChecks;
    result.stopStatus = reported_ ? report_.status : UINT32_MAX;
#ifdef __linux__
    result.error = std::string(vm.Error().begin(), vm.Error().end());
#else
    const auto &error = vm.Error();
    if (!error.empty()) {
        const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, error.data(),
                                             static_cast<int>(error.size()), nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) {
            result.error = "WHP error (text conversion failed)";
        } else {
            result.error.resize(static_cast<size_t>(bytes));
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, error.data(),
                                    static_cast<int>(error.size()), result.error.data(), bytes,
                                    nullptr, nullptr) != bytes)
                result.error = "WHP error (text conversion failed)";
        }
    }
#endif
    if (!result.success && !paused_ && result.error.empty())
        result.error = "Resource kernel checks failed";
    return result;
}
SessionSnapshot ResourceSession::Capture() {
    Require(prepared_ && (!ran_ || paused_), "Resource capture requires stopped processors");
    SessionSnapshot s;
    Require(machine_.Capture(s.machine), "Resource machine capture failed");
    state::Writer w;
    w.Pod(boot_.bootId);
    w.Pod(boot_.ramBytes);
    w.Pod(imageHash_);
    w.Pod(uint64_t(gr::Magic));
    w.Pod(uint32_t(gr::Version));
    w.Pod(uint32_t(0));
    w.Pod(elapsedNs_);
    w.Pod(boot_);
    w.Pod(sequence_);
    w.Pod(uint32_t(reported_));
    w.Pod(report_);
    s.metadata = std::move(w.data);
    s.disk = disk_->Snapshot();
    return s;
}
void ResourceSession::Restore(const SessionSnapshot &s) {
    Require(prepared_ && !ran_, "Resource restore requires a fresh candidate");
    state::Reader r(s.metadata);
    Require(r.Pod<uint64_t>() == boot_.bootId && r.Pod<uint64_t>() == boot_.ramBytes &&
                r.Pod<uint64_t>() == imageHash_ && r.Pod<uint64_t>() == gr::Magic &&
                r.Pod<uint32_t>() == gr::Version && r.Pod<uint32_t>() == 0,
            "Resource checkpoint identity rejected");
    const auto elapsed = r.Pod<uint64_t>();
    const auto boot = r.Pod<gr::Boot>();
    const auto sequence = r.Pod<uint64_t>();
    const auto reported = r.Pod<uint32_t>();
    const auto report = r.Pod<gr::Report>();
    r.End();
    Require(std::memcmp(&boot, &boot_, sizeof(boot)) == 0 && elapsed < guardian::session::MaxElapsedNs && reported <= 1,
            "Resource checkpoint configuration rejected");
    Require(s.machine.ram.size() >= gr::BootGpa + sizeof(boot_) &&
                std::memcmp(s.machine.ram.data() + gr::BootGpa, &boot_, sizeof(boot_)) == 0 &&
                s.machine.access[gr::BootGpa / 4096] == 1,
            "Resource checkpoint boot page rejected");
    Require(disk_->Snapshot() == s.disk, "Resource checkpoint disk generation rejected");
    Require(machine_.Restore(s.machine), "Resource machine restore failed");
    sequence_ = sequence;
    elapsedNs_ = elapsed;
    reported_ = reported != 0;
    report_ = report;
    paused_ = true;
}
} // namespace av::microvm::boot
