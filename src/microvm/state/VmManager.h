#pragma once
#include "../boot/KernelBoot.h"
#include "Files.h"
#include "../environment/Profile.h"
#include "../hardware/Profile.h"
#include <optional>
#include <filesystem>
#include <memory>
namespace av::microvm::state {
enum class VmState : uint32_t { Stopped, Starting, Running, Paused, Fault };
struct VmConfig {
    std::string id, contract = "storage";
    uint32_t ramMiB = 32, timeoutMs = 10000;
    uint32_t requestedRamMiB = 32, requestedVcpus = 1;
    uint32_t hostReserveMiB = 0;
    bool maximumResources = false;
    uint64_t volumeQuotaBytes = guardian::storage::VolumeCapacity;
    uint64_t overlayLimitBytes = 512+4096ull*544;
    uint64_t storageLimitBytes = 4ull<<30;
    std::filesystem::path diskFolder{};
    std::string diskOwner{};
    uint64_t diskCapacityBytes=2ull<<30,diskGrowthBytes=64ull<<20;
    bool persistent = true;
    uint64_t imageHash = 0;
    uint32_t durationMs = 0, checkpointMs = 1000;
    std::vector<gui::TimedInput> script;
    net::Policy network;
    std::optional<hardware::Profile> hardware;
    std::optional<guardian::environment::Info> environment;
    guardian::telemetry::Policy analysisPolicy{};
    uint32_t analysisScenario=0;
};
struct VmStatus {
    bool recoveredDisk = false, resident = false;
    VmState state = VmState::Stopped;
    uint64_t stamp = 0, boot = 0, virtualNs = 0, pid = 0;
    std::string generation, active, error;
};
class VmManager {
  public:
    explicit VmManager(std::filesystem::path root, bool worker = false);
    void Create(const VmConfig &, const std::filesystem::path &image, const std::filesystem::path &base = {});
    void Start(const std::string &, const std::filesystem::path &executable, bool paused = false);
    void Command(const std::string &, const std::string &operation, const std::string &argument = {},
                 const std::filesystem::path &executable = {});
    void Clone(const std::string &, const std::string &target);
    void ConfigureScalableStorage(const std::string&,uint64_t quota,uint64_t growth,uint64_t total=0);
    void ConfigureDiskFolder(const std::string&,const std::filesystem::path&);
    void ConfigureNetwork(const std::string&,const net::Policy&);
    void ConfigureHardware(const std::string&, const hardware::Profile&);
    hardware::Profile HardwareProfile(const std::string&);
    std::string Environment(const std::string&);
    void ConfigureAnalysis(const std::string&,const std::string& domain,const std::string& decision,uint32_t eventLimit=0,uint32_t scenario=UINT32_MAX);
    std::string AnalysisPolicy(const std::string&);
    std::string AnalysisReport(const std::string&,bool html=false);
    std::string Hardware(const std::string&);
    void ImportDriver(const std::string&, const std::string& name, const std::filesystem::path&,
                      const std::string& nt, const std::string& device);
    std::string Drivers(const std::string&);
    void InstallDriver(const std::string&,const std::string&,const std::filesystem::path& api);
    void ConfigureSession(const std::string&, uint32_t durationMs, uint32_t checkpointMs, const std::filesystem::path& script = {});
    void Configure(const std::string &, uint32_t ramMiB, uint32_t timeoutMs);
    void ConfigureResources(const std::string&,uint32_t ramMiB,uint32_t vcpus,bool maximum,uint32_t hostReserveMiB=UINT32_MAX);
    void ConfigureStorage(const std::string&,uint64_t volumeQuotaBytes,uint64_t overlayLimitBytes,uint64_t storageLimitBytes=0);
    std::string Resources(const std::string&);
    void Delete(const std::string &);
    std::string Status(const std::string &);
    std::string List();
    int Worker(const std::string &, const std::string &mode);
    static void Name(const std::string &);

  private:
    std::filesystem::path Path(const std::string &) const;
    VmConfig Config(const std::string &) const;
    VmStatus ReadStatus(const std::string &, bool recover = true) const;
    void Publish(const std::string &, const VmStatus &) const;
    void Spawn(const std::string &, const std::string &, const std::filesystem::path &);
    void Request(const std::string &, const std::string &, const std::string &);
    void ValidateDirectory(const std::string &) const;
    std::filesystem::path root_;
    std::unique_ptr<Lease> catalog_;
};
} // namespace av::microvm::state
