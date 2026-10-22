#include "VmManager.h"
#include "Resources.h"
#include "StorageBudget.h"
#include "SnapshotDisk.h"
#include "../storage/DiskLocation.h"
#include "../gui/Viewer.h"
#include "../hardware/Package.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace av::microvm::state {
namespace {
namespace fs = std::filesystem;
using namespace av::microvm::boot;
uint64_t Now() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}
std::string Quote(const std::string &in) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : in) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 32) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += char(c);
    }
    return out + '"';
}
fs::path Utf8Path(const std::string& text){std::u8string out;for(unsigned char c:text)out+=char8_t(c);return fs::path(out);}
std::string PathText(const fs::path& path){const auto text=path.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
std::string OpaqueName(const char* prefix,const std::string& token){
    return std::string(prefix)+std::to_string(Hash({reinterpret_cast<const uint8_t*>(token.data()),token.size()}));
}
const char *Label(VmState s) {
    switch (s) {
    case VmState::Stopped:
        return "stopped";
    case VmState::Starting:
        return "starting";
    case VmState::Running:
        return "running";
    case VmState::Paused:
        return "paused";
    case VmState::Fault:
        return "fault";
    }
    return "invalid";
}
void SaveConfig(const fs::path &p, const VmConfig &c) {
    StorageBudget budget(p.parent_path(),c.storageLimitBytes,c.diskFolder);
    Writer w;
    w.Pod(uint32_t(17));
    w.Text(c.id);
    w.Text(c.contract);
    w.Pod(c.ramMiB);
    w.Pod(c.timeoutMs);
    w.Pod(uint32_t(c.persistent));
    w.Pod(c.imageHash);
    w.Bytes(net::EncodePolicy(c.network));
    w.Pod(uint32_t(c.hardware.has_value()));
    if(c.hardware)w.Bytes(hardware::Encode(*c.hardware));
    w.Pod(c.requestedRamMiB);w.Pod(c.requestedVcpus);w.Pod(uint32_t(c.maximumResources));
    w.Pod(c.volumeQuotaBytes);w.Pod(c.overlayLimitBytes);w.Pod(c.storageLimitBytes);w.Pod(c.hostReserveMiB);
    w.Text(PathText(c.diskFolder));w.Text(c.diskOwner);w.Pod(c.diskCapacityBytes);w.Pod(c.diskGrowthBytes);
    w.Pod(c.durationMs);w.Pod(c.checkpointMs);gui::SaveScript(w,c.script);
    w.Pod(uint32_t(c.environment.has_value()));if(c.environment)w.Pod(*c.environment);
    w.Pod(c.analysisPolicy);w.Pod(c.analysisScenario);
    AtomicWrite(p, Seal(w.data));
}
VmConfig LoadConfig(const fs::path &p) {
    auto data = Open(ReadFile(p, 65536));
    Reader r(data);
    const auto version=r.Pod<uint32_t>();
    Require(version==9||version==10||version==12||version==13||version==14||version==15||version==16||version==17, "VM configuration version rejected");
    VmConfig c;
    c.id = r.Text(32);
    c.contract = r.Text(16);
    c.ramMiB = r.Pod<uint32_t>();
    c.timeoutMs = r.Pod<uint32_t>();
    auto policy = r.Pod<uint32_t>();c.persistent=policy!=0;
    c.imageHash = r.Pod<uint64_t>();
    if(version>=10)c.network=net::DecodePolicy(r.Bytes(4096));
    if(version>=12){const auto enabled=r.Pod<uint32_t>();Require(enabled<=1,"Hardware profile flag rejected");if(enabled)c.hardware=hardware::Decode(r.Bytes(2048));}
    c.requestedRamMiB=c.ramMiB;
    if(version>=13){c.requestedRamMiB=r.Pod<uint32_t>();c.requestedVcpus=r.Pod<uint32_t>();const auto maximum=r.Pod<uint32_t>();Require(maximum<=1,"Invalid maximum resource flag");c.maximumResources=maximum!=0;c.volumeQuotaBytes=r.Pod<uint64_t>();c.overlayLimitBytes=r.Pod<uint64_t>();c.storageLimitBytes=r.Pod<uint64_t>();c.hostReserveMiB=r.Pod<uint32_t>();}
    if(version>=14){auto text=r.Text(32768);c.diskFolder=Utf8Path(text);c.diskOwner=r.Text(63);c.diskCapacityBytes=r.Pod<uint64_t>();c.diskGrowthBytes=r.Pod<uint64_t>();}
    if(version>=15){c.durationMs=r.Pod<uint32_t>();c.checkpointMs=r.Pod<uint32_t>();c.script=gui::LoadScript(r);}
    if(version>=16){const auto enabled=r.Pod<uint32_t>();Require(enabled<=1,"Environment flag rejected");if(enabled){c.environment=r.Pod<guardian::environment::Info>();environment::Validate(*c.environment);}}
    if(version>=17){c.analysisPolicy=r.Pod<guardian::telemetry::Policy>();c.analysisScenario=r.Pod<uint32_t>();Require(guardian::telemetry::ValidPolicy(c.analysisPolicy)&&c.analysisScenario<=1,"Stored analysis policy rejected");}
    if(c.environment){Require(c.contract=="storage"&&c.hardware&&c.persistent==bool(c.environment->preserve),"Environment contract/persistence mismatch");const auto inventory=hardware::Inventory(*c.hardware,uint64_t(c.ramMiB)<<20,"storage");Require(inventory.ntBuild==c.environment->ntBuild,"Environment and hardware NT disagree");}
    Require((!c.durationMs||guardian::session::ValidDuration(c.durationMs,c.checkpointMs)) && (c.script.empty()||(c.durationMs&&c.contract=="graphics")),"Session configuration rejected");
    Require(c.contract!="resources"||!c.hardware,"Resource kernel does not implement hardware identity profiles");
    guardian::storage::QuotaInfo quota{};quota.allocationLimit=c.volumeQuotaBytes;
    if(c.contract=="resources"){
        Require(!c.diskFolder.empty()&&c.diskFolder.is_absolute()&&!c.diskOwner.empty(),"Independent disk configuration missing");
        Require(c.diskCapacityBytes>=1ull<<30&&c.diskCapacityBytes<=8ull<<30&&c.diskCapacityBytes%guardian::scalable::ChunkBytes==0&&c.diskGrowthBytes>=4ull<<20&&c.diskGrowthBytes<=256ull<<20&&c.diskGrowthBytes%guardian::scalable::ChunkBytes==0,"Scalable disk bounds rejected");
        Require(c.volumeQuotaBytes>=guardian::scalable::DataStart&&c.volumeQuotaBytes<=c.diskCapacityBytes&&c.volumeQuotaBytes%guardian::scalable::ChunkBytes==0,"Scalable volume quota rejected");
    }else Require(guardian::storage::ValidQuota(quota)&&c.overlayLimitBytes>=512&&c.overlayLimitBytes<=512+4096ull*544&&c.diskFolder.empty()&&c.diskOwner.empty(),"Storage quota configuration rejected");
    Require(c.storageLimitBytes>=(1ull<<20)&&c.storageLimitBytes<=(64ull<<30),"VM aggregate storage quota rejected");
    Require(c.hostReserveMiB==0||c.hostReserveMiB>=512,"Host reserve configuration rejected");
    Require((c.contract=="resources"?(c.requestedVcpus>=1&&c.requestedVcpus<=4):c.requestedVcpus==1) && (c.maximumResources||c.requestedRamMiB==c.ramMiB),"Requested/provisioned resources disagree");
    r.End();
    Require(policy <= 1 && c.ramMiB >= 16 && c.ramMiB <= resources::MaxRamMiB && c.timeoutMs >= 100 && c.timeoutMs <= 30000,
            "VM resource configuration rejected");
    c.persistent = policy != 0;
    return c;
}
CpuOptions Options(const std::string &mode) {
    CpuOptions c{};
    if(mode=="resources"){c.resources=true;return c;}
    if(mode=="hardware"){c.driver=true;return c;}
    if (mode == "basic")
        return c;
    c.enabled = true;
    if(mode=="graphics"){c.graphics=true;return c;}
    if(mode=="network"){c.network=true;return c;}
    if (mode == "cpu")
        return c;
    if (mode == "memory")
        c.memory = true;
    else if (mode == "threads")
        c.threads = true;
    else if (mode == "processes")
        c.processes = true;
    else if (mode == "io") {
        c.processes = true;
        c.io = true;
    } else if (mode == "storage") {
        c.processes = true;
        c.io = true;
        c.storage = true;
    } else
        throw std::runtime_error("Unknown supported image contract");
    return c;
}
void StatusFile(const fs::path &p, const VmStatus &s) {
    Writer w;
    w.Pod(uint32_t(9));
    w.Pod(uint32_t(s.state));
    w.Pod(uint32_t(s.resident));
    w.Pod(uint32_t(s.recoveredDisk));
    w.Pod(s.stamp);
    w.Pod(s.boot);
    w.Pod(s.virtualNs);
    w.Pod(s.pid);
    w.Text(s.generation);
    w.Text(s.active);
    w.Text(s.error);
    AtomicWrite(p, Seal(w.data));
}
VmStatus LoadStatus(const fs::path &p) {
    auto bytes = Open(ReadFile(p, 8192));
    Reader r(bytes);
    Require(r.Pod<uint32_t>() == 9, "VM status version rejected");
    const auto value = r.Pod<uint32_t>();
    Require(value <= uint32_t(VmState::Fault), "VM status rejected");
    VmStatus s;
    s.state = VmState(value);
    auto resident = r.Pod<uint32_t>();
    Require(resident <= 1, "Invalid worker residency");
    s.resident = resident != 0;
    auto recovered = r.Pod<uint32_t>();
    Require(recovered <= 1, "Invalid disk recovery flag");
    s.recoveredDisk = recovered != 0;
    s.stamp = r.Pod<uint64_t>();
    s.boot = r.Pod<uint64_t>();
    s.virtualNs = r.Pod<uint64_t>();
    s.pid = r.Pod<uint64_t>();
    s.generation = r.Text(128);
    s.active = r.Text(128);
    s.error = r.Text(4096);
    r.End();
    return s;
}
bool ResourceSpace(const fs::path& directory,const VmConfig& c,const VmStatus& status,guardian::scalable::Space& space,uint64_t& charge){
    if((status.state!=VmState::Stopped&&status.state!=VmState::Paused)||status.recoveredDisk||!Ordinary(directory/"space.gvr"))return false;
    try{const auto data=Open(ReadFile(directory/"space.gvr",1024));Reader r(data);const auto image=r.Pod<uint64_t>();const auto generation=r.Text(128);const auto quota=r.Pod<uint64_t>(),growth=r.Pod<uint64_t>();const auto known=r.Pod<uint32_t>();space=r.Pod<guardian::scalable::Space>();charge=r.Pod<uint64_t>();r.End();
        return image==c.imageHash&&generation==status.generation&&quota==c.volumeQuotaBytes&&growth==c.diskGrowthBytes&&known==1&&space.capacity==c.diskCapacityBytes&&space.allocated>=guardian::scalable::DataStart&&space.allocated<=space.capacity&&space.free==space.capacity-space.allocated&&space.quota==quota&&charge>=128ull<<10&&charge<=c.diskGrowthBytes;
    }catch(const std::exception&){return false;}
}
uint64_t SnapshotLimit(uint32_t ram,uint64_t diskGrowth=0) {return (uint64_t(ram)<<20)+(diskGrowth?diskGrowth+(16ull<<20):(32ull<<20));}
void CheckSnapshotFile(const fs::path& p,uint32_t ram,uint64_t diskGrowth=0) {
    Require(Ordinary(p),"Checkpoint must be an ordinary file");const auto bytes=fs::file_size(p);
    Require(bytes>=24&&bytes<=SnapshotLimit(ram,diskGrowth),"Checkpoint exceeds configured RAM bound");
}
SessionSnapshot Snapshot(const fs::path &p,uint32_t ram,uint64_t diskGrowth=0) {
    CheckSnapshotFile(p,ram,diskGrowth);return SessionSnapshot::Decode(ReadFile(p,SnapshotLimit(ram,diskGrowth)));
}
uint64_t Elapsed(const SessionSnapshot &point) {
    Reader r(point.metadata);
    for (int i = 0; i < 4; ++i)
        r.Pod<uint64_t>();
    r.Pod<uint32_t>();
    r.Pod<uint32_t>();
    return r.Pod<uint64_t>();
}
std::vector<uint8_t> EmptyBase() { return std::vector<uint8_t>(guardian::io::BlockBytes); }
std::string Owner(const std::string &id) { return "GuardianVM phase 9\n" + id + "\n"; }
void SafeDirectory(const fs::path &p) {
    std::error_code ec;
    auto s = fs::symlink_status(p, ec);
    Require(!ec && fs::is_directory(s) && !fs::is_symlink(s), "VM directory type rejected");
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(p.c_str());
    Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
            "VM directory reparse point rejected");
#endif
}
void CleanupStagedDisk(const fs::path& stage){
    fs::path folder;std::string owner;
    if(Ordinary(stage/"config.gvc")){const auto c=LoadConfig(stage/"config.gvc");folder=c.diskFolder;owner=c.diskOwner;}
    else if(Ordinary(stage/".disk-create.gvt")){const auto data=Open(ReadFile(stage/".disk-create.gvt",65536));Reader r(data);owner=r.Text(63);folder=Utf8Path(r.Text(32768));r.End();}
    if(!folder.empty()&&fs::exists(folder)){
        if(fs::is_empty(folder)){storage::SafeDiskParent(folder);fs::remove(folder);}
        else storage::RemoveLocation(folder,owner);
    }
}
void RecoverDiskMove(const fs::path& p,const VmConfig& c){
    if(!Ordinary(p/"disk-move.gvt"))return;
    Lease worker(p/"worker.lock",true);Require(worker.Held(),"Disk migration recovery requires a stopped worker");
    const auto data=Open(ReadFile(p/"disk-move.gvt",131072));Reader r(data);const auto owner=r.Text(63);const auto old=Utf8Path(r.Text(32768)),next=Utf8Path(r.Text(32768));r.End();
    Require(c.contract=="resources"&&owner==c.diskOwner&&old!=next&&(c.diskFolder==old||c.diskFolder==next),"Disk migration identity rejected");
    storage::ValidateLocation(c.diskFolder,c.diskOwner);const auto unused=c.diskFolder==old?next:old;
    if(fs::exists(unused)){if(fs::is_empty(unused)){storage::SafeDiskParent(unused);fs::remove(unused);}else storage::RemoveLocation(unused,owner);}
    fs::remove(p/"disk-move.gvt");
}
void RemoveTree(const fs::path &p) {
    SafeDirectory(p);
    fs::remove_all(p);
}
void RemoveWork(const fs::path& path,const VmConfig& c){
    if(c.contract=="resources")storage::RemoveGeneration(path,c.diskOwner);else RemoveTree(path);
}
bool Resident(const fs::path &directory, bool expected) {
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        Lease lease(directory / "worker.lock", true);
        if (lease.Held())
            return false;
        if (expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}
#ifdef _WIN32
std::wstring QuotedPath(const std::wstring &value) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out += c;
        } else {
            out.append(slashes, L'\\');
            out += c;
        }
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
#endif
} // namespace
void VmManager::Name(const std::string &n) {
    Require(!n.empty() && n.size() <= 32 && n[0] >= 'a' && n[0] <= 'z',
            "VM/snapshot names must begin with a lowercase letter");
    for (char c : n)
        Require((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_',
                "VM/snapshot name rejected");
    Require(n != "con" && n != "prn" && n != "aux" && n != "nul" &&
                !(n.size() == 4 && (n.substr(0, 3) == "com" || n.substr(0, 3) == "lpt") && n[3] >= '1' &&
                  n[3] <= '9'),
            "Reserved device name rejected");
}
VmManager::VmManager(fs::path root, bool worker) : root_(fs::absolute(std::move(root)).lexically_normal()) {
    if (!fs::exists(root_)) {
        Require(!worker, "VM catalog not found");
        fs::create_directories(root_);
    }
    SafeDirectory(root_);
    if (!worker) {
        if (!fs::exists(root_ / ".owner"))
            for (const auto &entry : fs::directory_iterator(root_))
                Require(entry.path().filename() == ".catalog.lock", "Refusing to adopt a nonempty directory");
        catalog_ = std::make_unique<Lease>(root_ / ".catalog.lock");
    }
    const auto marker = root_ / ".owner";
    if (!fs::exists(marker)) {
        Require(!worker, "VM catalog marker missing");
        for (const auto &entry : fs::directory_iterator(root_))
            Require(entry.path().filename() == ".catalog.lock", "Refusing to adopt a nonempty directory");
        AtomicText(marker, "GuardianVM catalog 9\n");
    }
    const auto bytes = ReadFile(marker, 64);
    Require(std::string(bytes.begin(), bytes.end()) == "GuardianVM catalog 9\n",
            "VM catalog ownership rejected");
    if (!worker) {
        std::vector<fs::path> intents;
        for (const auto &e : fs::directory_iterator(root_)) {
            const auto name = e.path().filename().string();
            if ((name.starts_with(".create-") || name.starts_with(".delete-")) && name.ends_with(".intent"))
                intents.push_back(e.path());
        }
        for (const auto &intent : intents) {
            auto data = ReadFile(intent, 128);
            const std::string owner(data.begin(), data.end()), prefix = "GuardianVM phase 9\n";
            Require(owner.starts_with(prefix) && owner.ends_with("\n"), "Transaction ownership rejected");
            const auto id = owner.substr(prefix.size(), owner.size() - prefix.size() - 1);
            Name(id);
            const auto name = intent.filename().string();
            const auto stage = root_ / name.substr(0, name.size() - 7);
            if (fs::exists(stage)){CleanupStagedDisk(stage);RemoveTree(stage);}
            fs::remove(intent);
        }
        for (const auto &entry : fs::directory_iterator(root_)) {
            const auto name = entry.path().filename().string();
            if (name.starts_with(".create-") || name.starts_with(".delete-")) {
                SafeDirectory(entry.path());
                auto owner = ReadFile(entry.path() / ".owner", 128);
                Require(std::string(owner.begin(), owner.end()).starts_with("GuardianVM phase 9\n"),
                        "Interrupted transaction ownership rejected");
                CleanupStagedDisk(entry.path());RemoveTree(entry.path());
            } else if (name.starts_with(".partial-")) {
                Require(Ordinary(entry.path()), "Transaction file type rejected");
                fs::remove(entry.path());
            }
        }
    }
}
fs::path VmManager::Path(const std::string &id) const {
    Name(id);
    return root_ / id;
}
void VmManager::ValidateDirectory(const std::string &id) const {
    auto p = Path(id);
    SafeDirectory(p);
    auto bytes = ReadFile(p / ".owner", 128);
    Require(std::string(bytes.begin(), bytes.end()) == Owner(id), "VM ownership mismatch");
    SafeDirectory(p / "snapshots");
}
VmConfig VmManager::Config(const std::string &id) const {
    ValidateDirectory(id);
    auto c = LoadConfig(Path(id) / "config.gvc");
    Require(c.id == id, "VM identity mismatch");
    Options(c.contract);
    if(c.contract=="resources"){RecoverDiskMove(Path(id),c);storage::ValidateLocation(c.diskFolder,c.diskOwner);}
    net::Validate(c.network);
    const bool ownedEcho=c.contract=="storage"&&c.network.mode==guardian::net::Mode::Simulated&&KernelImage::Read(Path(id)/"image.gk").SupportsAnalysisFixture();
    Require(c.contract=="network"||c.network.mode==guardian::net::Mode::Offline||ownedEcho,"Network rules require a network-capable image");
    return c;
}
void VmManager::Publish(const std::string &id, const VmStatus &status) const {
    StatusFile(Path(id) / "status.gvt", status);
}
VmStatus VmManager::ReadStatus(const std::string &id, bool recover) const {
    const auto config = Config(id);
    auto p = Path(id);
    StorageBudget budget(p,config.storageLimitBytes,config.diskFolder);
    auto status = LoadStatus(p / "status.gvt");
    if (!recover)
        return status;
    Lease probe(p / "worker.lock", true);
    const bool failedDisk = !status.resident && status.state == VmState::Fault && config.persistent &&
                            !status.active.empty() && fs::exists((config.contract=="resources"?config.diskFolder:p) / status.active);
    if (probe.Held() &&
        ((status.resident && (status.state == VmState::Running || status.state == VmState::Paused ||
                              (status.state == VmState::Starting && Now() > status.stamp + 30))) ||
         failedDisk)) {
        try {
        if (config.persistent && (Options(config.contract).storage||config.contract=="resources") && !status.active.empty()) {
            Require(status.active.starts_with(".active-") &&
                        status.active.find_first_not_of(".active-0123456789") == std::string::npos,
                    "Recovered disk directory rejected");
            const auto work = (config.contract=="resources"?config.diskFolder:p) / status.active;
            if (fs::exists(work)) {
                resources::Reservation memory(16,1,config.hostReserveMiB,config.contract=="resources"?config.diskGrowthBytes:0);
                SafeDirectory(work);
                if(config.contract=="resources"){
                    storage::SparseOptions options;options.directory=work;options.owner=config.diskOwner;options.capacity=config.diskCapacityBytes;options.growthLimit=config.diskGrowthBytes;
                    storage::SparseDisk recovered(options);AtomicWrite(config.diskFolder/"disk.gvs",recovered.Snapshot());
                }else {PersistentBlockStore recovered({p / "base.gvd", work / "disk.gvo", false,config.overlayLimitBytes,config.volumeQuotaBytes,HostDiskReserve,p,config.storageLimitBytes});
                AtomicWrite(p / "disk.gvo", recovered.Snapshot());}
                status.recoveredDisk = true;
            }
        }
        }catch(const std::exception& e){
            status.state=VmState::Fault;status.resident=false;status.pid=0;status.stamp=Now();status.recoveredDisk=false;
            status.error=std::string("Disk recovery pending; active generation retained: ")+e.what();
            try{Publish(id,status);}catch(const std::exception&){}
            return status;
        }
        if (Ordinary(p / "current.gvs")) {
            try {
                CheckSnapshotFile(p / "current.gvs",config.ramMiB,config.contract=="resources"?config.diskGrowthBytes:0);
                status.state = VmState::Paused;
                status.error = "Worker exited; saved checkpoint awaits validated restore";
            }catch(const std::exception& e){status.state=VmState::Fault;status.error=e.what();}
        } else {
            status.state = VmState::Fault;
            status.error = "Worker exited without a complete checkpoint";
        }
        status.resident = false;
        status.pid = 0;
        status.stamp = Now();
        Publish(id, status);
        for (const auto &e : fs::directory_iterator(p))
            if (e.path().filename().string().starts_with(".active-"))
                RemoveTree(e.path());
    }
    if (probe.Held() && status.state != VmState::Starting)
        for (const auto &e : fs::directory_iterator(p)) {
            const auto name = e.path().filename().string();
            if (name.starts_with(".active-"))
                RemoveTree(e.path());
            else if (name.starts_with(".partial-")) {
                Require(Ordinary(e.path()), "Interrupted state file type rejected");
                fs::remove(e.path());
            }
        }
    if(config.contract=="resources"&&probe.Held()&&status.state!=VmState::Starting){
        for(const auto& e:fs::directory_iterator(config.diskFolder))if(e.path().filename().string().starts_with(".active-"))storage::RemoveGeneration(e.path(),config.diskOwner);
    }
    return status;
}
void VmManager::Create(const VmConfig &input, const fs::path &source, const fs::path &disk) {
    Name(input.id);
    Require(!fs::exists(Path(input.id)), "VM already exists");
    size_t count = 0;
    for (const auto &e : fs::directory_iterator(root_))
        if (e.is_directory() && e.path().filename().string()[0] != '.')
            ++count;
    Require(count < 16, "VM catalog limit reached");
    auto c = input;
    if(c.environment){
        environment::Validate(*c.environment);Require(c.contract=="storage"&&disk.empty(),"Synthetic environment requires its own blank volume and storage contract");
        c.persistent=c.environment->preserve!=0;c.hardware=environment::Hardware(*c.environment);
    }
    Require((!c.durationMs||guardian::session::ValidDuration(c.durationMs,c.checkpointMs)) && (c.script.empty()||(c.durationMs&&c.contract=="graphics")),"Session configuration rejected");
    gui::ValidateScript(c.script);
    Require(c.contract!="resources"||!c.hardware,"Resource kernel does not implement hardware identity profiles");
    if(c.contract!="resources"){if(!c.hardware)c.hardware=hardware::HostReference();hardware::Validate(*c.hardware);}
    Options(c.contract);
    Require(c.contract=="resources"||c.requestedVcpus==1,"SMP requires the resource kernel contract");
    const auto host=resources::Probe();
    c.requestedRamMiB=c.maximumResources?uint32_t(host.total/resources::MiB):c.ramMiB;
    c.ramMiB=resources::Admit(c.ramMiB,c.requestedVcpus,c.maximumResources,host,{},c.hostReserveMiB,c.contract=="resources"?c.diskGrowthBytes:0).ramMiB;
    guardian::storage::QuotaInfo quota{};quota.allocationLimit=c.volumeQuotaBytes;
    if(c.contract=="resources"){
        if(c.volumeQuotaBytes==guardian::storage::VolumeCapacity)c.volumeQuotaBytes=c.diskCapacityBytes;
        Require(c.diskCapacityBytes>=1ull<<30&&c.diskCapacityBytes<=8ull<<30&&c.diskCapacityBytes%guardian::scalable::ChunkBytes==0&&c.volumeQuotaBytes>=guardian::scalable::DataStart&&c.volumeQuotaBytes<=c.diskCapacityBytes&&c.volumeQuotaBytes%guardian::scalable::ChunkBytes==0,"Scalable volume bounds rejected");
        Require(c.diskGrowthBytes>=4ull<<20&&c.diskGrowthBytes<=256ull<<20&&c.diskGrowthBytes%guardian::scalable::ChunkBytes==0,"Scalable growth bounds rejected");
        Require(disk.empty(),"Scalable disks are guest-formatted; legacy base import requires the storage contract");
    }else Require(guardian::storage::ValidQuota(quota)&&c.overlayLimitBytes>=512&&c.overlayLimitBytes<=512+4096ull*544&&c.diskFolder.empty(),"Storage quota configuration rejected");
    Require(c.timeoutMs >= 100 && c.timeoutMs <= 30000,
            "VM resource bounds rejected");
    const auto image = KernelImage::Parse(ReadFile(source, KernelImage::MaxFileBytes));
    Require(c.contract!="resources"||image.SupportsResources(),"Imported kernel lacks the SMP/scalable resource contract");
    c.imageHash = Hash(image.FileBytes());
    Require(c.environment.has_value()==image.SupportsEnvironment(),"Synthetic kernels require create-environment; other kernels do not accept synthetic profiles");
    Require(!Options(c.contract).storage||c.volumeQuotaBytes==guardian::storage::VolumeCapacity||image.SupportsStorageQuota(),"Imported kernel lacks configurable volume quota support");
    auto base = disk.empty() ? EmptyBase() : ReadFile(disk, guardian::io::BlockBytes);
    Require(base.size() == guardian::io::BlockBytes, "Only 64 KiB base images are supported");
    const auto stage = root_ / (".create-" + Token());
    auto intent = stage;
    intent += ".intent";
    AtomicText(intent, Owner(c.id));
    fs::create_directory(stage);
    try {
        if(c.contract=="resources"){
            auto parent=c.diskFolder;
            if(parent.empty()){parent=root_/".disks";fs::create_directory(parent);}
            parent=fs::absolute(parent).lexically_normal();storage::SafeDiskParent(parent);
            c.diskOwner=Token();c.diskFolder=parent/OpaqueName(".disk-",c.diskOwner);
            Writer location;location.Text(c.diskOwner);location.Text(PathText(c.diskFolder));AtomicWrite(stage/".disk-create.gvt",Seal(location.data));
            storage::CreateLocation(c.diskFolder,c.diskOwner);
        }
        AtomicText(stage / ".owner", Owner(c.id));
        fs::create_directory(stage / "snapshots");
        StorageBudget budget(stage,c.storageLimitBytes,c.diskFolder);
        SaveConfig(stage / "config.gvc", c);
        AtomicWrite(stage / "image.gk", image.FileBytes());
        AtomicWrite(stage / "base.gvd", base);
        StatusFile(stage / "status.gvt", {});
        PublishDirectory(stage, Path(c.id));
        fs::remove(intent);
    } catch (...) {
        if (fs::exists(stage)){CleanupStagedDisk(stage);RemoveTree(stage);}
        if (fs::exists(intent))
            fs::remove(intent);
        throw;
    }
}
void VmManager::Spawn(const std::string &id, const std::string &mode, const fs::path &executable) {
    const auto c = Config(id);
    StorageBudget budget(Path(id),c.storageLimitBytes,c.diskFolder);
    unsigned active = 0;
    uint64_t total = 0;
    for (const auto &e : fs::directory_iterator(root_)) {
        const auto n = e.path().filename().string();
        if (n.empty() || n[0] == '.' || !e.is_directory())
            continue;
        const auto other = Config(n);
        auto status = ReadStatus(n);
        Lease lease(e.path() / "worker.lock", true);
        if (!lease.Held() || status.state == VmState::Starting) {
            ++active;
            total += other.ramMiB;
        }
    }
    Require(active < 4 && total + c.ramMiB <= 4*resources::MaxRamMiB, "Concurrent VM/RAM limit reached");
    (void)resources::Admit(c.ramMiB,c.requestedVcpus,false,resources::Probe(),{},c.hostReserveMiB,c.contract=="resources"?c.diskGrowthBytes:0);
    Require(Ordinary(executable), "Worker executable must be an ordinary file");
    {
        Lease lease(Path(id) / "worker.lock", true);
        Require(lease.Held(), "VM already has a worker");
    }
    if (Ordinary(Path(id) / "request.gvr")) {
        AtomicWrite(Path(id) / "abandoned-request.gvr", ReadFile(Path(id) / "request.gvr", 8192));
        fs::remove(Path(id) / "request.gvr");
    }
    const auto logfile = Path(id) / "worker.log";
    std::error_code logError;
    const auto logType = fs::symlink_status(logfile, logError);
    if (!logError && logType.type() != fs::file_type::not_found)
        Require(Ordinary(logfile) && fs::hard_link_count(logfile) == 1,
                "VM log must be a private ordinary file");
    else
        Require(!logError || logError == std::errc::no_such_file_or_directory, "Cannot inspect VM log");
    auto status = ReadStatus(id);
    StatusFile(Path(id) / "launch.gvt", status);
    status.resident = true;
    status.state = VmState::Starting;
    status.error.clear();
    status.stamp = Now();
    Publish(id, status);
    const auto directory = Path(id);
#ifdef _WIN32
    auto command = QuotedPath(executable.wstring()) + L" --worker " + QuotedPath(root_.wstring()) + L" " +
                   std::wstring(id.begin(), id.end()) + L" " + std::wstring(mode.begin(), mode.end());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, root_.c_str(), &startup, &process)) {
        status.state = VmState::Fault;
        status.error = "Cannot launch native VM worker";
        Publish(id, status);
        throw std::runtime_error(status.error);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    const auto exe = fs::absolute(executable).string(), catalog = root_.string();
    const pid_t child = ::fork();
    Require(child >= 0, "Cannot launch VM worker");
    if (!child) {
        ::setsid();
        const int input = ::open("/dev/null", O_RDONLY);
        const int output = ::open((directory / "worker.log").c_str(),
                                  O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        struct stat logInfo{};
        if (input < 0 || output < 0 || ::fstat(output, &logInfo) || !S_ISREG(logInfo.st_mode) ||
            logInfo.st_nlink != 1)
            _exit(126);
        ::dup2(input, 0);
        ::dup2(output, 1);
        ::dup2(output, 2);
        ::close(input);
        ::close(output);
        ::execl(exe.c_str(), exe.c_str(), "--worker", catalog.c_str(), id.c_str(), mode.c_str(),
                static_cast<char *>(nullptr));
        _exit(127);
    }
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30+c.ramMiB/8);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto now = ReadStatus(id, false);
        if (now.state == VmState::Starting)
            continue;
        Require(now.state != VmState::Fault && now.error.empty(), now.error.c_str());
        return;
    }
    throw std::runtime_error("VM worker startup did not acknowledge within resource-scaled startup deadline");
}
void VmManager::Start(const std::string &id, const fs::path &executable, bool paused) {
    const auto s = ReadStatus(id);
    {
        Require(!Resident(Path(id), s.resident) && (s.state == VmState::Stopped || s.state == VmState::Fault),
                "Only a stopped VM can start; use resume for a checkpoint");
    }
    Spawn(id, paused ? "hold" : "boot", executable);
}
void VmManager::Request(const std::string &id, const std::string &operation, const std::string &argument) {
    const auto c=Config(id);
    const auto p = Path(id);
    Require(!fs::exists(p / "request.gvr"), "An earlier VM command is still pending");
    const auto token = Token();
    Writer w;
    w.Text(token);
    w.Text(operation);
    w.Text(argument);
    AtomicWrite(p / "request.gvr", Seal(w.data));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30+c.ramMiB/8);
    while (std::chrono::steady_clock::now() < deadline) {
        if (Ordinary(p / "reply.gvr")) {
            auto data = Open(ReadFile(p / "reply.gvr", 8192));
            Reader r(data);
            auto answer = r.Text(128), error = r.Text();
            r.End();
            if (answer == token) {
                Require(error.empty(), error.c_str());
                return;
            }
        }
        Lease probe(p / "worker.lock", true);
        if (probe.Held()) {
            if (Ordinary(p / "reply.gvr")) {
                auto data = Open(ReadFile(p / "reply.gvr", 8192));
                Reader r(data);
                auto answer = r.Text(128), error = r.Text();
                r.End();
                if (answer == token) {
                    Require(error.empty(), error.c_str());
                    return;
                }
            }
            throw std::runtime_error("VM worker exited before acknowledging command");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("VM command remains pending after timeout; inspect status before retrying");
}
void VmManager::Command(const std::string &id, const std::string &operation, const std::string &argument,
                        const fs::path &executable) {
    const auto s = ReadStatus(id);
    const auto config=Config(id);StorageBudget budget(Path(id),config.storageLimitBytes,config.diskFolder);
    if (operation == "snapshot" || operation == "restore" || operation == "drop-snapshot")
        Name(argument);
    if (operation == "drop-snapshot") {
        const auto path = Path(id) / "snapshots" / (argument + ".gvs");
        Require(Ordinary(path), "Snapshot not found");
        fs::remove(path);
        return;
    }
    const bool active = Resident(Path(id), s.resident);
    if (active) {
        Request(id, operation, argument);
        return;
    }
    if (operation == "resume") {
        Require(s.state == VmState::Paused, "No paused VM to resume");
        Spawn(id, "resume", executable);
        return;
    }
    if (operation == "restore") {
        CheckSnapshotFile(Path(id) / "snapshots" / (argument + ".gvs"),config.ramMiB,config.contract=="resources"?config.diskGrowthBytes:0);
        Spawn(id, "load:" + argument, executable);
        return;
    }
    if (operation == "suspend") { Require(s.state == VmState::Paused,"No paused VM to suspend"); return; }
    if ((operation == "restart" || operation == "shutdown") && s.state == VmState::Paused) {
        if (config.persistent && (Options(config.contract).storage||config.contract=="resources") && !s.recoveredDisk){
            resources::Reservation memory(16,1,config.hostReserveMiB,config.contract=="resources"?config.diskGrowthBytes:0);
            const auto data=SnapshotDisk(Path(id)/"current.gvs",config.ramMiB,config.imageHash,config.contract=="resources"?config.diskGrowthBytes:0);
            Require(data.size()>=512,"Checkpoint storage image missing");
            AtomicWrite(config.contract=="resources"?config.diskFolder/"disk.gvs":Path(id)/"disk.gvo",data);
        }
    }
    if (operation == "restart") {
        Spawn(id, "boot", executable);
        return;
    }
    if (operation == "shutdown") {
        auto out = s;
        out.resident = false;
        out.pid = 0;
        out.state = VmState::Stopped;
        out.error.clear();
        out.stamp = Now();
        Publish(id, out);
        return;
    }
    throw std::runtime_error("Operation requires an active VM worker");
}
void VmManager::ConfigureScalableStorage(const std::string& id,uint64_t quota,uint64_t growth,uint64_t total){
    auto c=Config(id);const auto s=ReadStatus(id);const auto p=Path(id);
    Require(c.contract=="resources","Scalable disk limits require the resource kernel contract");
    Require(!Resident(p,s.resident)&&(s.state==VmState::Stopped||s.state==VmState::Fault),"Disk limit changes require a stopped VM");
    Require(quota>=guardian::scalable::DataStart&&quota<=c.diskCapacityBytes&&quota%guardian::scalable::ChunkBytes==0,"Scalable allocation quota rejected");
    Require(growth>=4ull<<20&&growth<=256ull<<20&&growth%guardian::scalable::ChunkBytes==0,"Scalable growth limit must be 4..256 MiB");
    Require(quota==c.volumeQuotaBytes||(!fs::exists(p/"current.gvs")&&fs::is_empty(p/"snapshots")),"Changing allocation quota requires a fresh VM without checkpoints");
    if(Ordinary(c.diskFolder/"disk.gvs"))Require(fs::file_size(c.diskFolder/"disk.gvs")+(128ull<<10)<=growth,"Growth limit below current recoverable disk image");
    if(total){Require(total>=1ull<<20&&total<=64ull<<30,"Aggregate storage bounds rejected");Require(CombinedUsage(p,c.diskFolder).charged+StorageControlReserve<=total,"Aggregate quota below existing storage");c.storageLimitBytes=total;}
    (void)resources::Admit(c.ramMiB,c.requestedVcpus,false,resources::Probe(),{},c.hostReserveMiB,growth);
    c.volumeQuotaBytes=quota;c.diskGrowthBytes=growth;SaveConfig(p/"config.gvc",c);
}
void VmManager::ConfigureDiskFolder(const std::string& id,const fs::path& selected){
    auto c=Config(id);const auto status=ReadStatus(id);const auto p=Path(id);
    Require(c.contract=="resources","Independent disk folders currently require the resource kernel contract");
    Require(!Resident(p,status.resident)&&(status.state==VmState::Stopped||status.state==VmState::Fault),"Disk migration requires a stopped VM");
    resources::Reservation memory(16,1,c.hostReserveMiB,c.diskGrowthBytes);
    const auto parent=fs::absolute(selected).lexically_normal();storage::SafeDiskParent(parent);
    const auto next=parent/OpaqueName(".disk-",Token());const auto old=c.diskFolder;
    const auto usage=CombinedUsage(p,old);uint64_t copy=0;
    for(const auto& e:fs::directory_iterator(old)){
        const auto name=e.path().filename().string();Require(name==".guardian-location-owner"||name=="disk.gvs","Disk migration found an unresolved active generation");
        if(name=="disk.gvs"){Require(Ordinary(e.path()),"Disk migration source type rejected");copy+=fs::file_size(e.path());}
    }
    Require(copy+StorageControlReserve<=c.storageLimitBytes&&usage.charged<=c.storageLimitBytes-copy-StorageControlReserve,"Disk migration exceeds aggregate VM quota");
    const auto available=fs::space(parent).available;Require(available>HostDiskReserve&&copy+StorageControlReserve<=available-HostDiskReserve,"Disk migration exceeds destination reserve");
    StorageBudget budget(p,c.storageLimitBytes,old);Writer intent;intent.Text(c.diskOwner);intent.Text(PathText(old));intent.Text(PathText(next));AtomicWrite(p/"disk-move.gvt",Seal(intent.data));
    try{
        storage::CreateLocation(next,c.diskOwner);
        if(Ordinary(old/"disk.gvs"))AtomicWrite(next/"disk.gvs",ReadFile(old/"disk.gvs",c.diskGrowthBytes));
        c.diskFolder=next;SaveConfig(p/"config.gvc",c);RecoverDiskMove(p,c);
    }catch(...){const auto committed=LoadConfig(p/"config.gvc");RecoverDiskMove(p,committed);throw;}
}
void VmManager::ConfigureResources(const std::string& id,uint32_t ram,uint32_t vcpus,bool maximum,uint32_t reserve){
    auto c=Config(id);const auto s=ReadStatus(id);if(reserve!=UINT32_MAX)c.hostReserveMiB=reserve;
    const auto host=resources::Probe();const auto plan=resources::Admit(ram,vcpus,maximum,host,{},c.hostReserveMiB,c.contract=="resources"?c.diskGrowthBytes:0);
    Require(c.contract=="resources"||vcpus==1,"SMP requires the resource kernel contract");
    Require(!Resident(Path(id),s.resident)&&(s.state==VmState::Stopped||s.state==VmState::Fault),"Resource changes require a stopped VM");
    c.requestedRamMiB=maximum?uint32_t(host.total/resources::MiB):ram;c.requestedVcpus=vcpus;c.ramMiB=plan.ramMiB;c.maximumResources=maximum;
    SaveConfig(Path(id)/"config.gvc",c);
    if(fs::exists(Path(id)/"current.gvs"))fs::remove(Path(id)/"current.gvs");
}
void VmManager::ConfigureStorage(const std::string& id,uint64_t volume,uint64_t overlay,uint64_t total){
    auto c=Config(id);const auto s=ReadStatus(id);guardian::storage::QuotaInfo q{};q.allocationLimit=volume;
    Require(!Resident(Path(id),s.resident)&&(s.state==VmState::Stopped||s.state==VmState::Fault),"Storage changes require a stopped VM");
    Require(guardian::storage::ValidQuota(q)&&overlay>=512&&overlay<=512+4096ull*544,"Storage quota outside reference volume limits");
    Require(volume==c.volumeQuotaBytes||(!fs::exists(Path(id)/"current.gvs")&&fs::is_empty(Path(id)/"snapshots")),"Changing volume allocation requires a fresh VM without checkpoints");
    if(Ordinary(Path(id)/"disk.gvo"))Require(AllocatedBytes(Path(id)/"disk.gvo")<=overlay,"Overlay quota below current backing size");
    if(total){Require(total>=(1ull<<20)&&total<=(64ull<<30),"VM storage budget must be 1 MiB..64 GiB");Require(std::max(StorageUsage(Path(id)).allocated,StorageUsage(Path(id)).logical)+StorageControlReserve<=total,"Storage budget below existing allocation");c.storageLimitBytes=total;}
    const auto image=KernelImage::Parse(ReadFile(Path(id)/"image.gk",KernelImage::MaxFileBytes));Require(!Options(c.contract).storage||volume==guardian::storage::VolumeCapacity||image.SupportsStorageQuota(),"Imported kernel lacks configurable volume quota support");
    c.volumeQuotaBytes=volume;c.overlayLimitBytes=overlay;SaveConfig(Path(id)/"config.gvc",c);
}
std::string VmManager::Resources(const std::string& id){
    const auto c=Config(id);const auto s=ReadStatus(id);const auto h=resources::Probe();const auto global=resources::Inspect();const bool provisioned=s.resident&&s.state!=VmState::Starting;const bool volume=c.contract=="storage";const auto image=KernelImage::Parse(ReadFile(Path(id)/"image.gk",KernelImage::MaxFileBytes));
    if(c.contract=="resources"){
        const auto usage=CombinedUsage(Path(id),c.diskFolder);auto backing=c.diskFolder/"disk.gvs";
        if(!s.active.empty()){
            Require(s.active.starts_with(".active-")&&s.active.find_first_not_of(".active-0123456789")==std::string::npos,"Active disk path rejected");
            const auto retained=c.diskFolder/s.active/"payload.gvd2";if(Ordinary(retained))backing=retained;
        }
        const auto allocated=Ordinary(backing)?AllocatedBytes(backing):0,logical=Ordinary(backing)?fs::file_size(backing):0;
        guardian::scalable::Space space{};uint64_t charge=0;const bool known=ResourceSpace(Path(id),c,s,space,charge);
        const auto host=fs::space(c.diskFolder).available;
        const uint64_t physical=allocated+(s.resident?0:2ull*guardian::scalable::ChunkBytes);
        const auto usedBacking=std::max(physical,known?charge:uint64_t(0));
        const uint64_t backingFree=usedBacking<c.diskGrowthBytes?c.diskGrowthBytes-usedBacking:0;
        uint64_t writable=known&&space.allocated<c.volumeQuotaBytes?std::min(space.free,c.volumeQuotaBytes-space.allocated):0;
        writable=std::min(writable,std::min(backingFree,StorageAvailable(Path(id),c.storageLimitBytes,c.diskFolder)));
        writable=std::min(writable,host>HostDiskReserve?host-HostDiskReserve:0);writable=writable/guardian::scalable::ChunkBytes*guardian::scalable::ChunkBytes;
        std::ostringstream out;
        out<<"{\"schema\":2,\"ram\":{\"requestedMiB\":"<<c.requestedRamMiB<<",\"admittedMiB\":"<<c.ramMiB<<",\"provisionedMiB\":"<<(provisioned?c.ramMiB:0)<<",\"supportedMaximumMiB\":1024,\"reservationBytes\":"<<(uint64_t(c.ramMiB)*resources::MiB*resources::RamCopies+resources::WorkerOverhead+4*c.diskGrowthBytes)<<"},\"cpu\":{\"requested\":"<<c.requestedVcpus<<",\"admitted\":"<<c.requestedVcpus<<",\"provisioned\":"<<(provisioned?c.requestedVcpus:0)<<",\"smpSupported\":true},\"disk\":{\"directory\":"<<Quote(PathText(c.diskFolder))<<",\"blockCapacityBytes\":"<<c.diskCapacityBytes<<",\"volumeCapacityBytes\":"<<c.diskCapacityBytes<<",\"allocationQuotaBytes\":"<<c.volumeQuotaBytes<<",\"growthLimitBytes\":"<<c.diskGrowthBytes<<",\"storageLimitBytes\":"<<c.storageLimitBytes<<",\"storageAllocatedBytes\":"<<usage.allocated<<",\"storageLogicalBytes\":"<<usage.logical<<",\"storageChargedBytes\":"<<usage.charged<<",\"storageAvailableBytes\":"<<StorageAvailable(Path(id),c.storageLimitBytes,c.diskFolder)<<",\"backingFileBytes\":"<<logical<<",\"backingAllocatedBytes\":"<<allocated<<",\"guestSpaceKnown\":"<<(known?"true":"false")<<",\"volumeAllocatedBytes\":"<<(known?std::to_string(space.allocated):"null")<<",\"volumeFreeBytes\":"<<(known?std::to_string(space.free):"null")<<",\"availableBytes\":"<<(known?std::to_string(writable):"null")<<",\"hostReserveBytes\":"<<HostDiskReserve<<"},\"globalReservations\":{\"workers\":"<<global.workers<<",\"committedBytes\":"<<global.committed<<",\"vcpus\":"<<global.cpus<<"}}";return out.str();
    }
    const auto directory=Path(id);auto file=directory/"disk.gvo";
    if(s.resident&&!s.active.empty()){Require(s.active.starts_with(".active-")&&s.active.find_first_not_of(".active-0123456789")==std::string::npos,"Active disk path rejected");file=directory/s.active/"disk.gvo";}
    const auto backing=Ordinary(file)?fs::file_size(file):0;
    const auto available=fs::space(directory).available;const auto usage=StorageUsage(directory);const auto allocated=Ordinary(file)?AllocatedBytes(file):0;
    return "{\"schema\":1,\"ram\":{\"requestedMiB\":"+std::to_string(c.requestedRamMiB)+",\"admittedMiB\":"+std::to_string(c.ramMiB)+",\"provisionedMiB\":"+std::to_string(provisioned?c.ramMiB:0)+",\"maximum\":"+(c.maximumResources?"true":"false")+",\"supportedMaximumMiB\":1024,\"reservationBytes\":"+std::to_string(uint64_t(c.ramMiB)*resources::MiB*resources::RamCopies+resources::WorkerOverhead)+"},\"cpu\":{\"requested\":"+std::to_string(c.requestedVcpus)+",\"admitted\":1,\"provisioned\":"+(provisioned?"1":"0")+",\"smpSupported\":false},\"host\":{\"totalRamBytes\":"+std::to_string(h.total)+",\"availableRamBytes\":"+std::to_string(h.available)+",\"memoryReserveBytes\":"+std::to_string(std::max(resources::Reserve(h,c.hostReserveMiB),global.reserve))+",\"logicalProcessors\":"+std::to_string(h.cpus)+"},\"globalReservations\":{\"workers\":"+std::to_string(global.workers)+",\"committedBytes\":"+std::to_string(global.committed)+",\"vcpus\":"+std::to_string(global.cpus)+"},\"disk\":{\"provisioned\":"+((volume&&provisioned)?"true":"false")+",\"volumeQuotaSupported\":"+((volume&&image.SupportsStorageQuota())?"true":"false")+",\"directory\":"+Quote(directory.generic_string())+",\"blockCapacityBytes\":65536,\"volumeCapacityBytes\":16384,\"allocationQuotaBytes\":"+std::to_string(c.volumeQuotaBytes)+",\"overlayLimitBytes\":"+std::to_string(c.overlayLimitBytes)+",\"backingFileBytes\":"+std::to_string(backing)+",\"backingQuotaRemainingBytes\":"+std::to_string(backing<c.overlayLimitBytes?c.overlayLimitBytes-backing:0)+",\"storageLimitBytes\":"+std::to_string(c.storageLimitBytes)+",\"storageAllocatedBytes\":"+std::to_string(usage.allocated)+",\"storageLogicalBytes\":"+std::to_string(usage.logical)+",\"storageAvailableBytes\":"+std::to_string(StorageAvailable(directory,c.storageLimitBytes))+",\"backingAllocatedBytes\":"+std::to_string(allocated)+",\"hostAvailableBytes\":"+std::to_string(available)+",\"hostReserveBytes\":268435456}}";
}
void VmManager::ConfigureNetwork(const std::string& id,const net::Policy& policy) {
    auto c=Config(id);const auto s=ReadStatus(id);
    Require(!Resident(Path(id),s.resident)&&(s.state==VmState::Stopped||s.state==VmState::Fault),"Network changes require a stopped VM");
    net::Validate(policy);const bool ownedEcho=c.contract=="storage"&&policy.mode==guardian::net::Mode::Simulated&&KernelImage::Read(Path(id)/"image.gk").SupportsAnalysisFixture();Require(c.contract=="network"||policy.mode==guardian::net::Mode::Offline||ownedEcho,"Image lacks the requested network contract");
    c.network=policy;SaveConfig(Path(id)/"config.gvc",c);
    if(fs::exists(Path(id)/"current.gvs"))fs::remove(Path(id)/"current.gvs");
}
void VmManager::ConfigureSession(const std::string& id, uint32_t duration, uint32_t checkpoint, const fs::path& script) {
    auto c=Config(id); const auto s=ReadStatus(id);
    Require(!Resident(Path(id),s.resident) && s.state!=VmState::Starting, "Suspend or stop the VM before changing session configuration");
    Require(guardian::session::ValidDuration(duration,checkpoint), "Session duration must be 100 ms..7 days; checkpoints 100..30000 ms");
    if(!script.empty()) {
        Require(c.contract=="graphics", "Input scripts require the guest graphics contract");
        const auto next=gui::ReadScript(script);
        Writer oldPolicy,newPolicy;gui::SaveScript(oldPolicy,c.script);gui::SaveScript(newPolicy,next);
        Require(oldPolicy.data==newPolicy.data || (!fs::exists(Path(id)/"current.gvs") && fs::is_empty(Path(id)/"snapshots")), "Changing input policy requires a fresh VM without checkpoints");
        c.script=next;
    }
    Require(c.durationMs || (!fs::exists(Path(id)/"current.gvs") && fs::is_empty(Path(id)/"snapshots")), "Enabling session mode requires a fresh VM without checkpoints");
    c.durationMs=duration;c.checkpointMs=checkpoint;SaveConfig(Path(id)/"config.gvc",c);
}
void VmManager::Configure(const std::string &id, uint32_t ram, uint32_t timeout) {
    auto c = Config(id);
    const auto s = ReadStatus(id);
    Require(!Resident(Path(id), s.resident) && (s.state == VmState::Stopped || s.state == VmState::Fault),
            "Resource changes require a stopped VM");
    Require(timeout >= 100 && timeout <= 30000, "VM timeout bounds rejected");
    (void)resources::Admit(ram,1,false,resources::Probe(),{},c.hostReserveMiB,c.contract=="resources"?c.diskGrowthBytes:0);
    c.requestedRamMiB=ram;c.maximumResources=false;
    c.ramMiB = ram;
    c.timeoutMs = timeout;
    SaveConfig(Path(id) / "config.gvc", c);
    if (fs::exists(Path(id) / "current.gvs"))
        fs::remove(Path(id) / "current.gvs");
}
hardware::Profile VmManager::HardwareProfile(const std::string& id) {
    const auto c=Config(id);
    Require(c.hardware.has_value(),"Legacy VM has no hardware profile; use hardware-config --host-reference while stopped");
    return *c.hardware;
}
std::string VmManager::Environment(const std::string& id){const auto c=Config(id);Require(c.environment.has_value(),"VM has no synthetic environment profile");return environment::Json(*c.environment);}
std::string VmManager::Hardware(const std::string& id) {
    const auto c=Config(id);const auto p=HardwareProfile(id);
    return hardware::Json(hardware::Inventory(p,uint64_t(c.ramMiB)<<20,c.contract));
}
void VmManager::ConfigureHardware(const std::string& id,const hardware::Profile& profile) {
    auto c=Config(id);const auto s=ReadStatus(id);hardware::Validate(profile);
    Require(c.contract!="resources","Resource kernel does not implement hardware identity profiles");
    Require(!Resident(Path(id),s.resident)&&(s.state==VmState::Stopped||s.state==VmState::Fault),"Hardware changes require a stopped VM");
    Require(!fs::exists(Path(id)/"current.gvs")&&fs::is_empty(Path(id)/"snapshots"),
        "Hardware changes require a VM without checkpoints or named snapshots; create a fresh VM");
    if(c.environment){const auto nt=hardware::Inventory(profile,uint64_t(c.ramMiB)<<20,"storage");Require(nt.ntBuild==c.environment->ntBuild,"Hardware NT must match the synthetic profile");}
    c.hardware=profile;SaveConfig(Path(id)/"config.gvc",c);
}
void VmManager::ImportDriver(const std::string& id,const std::string& name,const fs::path& file,
                            const std::string& nt,const std::string& device) {
    const auto config=Config(id);StorageBudget budget(Path(id),config.storageLimitBytes,config.diskFolder);
    Name(name);const auto profile=HardwareProfile(id);const auto s=ReadStatus(id);
    Require(!Resident(Path(id),s.resident)&&(s.state==VmState::Stopped||s.state==VmState::Fault),"Driver import requires a stopped VM");
    size_t count=0;
    for(const auto& entry:fs::directory_iterator(Path(id)))if(entry.path().filename().string().rfind("driver-",0)==0)++count;
    Require(count<16,"Driver package quota reached");
    const auto path=Path(id)/("driver-"+name+".gdp");Require(!fs::exists(path),"Driver package already exists");
    const auto bundle=hardware::ImportDriver(profile,file,nt,device);
    AtomicWrite(path,bundle);
}
std::string VmManager::Drivers(const std::string& id) {
    const auto profile=HardwareProfile(id);std::string out="[";size_t count=0;
    std::string selected;guardian::driver::Report observed{};bool observedValid=false;uint64_t installedHash=0;
    if(Ordinary(Path(id)/"installed.gdi")){
        auto data=Open(ReadFile(Path(id)/"installed.gdi",hardware::MaxPackageBytes+(1ull<<20)));Reader reader(data);Require(reader.Pod<uint32_t>()==1,"Installed driver descriptor rejected");
        selected=reader.Text(32);Name(selected);auto bundle=reader.Bytes(hardware::MaxPackageBytes),api=reader.Bytes(guardian::driver::MaxFile);reader.End();
        installedHash=Hash(bundle);
        const auto package=hardware::DriverPackage::Decode(bundle);Require(package.Compatibility(profile).empty(),"Installed driver/profile mismatch");
        if(Ordinary(Path(id)/"installed-proof.gds")){
            auto proof=Open(ReadFile(Path(id)/"installed-proof.gds",4096));Reader r(proof);const auto hash=r.Pod<uint64_t>();observed=r.Pod<guardian::driver::Report>();r.End();
            Writer identity;identity.Bytes(package.image);identity.Bytes(api);
            observedValid=hash==installedHash&&observed.magic==guardian::driver::Magic&&observed.version==guardian::driver::Version&&observed.bytes==sizeof(observed)&&observed.packageHash==Hash(identity.data)&&observed.bootId==ReadStatus(id).boot;
        }
    }
    for(const auto& entry:fs::directory_iterator(Path(id))){
        const auto name=entry.path().filename().string();
        if(name.rfind("driver-",0)!=0)continue;
        Require(++count<=16&&entry.path().extension()==".gdp","Driver catalog entry rejected");
        if(count>1)out+=',';
        const auto bundle=ReadFile(entry.path(),hardware::MaxPackageBytes);std::string audit;
        if(hardware::IsPackage(bundle)){const auto package=hardware::DriverPackage::Decode(bundle);audit=hardware::PackageJson(package,profile,observedValid&&Hash(bundle)==installedHash&&name=="driver-"+selected+".gdp"?&observed:nullptr);}
        else audit=hardware::DriverJson(bundle,profile);
        out+="{\"package\":"+Quote(name)+",\"configured\":"+(name=="driver-"+selected+".gdp"?"true":"false")+",\"audit\":"+audit+"}";
    }
    return out+"]";
}
void VmManager::InstallDriver(const std::string& id,const std::string& name,const fs::path& apiFile){
    Name(name);const auto c=Config(id);StorageBudget budget(Path(id),c.storageLimitBytes,c.diskFolder);const auto s=ReadStatus(id);
    Require(c.contract=="hardware"&&c.hardware.has_value(),"Driver installation requires a hardware-contract VM");
    Require(!Resident(Path(id),s.resident)&&(s.state==VmState::Stopped||s.state==VmState::Fault),"Driver installation requires a stopped VM");
    Require(!fs::exists(Path(id)/"current.gvs")&&fs::is_empty(Path(id)/"snapshots"),"Driver installation requires a fresh VM without checkpoints");
    const auto bytes=ReadFile(Path(id)/("driver-"+name+".gdp"),hardware::MaxPackageBytes);const auto package=hardware::DriverPackage::Decode(bytes);
    const auto reason=package.Compatibility(*c.hardware);Require(reason.empty(),reason.c_str());
    const auto api=ReadFile(apiFile,guardian::driver::MaxFile);Require(!api.empty(),"Guest driver API image is absent");
    Writer w;w.Pod(uint32_t(1));w.Text(name);w.Bytes(bytes);w.Bytes(api);AtomicWrite(Path(id)/"installed.gdi",Seal(w.data));
}
void VmManager::Clone(const std::string &id, const std::string &target) {
    Name(target);
    auto c = Config(id);const auto sourceDisk=c.diskFolder;
    Require(!fs::exists(Path(target)), "Clone destination exists");
    size_t count = 0;
    for (const auto &e : fs::directory_iterator(root_))
        if (e.is_directory() && e.path().filename().string()[0] != '.')
            ++count;
    Require(count < 16, "VM catalog limit reached");
    auto status=ReadStatus(id);
    const bool active=Resident(Path(id),status.resident);
    if(active){Request(id,"checkpoint",{});status=ReadStatus(id);}
    resources::Reservation memory((active||status.state==VmState::Paused)?c.ramMiB:16,c.requestedVcpus,c.hostReserveMiB,c.contract=="resources"?c.diskGrowthBytes:0);
    c.id = target;
    const auto stage = root_ / (".create-" + Token());
    auto intent = stage;
    intent += ".intent";
    AtomicText(intent, Owner(c.id));
    fs::create_directory(stage);
    try {
        if(c.contract=="resources"){
            c.diskOwner=Token();c.diskFolder=sourceDisk.parent_path()/OpaqueName(".disk-",c.diskOwner);
            Writer location;location.Text(c.diskOwner);location.Text(PathText(c.diskFolder));AtomicWrite(stage/".disk-create.gvt",Seal(location.data));storage::CreateLocation(c.diskFolder,c.diskOwner);
            if(Ordinary(sourceDisk/"disk.gvs"))AtomicWrite(c.diskFolder/"disk.gvs",ReadFile(sourceDisk/"disk.gvs",c.diskGrowthBytes));
        }
        AtomicText(stage / ".owner", Owner(target));
        fs::create_directory(stage / "snapshots");
        StorageBudget budget(stage,c.storageLimitBytes,c.diskFolder);
        SaveConfig(stage / "config.gvc", c);
        size_t packages=0;
        if(Ordinary(Path(id)/"installed.gdi"))AtomicWrite(stage/"installed.gdi",ReadFile(Path(id)/"installed.gdi",hardware::MaxPackageBytes+(1ull<<20)));
        for(const auto& entry:fs::directory_iterator(Path(id))){
            if(entry.path().filename().string().rfind("driver-",0)!=0)continue;
            Require(++packages<=16&&c.hardware.has_value()&&entry.path().extension()==".gdp","Driver clone catalog rejected");
            const auto bundle=ReadFile(entry.path(),hardware::MaxPackageBytes);
            (void)hardware::DriverJson(bundle,*c.hardware);
            AtomicWrite(stage/entry.path().filename(),bundle);
        }
        AtomicWrite(stage / "image.gk", ReadFile(Path(id) / "image.gk", KernelImage::MaxFileBytes));
        AtomicWrite(stage / "base.gvd", ReadFile(Path(id) / "base.gvd", 65536));
        if (active || status.state == VmState::Paused) {
            CheckSnapshotFile(Path(id)/"current.gvs",c.ramMiB,c.contract=="resources"?c.diskGrowthBytes:0);
            const auto point = ReadFile(Path(id) / "current.gvs",SnapshotLimit(c.ramMiB,c.contract=="resources"?c.diskGrowthBytes:0));
            {const auto decoded=SessionSnapshot::Decode(point);Require(decoded.machine.ram.size()==(uint64_t(c.ramMiB)<<20),"Clone checkpoint RAM mismatch");}
            AtomicWrite(stage / "current.gvs", point);
            status.state = VmState::Paused;
            status.resident = false;
            status.recoveredDisk = false;
            status.active.clear();
            status.pid = 0;
            status.generation = Token();
            status.error.clear();
        } else {
            status = {};
            if (Ordinary(Path(id) / "disk.gvo"))
                AtomicWrite(stage / "disk.gvo", ReadFile(Path(id) / "disk.gvo", 3 << 20));
        }
        StatusFile(stage / "status.gvt", status);
        PublishDirectory(stage, Path(target));
        fs::remove(intent);
    } catch (...) {
        if (fs::exists(stage)){CleanupStagedDisk(stage);RemoveTree(stage);}
        if (fs::exists(intent))
            fs::remove(intent);
        throw;
    }
}
void VmManager::Delete(const std::string &id) {
    const auto config=Config(id);
    const auto status = ReadStatus(id);
    {
        Require(!Resident(Path(id), status.resident) && status.state != VmState::Starting,
                "Shutdown the VM before deletion");
    }
    if(config.contract=="resources")storage::ValidateLocationRemoval(config.diskFolder,config.diskOwner);
    const auto destination = root_ / (".delete-" + Token());
    auto intent = destination;
    intent += ".intent";
    AtomicText(intent, Owner(id));
    PublishDirectory(Path(id), destination);
    if(config.contract=="resources")storage::RemoveLocation(config.diskFolder,config.diskOwner);
    RemoveTree(destination);
    fs::remove(intent);
}
std::string VmManager::Status(const std::string &id) {
    const auto c = Config(id);
    const auto s = ReadStatus(id);
    return "{\"schema\":9,\"id\":" + Quote(id) + ",\"state\":" + Quote(Label(s.state)) +
           ",\"ramMiB\":" + std::to_string(c.ramMiB) +
           ",\"network\":"+Quote(net::Label(c.network.mode))+",\"networkNode\":"+std::to_string(c.network.node)+
           ",\"vcpus\":"+std::to_string(c.requestedVcpus)+",\"persistent\":" + (c.persistent ? "true" : "false") +
           ",\"syntheticEnvironment\":"+std::string(c.environment?"true":"false")+
           ",\"durationMs\":"+std::to_string(c.durationMs)+",\"checkpointMs\":"+std::to_string(c.checkpointMs)+",\"timeMode\":\"real\",\"scriptEvents\":"+std::to_string(c.script.size())+
           ",\"bootId\":" + std::to_string(s.boot) + ",\"virtualNs\":" + std::to_string(s.virtualNs) +
           ",\"recoveredDisk\":" + (s.recoveredDisk ? "true" : "false") +
           ",\"workerPid\":" + std::to_string(s.pid) + ",\"resident\":" + (s.resident ? "true" : "false") +
           ",\"generation\":" + Quote(s.generation) + ",\"error\":" + Quote(s.error) + "}";
}
std::string VmManager::List() {
    std::string out = "[";
    for (const auto &e : fs::directory_iterator(root_)) {
        auto n = e.path().filename().string();
        if (n.empty() || n[0] == '.' || !e.is_directory())
            continue;
        if (out.size() > 1)
            out += ',';
        out += Status(n);
    }
    return out + "]";
}
int VmManager::Worker(const std::string &id, const std::string &mode) {
    const auto c = Config(id);
    const auto p = Path(id);
    Lease ownership(p / "worker.lock");
    auto status = ReadStatus(id, false);
    net::Options networking;networking.policy=c.network;networking.catalog=root_;networking.capture=p;networking.vm=id;
    std::unique_ptr<KernelBootSession> session;
    StorageBudget storageBudget(p,c.storageLimitBytes,c.diskFolder);
    uint64_t installedBundleHash=0;
    fs::path active;
    bool started = false;
    std::unique_ptr<resources::Reservation> resourceReservation;
#ifdef _WIN32
    status.pid = GetCurrentProcessId();
#else
    status.pid = uint64_t(getpid());
#endif
    auto build = [&](const SessionSnapshot *point) {
        const auto work = (c.contract=="resources"?c.diskFolder:p) / (c.contract=="resources"?OpaqueName(".active-",Token()):".active-"+Token());
        fs::create_directory(work);
        std::unique_ptr<KernelBootSession> candidate;
        try {
            if(!networking.transport&&c.network.mode==guardian::net::Mode::Lab)networking.transport=net::LabTransport(networking);
            else if(!networking.transport&&c.network.mode==guardian::net::Mode::Proxy)networking.transport=net::ProxyTransport(c.network);
            AtomicWrite(work / "base.gvd", ReadFile(p / "base.gvd", 65536));
            if (point&&c.contract!="resources")
                AtomicWrite(work / "disk.gvo", point->disk);
            else if (c.contract!="resources"&&c.persistent && Ordinary(p / "disk.gvo"))
                AtomicWrite(work / "disk.gvo", ReadFile(p / "disk.gvo", 3 << 20));
            const auto image = KernelImage::Parse(ReadFile(p / "image.gk", KernelImage::MaxFileBytes));
            Require(Hash(image.FileBytes()) == c.imageHash, "Imported image fingerprint changed");
            candidate = std::make_unique<KernelBootSession>();
            auto options = Options(c.contract);options.vcpus=c.requestedVcpus;if(image.SupportsAnalysisFixture())options.network=true;
            const auto boot = point ? point->BootId()
                                    : Hash({reinterpret_cast<const uint8_t *>(work.c_str()),
                                            work.native().size() * sizeof(fs::path::value_type)});
            if(c.contract=="network"||image.SupportsAnalysisFixture()){const auto history=p/"network-history";if(fs::exists(history)){SafeDirectory(history);size_t count=0;for(const auto& e:fs::directory_iterator(history)){(void)e;++count;}Require(count<64,"Network audit history quota reached (64 sessions)");}networking.capture=history/Token();fs::create_directories(networking.capture);}
            gui::Options graphics;
            hardware::DriverOptions driver;
            if(c.contract=="hardware"){
                auto data=Open(ReadFile(p/"installed.gdi",hardware::MaxPackageBytes+(1ull<<20)));Reader r(data);Require(r.Pod<uint32_t>()==1,"Installed driver descriptor rejected");
                const auto name=r.Text(32);Name(name);const auto bundle=r.Bytes(hardware::MaxPackageBytes);driver.api=r.Bytes(guardian::driver::MaxFile);r.End();
                const auto package=hardware::DriverPackage::Decode(bundle);Require(c.hardware.has_value(),"Driver VM lacks a hardware profile");
                const auto reason=package.Compatibility(*c.hardware);Require(reason.empty(),reason.c_str());driver.image=package.image;driver.service=package.service;installedBundleHash=Hash(bundle);
            }
            if(c.contract=="graphics"){
                const auto history=p/"graphics-history";
                if(fs::exists(history)){SafeDirectory(history);size_t count=0;for(const auto& e:fs::directory_iterator(history)){(void)e;++count;}Require(count<64,"Graphics audit history quota reached (64 sessions)");}
                graphics.output=history/Token();fs::create_directories(graphics.output);
                if(c.durationMs){graphics.continuous=true;graphics.script=c.script;}
                else graphics.input=gui::ReferenceInput();
            }
            storage::SparseOptions scalable;
            if(c.contract=="resources"){
                // The legacy base is only a tiny fixture; the scalable backing is separate.
                fs::remove(work/"base.gvd");scalable.directory=work;scalable.owner=c.diskOwner;scalable.capacity=c.diskCapacityBytes;scalable.growthLimit=c.diskGrowthBytes;scalable.create=true;
                scalable.aggregateAvailable=[&]{return StorageAvailable(p,c.storageLimitBytes,c.diskFolder);};
                if(point||(c.persistent&&Ordinary(c.diskFolder/"disk.gvs"))){storage::SparseDisk backing(scalable);backing.Restore(point?point->disk:ReadFile(c.diskFolder/"disk.gvs",c.diskGrowthBytes));scalable.create=false;}
            }
            candidate->Prepare(image, uint64_t(c.ramMiB) << 20, boot ? boot : 1, options,
                               {work / "base.gvd", work / "disk.gvo", true,c.overlayLimitBytes,c.volumeQuotaBytes,HostDiskReserve,p,c.storageLimitBytes},networking,graphics,c.hardware?&*c.hardware:nullptr,driver,c.contract=="resources"?&scalable:nullptr,c.environment?&*c.environment:nullptr,&c.analysisPolicy,c.analysisScenario);
            if (point)
                candidate->Restore(*point);
            return std::pair(std::move(candidate), work);
        } catch (...) {
            candidate.reset();
            RemoveWork(work,c);
            throw;
        }
    };
    auto replace = [&](const SessionSnapshot *point) {
        auto [candidate, work] = build(point);
        session = std::move(candidate);
        if (!active.empty())
            RemoveWork(active,c);
        active = work;
    };
    auto spaceProof=[&]{
        if(c.contract!="resources")return;
        guardian::scalable::Space space{};uint64_t charge=0;const bool known=session->QueryResourceSpace(space,&charge);
        Writer w;w.Pod(c.imageHash);w.Text(status.generation);w.Pod(c.volumeQuotaBytes);w.Pod(c.diskGrowthBytes);w.Pod(uint32_t(known));w.Pod(space);w.Pod(charge);AtomicWrite(p/"space.gvr",Seal(w.data));
    };
    auto analysisReport=[&]{
        if(const auto* trace=session->Analysis()){
            const auto history=p/"analysis-history";if(!fs::exists(history))fs::create_directory(history);SafeDirectory(history);
            const auto name=std::to_string(trace->Boot());const auto folder=history/name;
            if(!fs::exists(folder)){size_t count=0;for(const auto& e:fs::directory_iterator(history)){(void)e;++count;}Require(count<64,"Analysis history quota reached (64 boots)");fs::create_directory(folder);}
            SafeDirectory(folder);const auto json=trace->Json(),html=trace->Html();
            AtomicText(folder/"analysis.json",json);AtomicText(folder/"analysis.html",html);
            AtomicText(p/"analysis.json",json);AtomicText(p/"analysis.html",html);
        }
    };
    auto save = [&] {
        analysisReport();
        auto point = session->Capture();
        AtomicWrite(p / "current.gvs", point.Encode());
        status.boot = point.BootId();
        status.virtualNs = Elapsed(point);
        status.generation = Token();
        status.active = active.filename().string();
        status.recoveredDisk = false;
        spaceProof();return point;
    };
    auto disk = [&] {
        if (c.persistent && (Options(c.contract).storage||c.contract=="resources"))
            AtomicWrite(c.contract=="resources"?c.diskFolder/"disk.gvs":p/"disk.gvo", session->DiskSnapshot());
        spaceProof();
    };
    try {
        resourceReservation=std::make_unique<resources::Reservation>(c.ramMiB,c.requestedVcpus,c.hostReserveMiB,c.contract=="resources"?c.diskGrowthBytes:0);
        bool running = mode != "hold" && !mode.starts_with("load:");
        if (mode == "resume" || mode.starts_with("load:")) {
            SessionSnapshot point;
            if (mode == "resume")
                point = Snapshot(p / "current.gvs",c.ramMiB,c.contract=="resources"?c.diskGrowthBytes:0);
            else {
                Name(mode.substr(5));
                point = Snapshot(p / "snapshots" / (mode.substr(5) + ".gvs"),c.ramMiB,c.contract=="resources"?c.diskGrowthBytes:0);
            }
            replace(&point);
        } else {
            Require(mode == "boot" || mode == "hold", "Worker startup mode rejected");
            replace(nullptr);
            if (mode == "hold") {
                const auto point = session->Capture();
                replace(&point);
            }
        }
        save();
        status.resident = true;
        status.state = running ? VmState::Running : VmState::Paused;
        status.error.clear();
        status.stamp = Now();
        Publish(id, status);
        started = true;
        bool done = false;
        uint64_t activeMs=0, activeNs=0, checkpoints=0, segments=0;
        const auto initialGuestNs=status.virtualNs;
        const auto workerStart=std::chrono::steady_clock::now();
        auto audit=[&](const char* reason){
            std::ostringstream text;
            text << "{\"schema\":12,\"phase\":\"12D\",\"timeMode\":\"real\",\"offlineTime\":\"frozen\",\"durationMs\":" << c.durationMs
                 << ",\"checkpointMs\":" << c.checkpointMs << ",\"activeMs\":" << activeMs << ",\"segments\":" << segments
                 << ",\"wallMs\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-workerStart).count() << ",\"checkpoints\":" << checkpoints << ",\"initialGuestNs\":" << initialGuestNs << ",\"guestNs\":" << status.virtualNs
                 << ",\"bootId\":" << status.boot << ",\"reason\":" << Quote(reason) << "}\n";
            AtomicText(p/"session.json",text.str());
        };
        while (!done) {
            if (running) {
                std::atomic<bool> ended{false};
                std::thread commands([&] {
                    while (!ended) {
                        std::error_code ec;
                        if (fs::exists(p / "request.gvr", ec))
                            session->Pause();
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                });
                BootResult result;
                try {
                    const auto beforeNs=session->ElapsedNs();
                    const auto slice=c.durationMs?std::min<uint64_t>(c.checkpointMs,c.durationMs>activeMs?c.durationMs-activeMs:1):c.timeoutMs;
                    result = session->Run(std::chrono::milliseconds(slice),c.durationMs!=0);
                    activeNs+=session->ElapsedNs()-beforeNs;activeMs=activeNs/1000000;
                    ++segments;
                } catch (...) {
                    ended = true;
                    commands.join();
                    throw;
                }
                ended = true;
                commands.join();
                if (!result.paused) {
                    disk(); status.virtualNs=session->ElapsedNs();
                    WriteBootReport(p / "last-boot.json", {result});
                    analysisReport();
                    if(result.driverMode&&result.driverReportValid){Writer proof;proof.Pod(installedBundleHash);proof.Pod(result.driver);AtomicWrite(p/"installed-proof.gds",Seal(proof.data));}
                    status.resident = false;
                    status.state = result.success ? VmState::Stopped : VmState::Fault;
                    status.error = result.error;
                    status.stamp = Now();
                    if(c.durationMs) audit(result.success?"completed":"fault");
                    Publish(id, status);
                    done = true;
                    break;
                }
                save(); ++checkpoints;
                if(c.durationMs) {
                    status.stamp=Now();Publish(id,status); audit("checkpoint");
                    if(activeMs>=c.durationMs && !Ordinary(p/"request.gvr")) {
                        running=false; done=true; status.state=VmState::Paused; status.resident=false;
                        status.stamp=Now(); status.error.clear(); audit("duration"); Publish(id,status); break;
                    }
                }
            }
            if (Ordinary(p / "request.gvr")) {
                auto bytes = Open(ReadFile(p / "request.gvr", 8192));
                Reader request(bytes);
                const auto token = request.Text(128), operation = request.Text(32),
                           argument = request.Text(32);
                request.End();
                std::string error;
                try {
                    if (operation == "suspend") {
                        save(); ++checkpoints; running=false; done=true;
                        if(c.durationMs) audit("suspend");
                    } else if (operation == "pause") {
                        Require(running, "VM is already paused");
                        running = false;
                    } else if (operation == "resume") {
                        Require(!running, "VM is already running");
                        running = true;
                        if(c.durationMs && activeMs>=c.durationMs) {activeMs=0;activeNs=0;}
                    } else if (operation == "snapshot") {
                        Name(argument);
                        size_t n = 0;
                        for (const auto &e : fs::directory_iterator(p / "snapshots"))
                            if (e.path().extension() == ".gvs")
                                ++n;
                        Require(n < 8 || Ordinary(p / "snapshots" / (argument + ".gvs")),
                                "Snapshot limit reached");
                        auto point = session->Capture();
                        AtomicWrite(p / "snapshots" / (argument + ".gvs"), point.Encode());
                    } else if (operation == "restore") {
                        Name(argument);
                        auto point = Snapshot(p / "snapshots" / (argument + ".gvs"),c.ramMiB,c.contract=="resources"?c.diskGrowthBytes:0);
                        auto [candidate, work] = build(&point);
                        AtomicWrite(p / "current.gvs", point.Encode());
                        session = std::move(candidate);
                        if (!active.empty())
                            RemoveWork(active,c);
                        active = work;
                        running = false; activeMs=0;activeNs=0;
                        status.boot = point.BootId();
                        status.virtualNs = Elapsed(point);
                        status.generation = Token();
                        status.active = active.filename().string();
                        status.recoveredDisk = false;
                        spaceProof();
                    } else if (operation == "checkpoint") {
                        save();
                    } else if (operation == "restart") {
                        disk();
                        replace(nullptr);
                        save(); activeMs=0;activeNs=0;
                        running = true;
                    } else if (operation == "shutdown") {
                        disk();
                        running = false;
                        done = true;
                    } else
                        throw std::runtime_error("Unknown VM operation");
                    status.resident = !done;
                    status.state = done && operation!="suspend" ? VmState::Stopped : (running ? VmState::Running : VmState::Paused);
                    status.stamp = Now();
                    status.error.clear();
                    Publish(id, status);
                } catch (const std::exception &e) {
                    error = e.what();
                }
                Writer reply;
                reply.Text(token);
                reply.Text(error);
                fs::remove(p / "request.gvr");
                AtomicWrite(p / "reply.gvr", Seal(reply.data));
            }
            if (!running && !done)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        session.reset();
        if (!active.empty())
            RemoveWork(active,c);
        return status.state == VmState::Fault ? 3 : 0;
    } catch (const std::exception &e) {
        if (!started && Ordinary(p / "launch.gvt"))
            status = LoadStatus(p / "launch.gvt");
        else
            status.state = VmState::Fault;
        status.resident = false;
        status.pid = 0;
        status.error = e.what();
        status.stamp = Now();
        try {
            Publish(id, status);
        } catch (...) {
        }
        session.reset();
        if (!active.empty() && !c.persistent)
            RemoveWork(active,c);
        throw;
    }
}

void VmManager::ConfigureAnalysis(const std::string& id,const std::string& domain,const std::string& decision,uint32_t limit,uint32_t scenario){
    auto c=Config(id);const auto status=ReadStatus(id);const auto p=Path(id);
    Require(!Resident(p,status.resident)&&(status.state==VmState::Stopped||status.state==VmState::Fault),"Analysis policy changes require a stopped VM");
    const auto image=KernelImage::Parse(ReadFile(p/"image.gk",KernelImage::MaxFileBytes));Require(image.SupportsTelemetry(),"Imported kernel lacks telemetry/policy support");
    const auto bit=guardian::telemetry::Bit(analysis::ParseDomain(domain));
    Require(decision=="allow"||decision=="deny"||decision=="simulate","Unknown analysis decision");
    c.analysisPolicy.denyMask&=~bit;c.analysisPolicy.simulateMask&=~bit;
    if(decision=="deny")c.analysisPolicy.denyMask|=bit;else if(decision=="simulate")c.analysisPolicy.simulateMask|=bit;
    if(limit)c.analysisPolicy.eventLimit=limit;
    if(scenario!=UINT32_MAX){Require(image.SupportsAnalysisFixture()&&scenario<=1,"Scenario requires the owned analysis fixture");c.analysisScenario=scenario;}
    Require(guardian::telemetry::ValidPolicy(c.analysisPolicy),"Analysis event limit rejected");SaveConfig(p/"config.gvc",c);
    if(fs::exists(p/"current.gvs"))fs::remove(p/"current.gvs");
}
std::string VmManager::AnalysisPolicy(const std::string& id){
    const auto c=Config(id);return "{\"version\":1,\"denyMask\":"+std::to_string(c.analysisPolicy.denyMask)+",\"simulateMask\":"+std::to_string(c.analysisPolicy.simulateMask)+",\"eventLimit\":"+std::to_string(c.analysisPolicy.eventLimit)+",\"scenario\":"+std::to_string(c.analysisScenario)+",\"scope\":\"guest-mutations\"}";
}
std::string VmManager::AnalysisReport(const std::string& id,bool html){
    const auto p=Path(id)/(html?"analysis.html":"analysis.json");Require(Ordinary(p),"No analysis report has been produced for this VM");const auto bytes=ReadFile(p,16ull<<20);return {bytes.begin(),bytes.end()};
}
} // namespace av::microvm::state
