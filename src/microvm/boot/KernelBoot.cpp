#include "KernelBoot.h"
#include "guardian/SystemClock.h"
#if !defined(__linux__)
#include <psapi.h>
#endif
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace av::microvm::boot {
namespace {
void Require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
std::string Narrow(const std::wstring& s) {
#if defined(__linux__)
    return std::string(s.begin(),s.end());
#else
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "WHP error (text conversion failed)";
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
#endif
}
std::string Quoted(const std::string& value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 32 || ch > 126) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += static_cast<char>(ch);
    }
    return out + '"';
}
}
bool WhpAvailable() {
#if defined(__linux__)
    return MicroVm::Available();
#else
    WHV_CAPABILITY capability{}; UINT32 written = 0;
    return SUCCEEDED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &capability, sizeof(capability), &written)) &&
        written >= sizeof(BOOL) && capability.HypervisorPresent;
#endif
}
void KernelBootSession::Prepare(const KernelImage& image, uint64_t ramBytes, uint64_t bootId, CpuOptions cpu,const StorageOptions& storage,const net::Options& networking,const gui::Options& graphics,const hardware::Profile* hardware,const hardware::DriverOptions& driver,const storage::SparseOptions* scalable,const guardian::environment::Info* environment,const guardian::telemetry::Policy* analysisPolicy,uint32_t analysisScenario,std::span<const uint8_t> externalGui,const guardian::nt::Info* compatibility,std::span<const uint8_t> externalVolume,std::span<const uint8_t> securityState,const desktop_session::Options& desktop,const internet_session::Policy* internet) {
    Require(!desktop.integrated||image.SupportsWindowBridge(),"Integrated windows require their kernel");
    Require(desktop.integrated||(!desktop.windowPoll&&!desktop.windowDisplay),"Window callbacks require integrated mode");
    Require(!internet||image.SupportsInternet(),"Internet policy requires its kernel");
    Require(image.SupportsDesktop()||(!desktop.telemetryCapacity&&desktop.output.empty()&&desktop.script.empty()&&!desktop.interactive&&!desktop.poll&&!desktop.display),"Desktop options require their kernel");
    constexpr uint64_t securityGpa=0x5000000,securityBytes=65536;
    Require(securityState.empty()||(image.SupportsSecurity()&&securityState.size()==securityBytes),"Security state requires its kernel and exactly 64 KiB");
    using namespace guardian::abi;
    Require(!attempted_, "A boot session cannot be reused"); attempted_ = true;
    externalPeKernel_=image.SupportsExternalPe();
    Require(externalPeKernel_?!externalVolume.empty():externalVolume.empty(),"External volume requires its kernel contract");
    std::span<const uint8_t> application=externalGui;
    if(externalPeKernel_){
        Require(compatibility&&cpu.enabled&&cpu.processes&&cpu.graphics&&!cpu.io&&!cpu.storage&&!cpu.network&&!cpu.driver&&!cpu.resources&&externalGui.empty()&&ramBytes>=(128ull<<20),"External volume options rejected");
        Require(externalVolume.size()<=guardian::pevolume::MaxBytes&&guardian::pevolume::Valid(externalVolume.data(),static_cast<uint32_t>(externalVolume.size())),"External volume rejected");
        const auto& header=*reinterpret_cast<const guardian::pevolume::Header*>(externalVolume.data());Require(header.version==1||image.SupportsComponents(),"Extended volume requires the component kernel");for(uint32_t i=0;i<header.files;++i)Require(guardian::pevolume::FileAt(header,i).kind!=guardian::pevolume::Kind::Data||image.SupportsDesktop(),"Data assets require the desktop kernel");
        Require(header.profile==compatibility->profile,"External volume NT profile mismatch");
        const auto digest=compatibility::Sha256(externalVolume);Require(std::memcmp(digest.data(),compatibility->runtimeSha,32)==0,"External volume SHA-256 mismatch");
        const auto& root=guardian::pevolume::FileAt(header,header.root);application=externalVolume.subspan(root.offset,root.bytes);
    }
    if(compatibility){
        Require(cpu.enabled&&!cpu.resources&&!cpu.driver,"NT binding requires the CPU/process boot contract");
        compatibility::Verify(*compatibility,image.FileBytes(),application);
        ntInfo_=*compatibility;ntInfo_.boot=bootId;ntEnabled_=true;
        if(environment)Require(guardian::nt::Matches(guardian::nt::Find(ntInfo_.profile),environment->ntMajor,environment->ntMinor,environment->ntBuild),"NT/environment profile mismatch");
    }
    clockApiContract_=image.SupportsSystemClock();
    externalGuiKernel_=image.SupportsExternalGui();
    Require(externalGuiKernel_?!externalGui.empty():externalGui.empty(),"External GUI payload requires its explicit kernel contract");
    Require(!externalGuiKernel_||(externalGui.size()>=64&&externalGui.size()<=guardian::externalgui::MaxFile&&cpu.enabled&&cpu.processes&&cpu.graphics&&!cpu.io&&!cpu.storage&&!cpu.network&&!cpu.driver&&!cpu.resources),"External GUI contract/options rejected");
    const guardian::telemetry::Policy policy=analysisPolicy?*analysisPolicy:guardian::telemetry::Policy{};
    Require(guardian::telemetry::ValidPolicy(policy)&&analysisScenario<=1,"Analysis policy/scenario rejected");
    telemetryEnabled_=image.SupportsTelemetry();analysisKernel_=image.SupportsAnalysisFixture();
    Require(!analysisKernel_||(cpu.enabled&&cpu.processes&&cpu.io&&cpu.storage),"Owned analysis image requires the storage/process contract");
    Require(telemetryEnabled_||(!policy.denyMask&&!policy.simulateMask&&policy.eventLimit==guardian::telemetry::DefaultEvents&&!analysisScenario),"Imported kernel does not implement telemetry/policy");
    Require(!analysisScenario||image.SupportsAnalysisFixture(),"Scenario selection requires the owned analysis fixture");
    if(telemetryEnabled_){telemetryInfo_.boot=bootId;telemetryInfo_.scenario=analysisScenario;telemetry_.Configure(bootId,policy);if(desktop.telemetryCapacity)telemetry_.RecordCapacity(desktop.telemetryCapacity);}
    Require(!image.SupportsEnvironment()||environment,"Synthetic kernel requires an explicit environment profile");
    if(environment){Require(guardian::environment::Valid(*environment)&&!environment->catalogHash&&cpu.storage&&image.SupportsEnvironment()&&hardware,"Environment requires its supported kernel, volume and NT hardware profile");environmentInfo_=*environment;environmentEnabled_=true;}
    if(cpu.resources){
        Require(scalable&&!cpu.enabled&&!cpu.storage&&!cpu.driver&&!cpu.network&&!cpu.graphics,"Resource image requires its distinct device contract");
        resourceSession_=std::make_unique<ResourceSession>();resourceSession_->Prepare(image,ramBytes,bootId,cpu.vcpus,*scalable,storage.volumeQuotaBytes);return;
    }
    Require(!scalable&&cpu.vcpus==1,"This kernel image supports exactly one CPU");
    cpuOptions_ = cpu;imageHash_=state::Hash(image.FileBytes());
    if(hardware){
        const std::string contract=cpu.driver?"hardware":cpu.graphics?"graphics":cpu.storage?"storage":"basic";
        hardwareInfo_=av::microvm::hardware::Inventory(*hardware,ramBytes,contract);
        if(ntEnabled_)Require(guardian::nt::Matches(guardian::nt::Find(ntInfo_.profile),hardwareInfo_.ntMajor,hardwareInfo_.ntMinor,hardwareInfo_.ntBuild),"NT/hardware profile mismatch");
        if(environmentEnabled_)Require(hardwareInfo_.ntMajor==environmentInfo_.ntMajor&&hardwareInfo_.ntMinor==environmentInfo_.ntMinor&&hardwareInfo_.ntBuild==environmentInfo_.ntBuild,"Synthetic profile and NT hardware inventory disagree");
        vm_.ConfigureCpuIdentity(hardwareInfo_.devices[0].effectiveModel);
    }
    Require(!cpu.memory || cpu.enabled, "Memory mode requires CPU mode");
    Require(!cpu.driver||(!cpu.enabled&&hardware&&!cpu.io&&!cpu.storage&&!cpu.network&&!cpu.graphics),"Driver image requires its distinct hardware contract");
    Require(cpu.driver||driver.image.empty(),"Driver package requires the hardware contract");
    Require(!cpu.threads || (cpu.enabled && !cpu.memory), "Thread mode requires CPU and a distinct image contract");
    Require(!cpu.processes || (cpu.enabled && !cpu.memory && !cpu.threads), "Process mode requires CPU and a distinct contract");
    Require(!cpu.io || (cpu.enabled && cpu.processes && !cpu.memory && !cpu.threads), "I/O requires the process image contract");
    Require(!cpu.storage || (cpu.io && cpu.processes && !storage.base.empty() && !storage.overlay.empty()), "Storage requires I/O and explicit base/overlay paths");
    Require(cpu.graphics || (graphics.output.empty() && graphics.input.empty() && graphics.script.empty() && !graphics.continuous && !graphics.interactive && !graphics.poll && !graphics.display), "Graphics options require a graphics image");
    Require(!cpu.graphics || (cpu.enabled && !cpu.memory && !cpu.threads && (!cpu.processes||externalGuiKernel_||externalPeKernel_) && !cpu.network), "Graphics requires a distinct CPU profile");
    Require(!cpu.network || (cpu.enabled && !cpu.memory && !cpu.threads && (!cpu.processes||analysisKernel_)), "Network image contract requires the CPU profile");
    Require(!analysisKernel_||(cpu.network&&(networking.policy.mode==guardian::net::Mode::Offline||networking.policy.mode==guardian::net::Mode::Simulated)),"Owned analysis echo supports offline/simulated networking only");
    Require(cpu.network || networking.policy.mode==guardian::net::Mode::Offline,"Network policy requires a network-capable image contract");
    BootInfo b{BootMagic, Major, Minor, sizeof(BootInfo), X64, 0, ramBytes, image.Base(), image.Size(), image.Entry(),
        StackGpa, StackBytes, MapGpa, 6, sizeof(MemoryRegion), DevicesGpa, 2, sizeof(Device),
        ChannelGpa, MaxMessageBytes, 1, bootId, {0, 0, 0, 0}};
    // Validate scalar bounds before subtraction, allocation or table access.
    Require(ValidateBoot(b, BootGpa, ramBytes) == Result::Ok && image.Size() <= KernelImage::MaxImageBytes,
        "Boot parameters rejected before VM creation");
    const auto imageEnd = image.Base() + image.Size();
    std::vector<MemoryRegion> regions{
        {0, BootGpa, MemoryKind::Reserved, 0, 0},
        {BootGpa, image.Base() - BootGpa, MemoryKind::BootData, 0, 0},
        {image.Base(), image.Size(), MemoryKind::KernelImage, 0, 0},
        {imageEnd, StackGpa - imageEnd, MemoryKind::Available, 0, 0},
        {StackGpa, StackBytes, MemoryKind::KernelStack, 0, 0},
        {StackGpa + StackBytes, ramBytes - StackGpa - StackBytes, MemoryKind::Available, 0, 0}};
    if (cpu.enabled) {
        regions.pop_back();
        regions.push_back({StackGpa + StackBytes, guardian::cpu::UserCode - StackGpa - StackBytes, MemoryKind::Available, 0, 0});
        regions.push_back({guardian::cpu::UserCode, 0x200000, MemoryKind::Reserved, 0, 0});
        regions.push_back({0x800000, ramBytes - 0x800000, MemoryKind::Available, 0, 0});
        b.memoryMapCount = static_cast<uint32_t>(regions.size());
    }
    if(externalGuiKernel_){
        std::vector<MemoryRegion> split;
        constexpr auto begin=guardian::externalgui::SourceGpa,end=begin+guardian::externalgui::MaxFile;
        for(const auto& region:regions){
            if(region.kind==MemoryKind::Available&&region.gpa<=begin&&region.gpa+region.bytes>=end){
                if(region.gpa<begin)split.push_back({region.gpa,begin-region.gpa,MemoryKind::Available,0,0});
                split.push_back({begin,end-begin,MemoryKind::BootData,0,0});
                if(region.gpa+region.bytes>end)split.push_back({end,region.gpa+region.bytes-end,MemoryKind::Available,0,0});
            }else split.push_back(region);
        }
        regions=std::move(split);b.memoryMapCount=static_cast<uint32_t>(regions.size());
        Require(Covered(regions.data(),b.memoryMapCount,begin,end-begin,MemoryKind::BootData),"External PE source reservation rejected");
    }
    if(externalPeKernel_){
        std::vector<MemoryRegion> split;const auto begin=guardian::pevolume::SourceGpa;
        const uint64_t bytes=(externalVolume.size()+4095)&~4095ull,end=begin+bytes;
        for(const auto& region:regions){if(region.kind==MemoryKind::Available&&region.gpa<=begin&&region.gpa+region.bytes>=end){
            if(region.gpa<begin)split.push_back({region.gpa,begin-region.gpa,MemoryKind::Available,0,0});
            split.push_back({begin,bytes,MemoryKind::BootData,0,0});
            if(region.gpa+region.bytes>end)split.push_back({end,region.gpa+region.bytes-end,MemoryKind::Available,0,0});
        }else split.push_back(region);}
        regions=std::move(split);b.memoryMapCount=static_cast<uint32_t>(regions.size());
        Require(Covered(regions.data(),b.memoryMapCount,begin,bytes,MemoryKind::BootData),"External volume reservation rejected");
    }
    if(image.SupportsSecurity()){
        std::vector<MemoryRegion> split;const auto begin=securityGpa,end=begin+securityBytes;
        for(const auto& region:regions){if(region.kind==MemoryKind::Available&&region.gpa<=begin&&region.gpa+region.bytes>=end){
            if(region.gpa<begin)split.push_back({region.gpa,begin-region.gpa,MemoryKind::Available,0,0});
            split.push_back({begin,securityBytes,MemoryKind::BootData,0,0});
            if(region.gpa+region.bytes>end)split.push_back({end,region.gpa+region.bytes-end,MemoryKind::Available,0,0});
        }else split.push_back(region);}
        regions=std::move(split);b.memoryMapCount=static_cast<uint32_t>(regions.size());
        Require(Covered(regions.data(),b.memoryMapCount,begin,securityBytes,MemoryKind::BootData),"Security state reservation rejected");
    }
    if(image.SupportsDesktop()){
        constexpr uint64_t ranges[][2]={{guardian::desktop::PixelsGpa,guardian::desktop::PixelCount*4},{guardian::desktop::StateGpa,guardian::desktop::StateBytes}};
        for(const auto& range:ranges){std::vector<MemoryRegion> split;const auto begin=range[0],end=begin+range[1];for(const auto& region:regions){if(region.kind==MemoryKind::Available&&region.gpa<=begin&&region.gpa+region.bytes>=end){if(region.gpa<begin)split.push_back({region.gpa,begin-region.gpa,MemoryKind::Available,0,0});split.push_back({begin,range[1],MemoryKind::BootData,0,0});if(region.gpa+region.bytes>end)split.push_back({end,region.gpa+region.bytes-end,MemoryKind::Available,0,0});}else split.push_back(region);}regions=std::move(split);b.memoryMapCount=static_cast<uint32_t>(regions.size());Require(Covered(regions.data(),b.memoryMapCount,begin,range[1],MemoryKind::BootData),"Desktop reservation rejected");}
    }
    if(cpu.driver){
        std::vector<MemoryRegion> split;
        for(const auto& region:regions){
            if(region.kind==MemoryKind::Available&&region.gpa<=guardian::driver::SourceGpa&&region.bytes>=guardian::driver::ApiGpa+guardian::driver::MaxFile-region.gpa){
                if(region.gpa<guardian::driver::SourceGpa)split.push_back({region.gpa,guardian::driver::SourceGpa-region.gpa,MemoryKind::Available,0,0});
                split.push_back({guardian::driver::SourceGpa,2*guardian::driver::MaxFile,MemoryKind::BootData,0,0});
                const uint64_t end=guardian::driver::ApiGpa+guardian::driver::MaxFile;if(region.gpa+region.bytes>end)split.push_back({end,region.gpa+region.bytes-end,MemoryKind::Available,0,0});
            }else split.push_back(region);
        }
        regions=std::move(split);b.memoryMapCount=static_cast<uint32_t>(regions.size());
    }
    const Device devices[]{
        {DeviceKind::DebugConsole, 1, DebugPort, NoInterrupt, 0, 0},
        {DeviceKind::ControlChannel, 1, ControlPort, NoInterrupt, ChannelGpa, MaxMessageBytes}};
    Require(ValidateLayout(b, BootGpa, ramBytes, regions.data()) == Result::Ok && ValidateDevices(b, devices) == Result::Ok,
        "Boot layout rejected before VM creation");
    regions_ = regions;
    if(cpu.storage){storageQuotaContract_=image.SupportsStorageQuota();Require(storageQuotaContract_||storage.volumeQuotaBytes==guardian::storage::VolumeCapacity,"This imported kernel does not support configurable volume quotas");storageQuota_.allocationLimit=storage.volumeQuotaBytes;Require(guardian::storage::ValidQuota(storageQuota_),"Invalid volume allocation quota");storageBackend_=std::make_unique<PersistentBlockStore>(storage);}
    start_ = std::chrono::steady_clock::now(); ramBytes_ = ramBytes; bootId_ = bootId; channel_ = BootChannel(bootId);
#if defined(__linux__)
    exceptionExitBitmap_=0;
#else
    WHV_CAPABILITY capability{}; UINT32 written = 0;
    Require(SUCCEEDED(WHvGetCapability(WHvCapabilityCodeExceptionExitBitmap, &capability, sizeof(capability), &written)) &&
        written >= sizeof(uint64_t), "Cannot query WHP exception intercept support");
    exceptionExitBitmap_ = cpu.enabled ? 0 : capability.ExceptionExitBitmap & 0xFFFFFFFFull;
    constexpr uint64_t required = (1ull << 6) | (1ull << 13) | (1ull << 14);
    Require(cpu.enabled || (exceptionExitBitmap_ & required) == required, "WHP lacks required #UD/#GP/#PF intercepts");
#endif
    if (!vm_.Create(static_cast<size_t>(ramBytes), exceptionExitBitmap_)) throw std::runtime_error(Narrow(vm_.Error()));
    auto* ram = static_cast<uint8_t*>(vm_.RamData()); image.Map({ram, vm_.RamSize()});
    if(image.SupportsSecurity()){std::memset(ram+securityGpa,0,securityBytes);if(!securityState.empty())std::memcpy(ram+securityGpa,securityState.data(),securityState.size());Require(vm_.SetMemoryAccess(securityGpa,securityBytes,3),"Security state permissions rejected");}
    if(hardwareInfo_.magic)std::memcpy(ram+guardian::hardware::InfoGpa,&hardwareInfo_,sizeof(hardwareInfo_));
    std::memcpy(ram + BootGpa, &b, sizeof(b)); std::memcpy(ram + MapGpa, regions.data(), regions.size() * sizeof(MemoryRegion));
    std::memcpy(ram + DevicesGpa, devices, sizeof(devices));
    ram[ReturnGpa] = 0x0F; ram[ReturnGpa + 1] = 0x0B; // Returning from the entry point is #UD.
    const uint64_t rsp = StackGpa + StackBytes - 0x28;
    std::memcpy(ram + rsp, &ReturnGpa, sizeof(ReturnGpa));
    Require(vm_.SetupLongMode(image.Entry(), rsp), "Long-mode initialization failed");
#if !defined(__linux__)
    const WHV_REGISTER_NAME names[]{WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterR8, WHvX64RegisterR9};
    WHV_REGISTER_VALUE values[4]{}; values[0].Reg64 = BootGpa;
#endif
    if (cpu.enabled) {
        namespace gc = guardian::cpu;
        const gc::Boot c{gc::BootMagic, gc::Version, sizeof(gc::Boot), bootId, gc::ClockGpa, gc::ReportGpa, gc::PeriodNs,
            gc::TimerPort, gc::TimerVector, {0, 0, 0, 0, 0}};
        const auto unix100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() / 100;
        Require(unix100ns >= 0, "Host UTC precedes the supported epoch");
        utcEpoch100ns_ = guardian::time::UnixEpoch100ns + static_cast<uint64_t>(unix100ns);
        const gc::Clock clock{gc::ClockMagic, gc::Version, sizeof(gc::Clock), 0, bootId, 0, 0,
                              utcEpoch100ns_, guardian::time::Frequency};
        Require(gc::ValidBoot(c, bootId), "CPU boot extension rejected");
        std::memcpy(ram + gc::BootGpa, &c, sizeof(c)); std::memcpy(ram + gc::ClockGpa, &clock, sizeof(clock));
#if !defined(__linux__)
        values[1].Reg64 = gc::BootGpa;
#endif
    }
#if defined(__linux__)
    Require(vm_.BootArguments(BootGpa,cpu.enabled?guardian::cpu::BootGpa:0),"KVM boot arguments failed");
#else
    Require(vm_.SetRegisters(names, 4, values), "Boot registers initialization failed");
#endif
    Require(vm_.SetMemoryAccess(0, ramBytes, 3) && vm_.SetMemoryAccess(image.Base(), image.Size(), 1), "Initial guest permissions failed");
    for (const auto& section : image.Sections())
        Require(vm_.SetMemoryAccess(image.Base() + section.rva, section.mappedBytes, section.access), "Kernel section permissions failed");
    Require(vm_.SetMemoryAccess(BootGpa, 3 * PageBytes, 1) && vm_.SetMemoryAccess(ReturnGpa, PageBytes, 5) &&
        vm_.SetMemoryAccess(StackGpa, PageBytes, 0), "Boot/stack protections failed");
    if(externalGuiKernel_){
        namespace eg=guardian::externalgui;
        const eg::Info info{eg::Magic,bootId,eg::Version,sizeof(eg::Info),static_cast<uint32_t>(externalGui.size()),0,eg::Hash(externalGui.data(),static_cast<uint32_t>(externalGui.size()))};
        std::memcpy(ram+eg::InfoGpa,&info,sizeof(info));
        std::memcpy(ram+eg::SourceGpa,externalGui.data(),externalGui.size());
        Require(vm_.SetMemoryAccess(eg::InfoGpa,PageBytes,1)&&vm_.SetMemoryAccess(eg::SourceGpa,eg::MaxFile,1),"External PE source protections rejected");
    }
    if(externalPeKernel_){
        // Console programs can finish without presenting a frame. Start with
        // the same background as the host display before comparing reports.
        std::fill_n(reinterpret_cast<uint32_t*>(ram+guardian::graphics::PixelsGpa),guardian::graphics::PixelCount,0xFF172536u);
        guardian::pevolume::Info info;info.boot=bootId;info.volumeBytes=static_cast<uint32_t>(externalVolume.size());info.hash=guardian::pevolume::Hash(externalVolume.data(),info.volumeBytes,true);
        std::memcpy(ram+guardian::pevolume::InfoGpa,&info,sizeof(info));std::memcpy(ram+guardian::pevolume::SourceGpa,externalVolume.data(),externalVolume.size());
        Require(vm_.SetMemoryAccess(guardian::pevolume::InfoGpa,PageBytes,1)&&vm_.SetMemoryAccess(guardian::pevolume::SourceGpa,(externalVolume.size()+4095)&~4095ull,1),"External volume permissions rejected");
    }
    if(ntEnabled_){std::memcpy(ram+guardian::nt::InfoGpa,&ntInfo_,sizeof(ntInfo_));Require(vm_.SetMemoryAccess(guardian::nt::InfoGpa,PageBytes,1),"NT binding page protection failed");}
    if(hardwareInfo_.magic)Require(vm_.SetMemoryAccess(guardian::hardware::InfoGpa,PageBytes,1),"Hardware inventory protection failed");
    if(cpu.driver){
        driverBackend_=std::make_unique<hardware::DriverBackend>(vm_,bootId,driver);
        vm_.MapIoTrapDetailed(guardian::driver::Port,[this](MicroVm&,const MicroVm::IoAccess& a){driverBackend_->Control(a);});
        vm_.MapIoTrapDetailed(guardian::driver::DevicePort,[this](MicroVm&,const MicroVm::IoAccess& a){driverBackend_->Exchange(a);
            if(a.write&&pauseAtDriverWrite_&&driverBackend_->Writes()==pauseAtDriverWrite_){pauseAtDriverWrite_=0;vm_.RequestPause();}});
    }
    if (cpu.enabled) {
        Require(vm_.SetMemoryAccess(guardian::cpu::BootGpa, 2 * PageBytes, 1), "CPU boot/clock protections failed");
        vm_.MapIoTrapDetailed(guardian::cpu::TimerPort, [this](MicroVm&, const MicroVm::IoAccess& a) { CpuControl(a); });
        vm_.OnServiceWake([this](MicroVm&) { ClockService(); });
    }
    if (cpu.memory || cpu.threads || cpu.processes) vm_.MapIoTrapDetailed(guardian::memory::Port, [this](MicroVm&, const MicroVm::IoAccess& a) { MemoryControl(a); });
    if (cpu.threads) vm_.MapIoTrapDetailed(guardian::threads::Port, [this](MicroVm&, const MicroVm::IoAccess& a) { ThreadControl(a); });
    if (cpu.processes) vm_.MapIoTrapDetailed(guardian::processes::Port, [this](MicroVm&, const MicroVm::IoAccess& a) { ProcessControl(a); });
    if(cpu.io){
        ioBackend_=std::make_unique<guardian::io::Backend>(bootId);
        if(storageBackend_)ioBackend_->Storage(storageBackend_.get(),
            [](void* p,uint64_t off,uint8_t* data) noexcept {return static_cast<PersistentBlockStore*>(p)->Read(off,data);},
            [](void* p,uint64_t off,const uint8_t* data) noexcept {return static_cast<PersistentBlockStore*>(p)->Write(off,data);},
            [](void* p) noexcept {return static_cast<PersistentBlockStore*>(p)->Flush();},
            [](void* p) noexcept {return static_cast<PersistentBlockStore*>(p)->LastStatus();});
        if(storageBackend_)ioBackend_->QueryBacking([](void* p,guardian::storage::BackingSpace& out) noexcept {out=static_cast<PersistentBlockStore*>(p)->Space();return true;});
        std::memcpy(ram+guardian::io::BusGpa,&ioBackend_->Enumeration(),sizeof(guardian::io::Bus));
        Require(vm_.SetMemoryAccess(guardian::io::BusGpa,PageBytes,1),"I/O bus protection failed");
        vm_.MapIoTrapDetailed(guardian::io::Port,[this](MicroVm&,const MicroVm::IoAccess& a){IoControl(a);});
    }
    if(telemetryEnabled_){
        std::memcpy(ram+guardian::telemetry::InfoGpa,&telemetryInfo_,sizeof(telemetryInfo_));
        Require(vm_.SetMemoryAccess(guardian::telemetry::InfoGpa,PageBytes,1),"Telemetry policy page protection failed");
        vm_.MapIoTrapDetailed(guardian::telemetry::Port,[this](MicroVm&,const MicroVm::IoAccess& a){TelemetryControl(a);});
    }
    if(environmentEnabled_){std::memcpy(ram+guardian::environment::InfoGpa,&environmentInfo_,sizeof(environmentInfo_));Require(vm_.SetMemoryAccess(guardian::environment::InfoGpa,PageBytes,1),"Environment profile page protection failed");}
    if(cpu.storage&&storageQuotaContract_){std::memcpy(ram+guardian::storage::QuotaGpa,&storageQuota_,sizeof(storageQuota_));Require(vm_.SetMemoryAccess(guardian::storage::QuotaGpa,PageBytes,1),"Volume quota page protection failed");}
    if(cpu.storage)vm_.MapIoTrapDetailed(guardian::storage::Port,[this](MicroVm&,const MicroVm::IoAccess& a){StorageControl(a);});
    if(image.SupportsDesktop()){
        namespace gd=guardian::desktop;desktopBackend_=std::make_unique<desktop_session::Backend>(bootId,desktop);const auto info=desktopBackend_->Enumeration();std::memcpy(ram+gd::InfoGpa,&info,sizeof(info));
        Require(vm_.SetMemoryAccess(gd::InfoGpa,PageBytes,1)&&vm_.SetMemoryAccess(gd::PixelsGpa,gd::PixelCount*4,3)&&vm_.SetMemoryAccess(gd::StateGpa,gd::StateBytes,3),"Desktop page permissions rejected");
        if(desktop.integrated){windowBridge_=std::make_unique<window_session::Bridge>(bootId,window_session::NewSession(),window_session::Callbacks{desktop.windowPoll,desktop.windowDisplay});const auto windowInfo=windowBridge_->Enumeration();std::memcpy(ram+gd::InfoGpa+guardian::presentation::InfoOffset,&windowInfo,sizeof(windowInfo));windowOutput_=desktop.output;}
        vm_.MapIoTrapDetailed(gd::Port,[this](MicroVm&,const MicroVm::IoAccess& access){namespace d=guardian::desktop;if(!ValidIo(access)||!desktopBackend_||cpuReportValid_){vm_.FailExecution(L"Desktop channel rejected");return;}auto* memory=static_cast<uint8_t*>(vm_.RamData());try{if(access.value==1){d::Packet packet;std::memcpy(&packet,memory+d::PacketGpa,sizeof(packet));const auto now=elapsedNs_+uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-runStart_).count());if(!desktopBackend_->Exchange(packet,reinterpret_cast<const uint32_t*>(memory+d::PixelsGpa),now)){vm_.FailExecution(L"Desktop packet rejected");return;}std::memcpy(memory+d::PacketGpa,&packet,sizeof(packet));}else if(access.value==2){d::Report report;std::memcpy(&report,memory+d::ReportGpa,sizeof(report));if(!desktopBackend_->Complete(report))vm_.FailExecution(L"Desktop report rejected");}else if(access.value==3&&windowBridge_){guardian::presentation::Packet packet;std::memcpy(&packet,memory+d::PacketGpa,sizeof(packet));const auto now=elapsedNs_+uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-runStart_).count());if(!windowBridge_->Exchange(packet,reinterpret_cast<const uint32_t*>(memory+d::PixelsGpa),now)){vm_.FailExecution(L"Window bridge packet rejected");return;}std::memcpy(memory+d::PacketGpa,&packet,sizeof(packet));}else vm_.FailExecution(L"Desktop operation rejected");}catch(const std::exception&){vm_.FailExecution(L"Desktop backend failure");}});
    }
    if(cpu.graphics){
        graphicsBackend_=std::make_unique<gui::Backend>(bootId,graphics);
        const auto info=graphicsBackend_->Enumeration();std::memcpy(ram+guardian::graphics::InfoGpa,&info,sizeof(info));
        Require(vm_.SetMemoryAccess(guardian::graphics::InfoGpa,PageBytes,1),"Graphics enumeration protection failed");
        vm_.MapIoTrapDetailed(guardian::graphics::Port,[this](MicroVm&,const MicroVm::IoAccess& a){GraphicsControl(a);});
    }
    if(image.SupportsInternet()){
        internetBackend_=std::make_unique<internet_session::Backend>(internet?*internet:internet_session::Policy{});
        internetReport_=desktop.output.empty()?std::filesystem::path{}:desktop.output/"internet.json";
        Require(vm_.SetMemoryAccess(guardian::internet::PacketGpa,4096,3),"Internet channel mapping failed");
        vm_.MapIoTrapDetailed(guardian::internet::Port,[this](MicroVm&,const MicroVm::IoAccess& access){
            if(!ValidIo(access)||access.value!=1||!internetBackend_){vm_.FailExecution(L"Internet channel rejected");return;}
            auto* data=static_cast<uint8_t*>(vm_.RamData());guardian::internet::Request request;
            std::memcpy(&request,data+guardian::internet::PacketGpa,sizeof(request));
            if(!internetBackend_->Exchange(request)){vm_.FailExecution(L"Internet request rejected");return;}
            std::memcpy(data+guardian::internet::PacketGpa,&request,sizeof(request));
        });
    }
    if(cpu.network){
        auto transport=networking.transport?networking.transport:(networking.policy.mode==guardian::net::Mode::Lab?net::LabTransport(networking):
                       (networking.policy.mode==guardian::net::Mode::Proxy?net::ProxyTransport(networking.policy):net::Transport{}));
        networkBackend_=std::make_unique<net::Backend>(bootId,networking.policy,std::move(transport),networking.capture);
        std::memcpy(ram+guardian::net::InfoGpa,&networkBackend_->Enumeration(),sizeof(guardian::net::Info));
        Require(vm_.SetMemoryAccess(guardian::net::InfoGpa,PageBytes,1),"Network enumeration protection failed");
        vm_.MapIoTrapDetailed(guardian::net::Port,[this](MicroVm&,const MicroVm::IoAccess& a){NetworkControl(a);});
    }
    vm_.StrictIo(true);
    vm_.MapIoTrapDetailed(DebugPort, [this](MicroVm&, const MicroVm::IoAccess& access) {
        if (!ValidIo(access)) return;
        const auto ch = static_cast<uint8_t>(access.value);
        if (console_.size() >= 4096 || (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch > 126) {
            vm_.FailExecution(L"Console text/budget rejected"); return;
        }
        console_ += static_cast<char>(ch);
    });
    vm_.MapIoTrapDetailed(ControlPort, [this](MicroVm&, const MicroVm::IoAccess& access) { Control(access); });
    prepared_ = true;
}
bool KernelBootSession::ValidIo(const MicroVm::IoAccess& access) {
    if (!access.write || access.bytes != 1 || access.string || access.repeat) {
        vm_.FailExecution(L"Boot channel requires scalar byte OUT"); return false;
    }
    return true;
}
void KernelBootSession::Control(const MicroVm::IoAccess& access) {
    using namespace guardian::abi;
    if (!ValidIo(access)) return;
    // Fixed trusted GPA, bounded copies, and no dereference of a guest-supplied pointer.
    if (!Range(ChannelGpa, MaxMessageBytes, vm_.RamSize())) { vm_.FailExecution(L"Control buffer outside backing RAM"); return; }
    std::array<uint8_t, MaxMessageBytes> packet{};
    const auto* source = static_cast<const uint8_t*>(vm_.RamData()) + ChannelGpa;
    MessageHeader header{}; std::memcpy(&header, source, sizeof(header));
    if (header.payloadBytes > MaxMessageBytes - sizeof(header)) { vm_.FailExecution(L"Control payload exceeds buffer"); return; }
    const auto bytes = sizeof(header) + header.payloadBytes;
    std::memcpy(packet.data(), source, bytes);
    if (!channel_.Consume({packet.data(), bytes})) { vm_.FailExecution(L"Boot protocol rejected"); return; }
    if (channel_.Stopped()) vm_.RequestStop();
}
bool KernelBootSession::PendingTimer(bool set) {
#if defined(__linux__)
    return vm_.TimerInterrupt(set);
#else
    const WHV_REGISTER_NAME name = WHvRegisterPendingInterruption;
    WHV_REGISTER_VALUE value{};
    if (set) {
        if (!vm_.GetRegisters(&name, 1, &value)) return false;
        if (value.PendingInterruption.InterruptionPending) { vm_.FailExecution(L"Cannot overwrite a pending CPU event"); return false; }
        value = {}; value.PendingInterruption.InterruptionPending = 1;
        value.PendingInterruption.InterruptionType = WHvX64PendingInterrupt;
        value.PendingInterruption.InterruptionVector = guardian::cpu::TimerVector;
    }
    return vm_.SetRegisters(&name, 1, &value);
#endif
}
void KernelBootSession::ClockService() {
    namespace gc = guardian::cpu;
    if (!timerArmed_) return;
    const auto ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - runStart_).count())+elapsedNs_;
    if (!timerInFlight_ && ns >= nextTimerNs_) {
#if defined(__linux__)
        if(!vm_.CanInjectInterrupt())vm_.InterruptWindow(true);
        else {
            vm_.InterruptWindow(false);
            if(!PendingTimer(true))return;
            timerInFlight_=true;++timerQueued_;nextTimerNs_=ns+gc::PeriodNs;
        }
#else
        const WHV_REGISTER_NAME names[]{WHvX64RegisterRflags, WHvRegisterInterruptState};
        WHV_REGISTER_VALUE values[2]{};
        if (!vm_.GetRegisters(names, 2, values)) { vm_.FailExecution(L"Cannot query interruptibility"); return; }
        WHV_REGISTER_VALUE notification{}; const WHV_REGISTER_NAME noteName = WHvX64RegisterDeliverabilityNotifications;
        if (!(values[0].Reg64 & 0x200) || values[1].InterruptState.InterruptShadow) {
            notification.DeliverabilityNotifications.InterruptNotification = 1;
            if (!vm_.SetRegisters(&noteName, 1, &notification)) vm_.FailExecution(L"Cannot request interrupt window");
        } else {
            if (!vm_.SetRegisters(&noteName, 1, &notification) || !PendingTimer(true)) {
                vm_.FailExecution(L"Virtual timer injection failed: " + vm_.Error()); return;
            }
            timerInFlight_ = true; ++timerQueued_; nextTimerNs_ = ns + gc::PeriodNs;
        }
#endif
    }
    clockSequence_ += 2;
    const gc::Clock clock{gc::ClockMagic, gc::Version, sizeof(gc::Clock), clockSequence_, bootId_, ns, timerQueued_,
                          utcEpoch100ns_, guardian::time::Frequency};
    std::memcpy(static_cast<uint8_t*>(vm_.RamData()) + gc::ClockGpa, &clock, sizeof(clock));
}
void KernelBootSession::CpuControl(const MicroVm::IoAccess& access) {
    namespace gc = guardian::cpu;
    if (!ValidIo(access)) return;
    switch (static_cast<uint8_t>(access.value)) {
    case 0:
        if (!timerArmed_) { vm_.FailExecution(L"Timer disarm before arm"); return; }
        timerArmed_ = false;
#if defined(__linux__)
        vm_.InterruptWindow(false);
#else
        {
            const WHV_REGISTER_NAME name = WHvX64RegisterDeliverabilityNotifications; const WHV_REGISTER_VALUE value{};
            if (!vm_.SetRegisters(&name, 1, &value)) { vm_.FailExecution(L"Cannot clear timer notification"); return; }
        }
#endif
        if (timerInFlight_ && !PendingTimer(false)) { vm_.FailExecution(L"Cannot cancel pending timer"); return; }
        timerInFlight_ = false; return;
    case 1:
        if (!userCodeSealed_ || timerArmed_ || cpuReportValid_) { vm_.FailExecution(L"Timer arm state rejected"); return; }
        timerArmed_ = true;
        nextTimerNs_ = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - runStart_).count()) + elapsedNs_ + gc::PeriodNs;
        ClockService(); return;
    case 2:
        if (!timerArmed_ || !timerInFlight_) { vm_.FailExecution(L"Unexpected timer acknowledgement"); return; }
        timerInFlight_ = false; ++timerAcknowledged_; return;
    case 3:
        if (timerArmed_ || cpuReportValid_) { vm_.FailExecution(L"CPU report state rejected"); return; }
        std::memcpy(&cpuReport_, static_cast<const uint8_t*>(vm_.RamData()) + gc::ReportGpa, sizeof(cpuReport_));
        if (!gc::ValidReport(cpuReport_, bootId_)) { vm_.FailExecution(L"CPU report format/version/session rejected"); return; }
        cpuReportValid_ = true; return;
    case 4:
        if (userCodeSealed_ || timerArmed_ || cpuReportValid_ || !vm_.SetMemoryAccess(gc::UserCode, guardian::abi::PageBytes, 5)) {
            vm_.FailExecution(L"User executable page sealing rejected"); return;
        }
        userCodeSealed_ = true; return;
    case 5:
        if(!cpuOptions_.processes || !userCodeSealed_ || !timerArmed_ || cpuReportValid_){
            vm_.FailExecution(L"Clock refresh outside a running process contract");return;
        }
        ClockService();return;
    default: vm_.FailExecution(L"Unknown CPU control command"); return;
    }
}
void KernelBootSession::MemoryControl(const MicroVm::IoAccess& access) {
    namespace gm = guardian::memory;
    if (!ValidIo(access)) return;
    if (!userCodeSealed_ || memoryReportValid_ || threadReportValid_ || processReportValid_ || (timerArmed_ && !cpuOptions_.threads && !cpuOptions_.processes)) { vm_.FailExecution(L"Memory control state rejected"); return; }
    const auto* ram = static_cast<const uint8_t*>(vm_.RamData());
    const auto command = static_cast<uint8_t>(access.value);
    if (command == 1) {
        gm::Request request{}; std::memcpy(&request, ram + gm::RequestGpa, sizeof(request));
        if (!gm::ValidRequest(request, bootId_, physicalRequests_ + 1, ramBytes_) ||
            !guardian::abi::Covered(regions_.data(), static_cast<uint32_t>(regions_.size()), request.gpa, request.length, guardian::abi::MemoryKind::Available)) {
            vm_.FailExecution(L"Physical permission request rejected"); return;
        }
        if (!vm_.SetMemoryAccess(request.gpa, request.length, static_cast<uint8_t>(request.access))) { vm_.FailExecution(L"Physical permission mapping failed"); return; }
        ++physicalRequests_; return;
    }
    if (command == 2) {
        if(cpuOptions_.threads || cpuOptions_.processes) { vm_.FailExecution(L"Memory report is not the selected contract"); return; }
        std::memcpy(&memoryReport_, ram + gm::ReportGpa, sizeof(memoryReport_));
        if (!gm::ValidReport(memoryReport_, bootId_) || memoryReport_.physicalRequests != physicalRequests_) { vm_.FailExecution(L"Memory report format/session/count rejected"); return; }
        memoryReportValid_ = true; return;
    }
    vm_.FailExecution(L"Unknown memory control command");
}
void KernelBootSession::ThreadControl(const MicroVm::IoAccess& access) {
    namespace gt = guardian::threads;
    if(!ValidIo(access)) return;
    if(static_cast<uint8_t>(access.value)!=1 || !userCodeSealed_ || timerArmed_ || threadReportValid_ || cpuReportValid_) {
        vm_.FailExecution(L"Thread report command/state rejected");return;
    }
    std::memcpy(&threadReport_,static_cast<const uint8_t*>(vm_.RamData())+gt::ReportGpa,sizeof(threadReport_));
    if(!gt::ValidReport(threadReport_,bootId_)) {vm_.FailExecution(L"Thread report format/session rejected");return;}
    threadReportValid_=true;
}
void KernelBootSession::ProcessControl(const MicroVm::IoAccess& access) {
    if(!ValidIo(access))return;
    if(static_cast<uint8_t>(access.value)!=1 || !userCodeSealed_ || timerArmed_ || processReportValid_ || cpuReportValid_) {vm_.FailExecution(L"Process report command/state rejected");return;}
    std::memcpy(&processReport_,static_cast<const uint8_t*>(vm_.RamData())+guardian::processes::ReportGpa,sizeof(processReport_));
    if(!guardian::processes::ValidReport(processReport_,bootId_)){vm_.FailExecution(L"Process report format/session rejected");return;}processReportValid_=true;
}
void KernelBootSession::FailIoDevice(uint32_t device) {
    Require(prepared_ && !ran_ && ioBackend_ && device && device<=guardian::io::DeviceCount,"Invalid stopped-device test injection");ioBackend_->Fail(device);
}
void KernelBootSession::IoControl(const MicroVm::IoAccess& access) {
    namespace gi=guardian::io;
    if(!ValidIo(access))return;
    if(!ioBackend_ || !userCodeSealed_ || ioReportValid_ || cpuReportValid_){vm_.FailExecution(L"I/O command/state rejected");return;}
    auto* ram=static_cast<uint8_t*>(vm_.RamData());
    const auto command=static_cast<uint8_t>(access.value);
    if(command==1){
        if((!timerArmed_ && !cpuOptions_.storage) || processReportValid_){vm_.FailExecution(L"I/O request outside running driver state");return;}
        gi::Packet packet{};std::memcpy(&packet,ram+gi::PacketGpa,sizeof(packet));
        if(!ioBackend_->Exchange(packet)){vm_.FailExecution(L"I/O envelope/session/sequence rejected");return;}
        std::memcpy(ram+gi::PacketGpa,&packet,sizeof(packet));
        if(!pauseAtIoConsole_.empty() && packet.device==1 && packet.op==gi::Op::Write &&
           packet.status==gi::Success && packet.transferred==packet.length &&
           std::string(reinterpret_cast<const char*>(packet.data),packet.length)==pauseAtIoConsole_){
            pauseAtIoConsole_.clear();vm_.RequestPause();
        }
        if(pauseAtIo_ && ioBackend_->Requests()==pauseAtIo_){pauseAtIo_=0;vm_.RequestPause();}return;
    }
    if(command==2 && !timerArmed_ && processReportValid_){
        std::memcpy(&ioReport_,ram+gi::ReportGpa,sizeof(ioReport_));
        if(!gi::ValidReport(ioReport_,bootId_)){vm_.FailExecution(L"I/O report format/session rejected");return;}ioReportValid_=true;return;
    }
    vm_.FailExecution(L"Unknown or out-of-order I/O command");
}
void KernelBootSession::GraphicsControl(const MicroVm::IoAccess& access) {
    namespace gg=guardian::graphics;if(!ValidIo(access))return;
    if(!graphicsBackend_||graphicsReportValid_||cpuReportValid_){vm_.FailExecution(L"Graphics state rejected");return;}
    auto* ram=static_cast<uint8_t*>(vm_.RamData());
    try {
        if(access.value==1){gg::Packet p{};std::memcpy(&p,ram+gg::PacketGpa,sizeof(p));
            if(!graphicsBackend_->Exchange(p,reinterpret_cast<const uint32_t*>(ram+gg::PixelsGpa),elapsedNs_+uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-runStart_).count()))){vm_.FailExecution(L"Graphics envelope rejected");return;}
            std::memcpy(ram+gg::PacketGpa,&p,sizeof(p));if(pauseAtGraphics_&&p.sequence==pauseAtGraphics_){pauseAtGraphics_=0;vm_.RequestPause();}return;}
        if(access.value==2&&!timerArmed_){std::memcpy(&graphicsReport_,ram+gg::ReportGpa,sizeof(graphicsReport_));
            if(graphicsReport_.magic!=gg::Magic||graphicsReport_.boot!=bootId_||graphicsReport_.version!=gg::Version||graphicsReport_.bytes!=sizeof(graphicsReport_)||(graphicsReport_.checks&~gg::RequiredChecks)){vm_.FailExecution(L"Graphics report rejected");return;}
            graphicsReportValid_=true;return;}
        vm_.FailExecution(L"Unknown graphics command");
    }catch(const std::exception&){vm_.FailExecution(L"Graphics host/display failure");}
}
void KernelBootSession::NetworkControl(const MicroVm::IoAccess& access) {
    namespace gn=guardian::net;
    if(!ValidIo(access))return;
    if(!networkBackend_||networkReportValid_||cpuReportValid_){vm_.FailExecution(L"Network device state rejected");return;}
    auto* ram=static_cast<uint8_t*>(vm_.RamData());
    try {
        if(access.value==1){gn::Packet p{};std::memcpy(&p,ram+gn::PacketGpa,sizeof(p));
            if(!networkBackend_->Exchange(p)){vm_.FailExecution(L"Network packet envelope/session/sequence rejected");return;}
            std::memcpy(ram+gn::PacketGpa,&p,sizeof(p));if(pauseAtNetwork_&&p.sequence==pauseAtNetwork_){pauseAtNetwork_=0;vm_.RequestPause();}return;
        }
        if(access.value==2&&!timerArmed_){std::memcpy(&networkReport_,ram+gn::ReportGpa,sizeof(networkReport_));
            if(networkReport_.magic!=gn::Magic||networkReport_.boot!=bootId_||networkReport_.version!=gn::Version||networkReport_.bytes!=sizeof(networkReport_)||networkReport_.mode!=uint32_t(networkBackend_->Enumeration().mode)||(networkReport_.checks&~gn::RequiredChecks)){vm_.FailExecution(L"Network report rejected");return;}networkReportValid_=true;return;
        }
        vm_.FailExecution(L"Unknown network command");
    }catch(const std::exception&){vm_.FailExecution(L"Network host/capture failure");}
}
void KernelBootSession::StorageControl(const MicroVm::IoAccess& access) {
    if(!ValidIo(access))return;
    if(static_cast<uint8_t>(access.value)!=1 || !userCodeSealed_ || timerArmed_ || !processReportValid_ || !ioReportValid_ || storageReportValid_ || cpuReportValid_){vm_.FailExecution(L"Storage report command/state rejected");return;}
    std::memcpy(&storageReport_,static_cast<const uint8_t*>(vm_.RamData())+guardian::storage::ReportGpa,sizeof(storageReport_));
    if(!guardian::storage::ValidReport(storageReport_,bootId_)){vm_.FailExecution(L"Storage report format/session rejected");return;}storageReportValid_=true;
}
void KernelBootSession::TelemetryControl(const MicroVm::IoAccess& access) {
    if(!ValidIo(access)||access.value!=1||!telemetryEnabled_){telemetry_.Reject();vm_.FailExecution(L"Telemetry port access rejected");return;}
    guardian::telemetry::Event event{};
    auto* packet=static_cast<uint8_t*>(vm_.RamData())+guardian::telemetry::PacketGpa;
    std::memcpy(&event,packet,sizeof(event));
    const auto ns=elapsedNs_+uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-runStart_).count());
    if(!telemetry_.Accept(event,ns)){vm_.FailExecution(L"Telemetry input/session/sequence rejected");return;}
    std::memcpy(packet,&event,sizeof(event));
}
BootResult KernelBootSession::Run(std::chrono::milliseconds timeout, bool suspendAtDeadline) {
    if(resourceSession_)return resourceSession_->Run(timeout,suspendAtDeadline);
    Require(prepared_ && (!ran_ || paused_), "Boot must be prepared; only a paused session can resume");
    if(paused_)Require(vm_.Restore(pausedMachine_),"Cannot restore frozen paused CPU");
    paused_=false;
    Require(timeout.count() > 0 && timeout <= std::chrono::seconds(30), "Invalid boot timeout"); ran_ = true;
    std::mutex mutex; std::condition_variable cv; bool done = false; std::atomic<bool> expired{false};
    runStart_ = std::chrono::steady_clock::now();
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (cv.wait_for(lock, timeout, [&] { return done; })) return;
        expired = true;
        do { if(suspendAtDeadline) vm_.RequestPause(); else vm_.Cancel(); } while (!cv.wait_for(lock, std::chrono::milliseconds(5), [&] { return done; }));
    });
    std::thread timer;
    try {
        if (cpuOptions_.enabled && cpuOptions_.driveTimer) timer = std::thread([&] {
            std::unique_lock lock(mutex);
            while (!cv.wait_for(lock, std::chrono::milliseconds(5), [&] { return done; }))
                if (timerArmed_) vm_.WakeForService();
        });
    } catch (...) { { std::lock_guard lock(mutex); done = true; } cv.notify_all(); watchdog.join(); throw; }
    bool completed = false;
    try { completed = vm_.Run(suspendAtDeadline||desktopBackend_ ? 1000000 : telemetryEnabled_ ? 100000 : 10000); }
    catch (...) { { std::lock_guard lock(mutex); done = true; } cv.notify_all(); watchdog.join(); if (timer.joinable()) timer.join(); throw; }
    { std::lock_guard lock(mutex); done = true; } cv.notify_all(); watchdog.join(); if (timer.joinable()) timer.join();
    elapsedNs_+=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-runStart_).count());
    paused_=vm_.Paused();
    if(paused_)Require(vm_.Capture(pausedMachine_),"Cannot capture paused machine");
    BootResult r;r.paused=paused_; r.bootId = bootId_; r.ramBytes = ramBytes_; r.exits = vm_.TotalExitCount(); r.timedOut = expired && !suspendAtDeadline;
    if(ntEnabled_)r.ntCompatibility=compatibility::Json(ntInfo_);
    r.driverMode=cpuOptions_.driver;if(driverBackend_){r.driverReportValid=driverBackend_->Valid();r.driver=driverBackend_->Report();}
    r.exceptionExitBitmap = exceptionExitBitmap_;
    r.cpuMode = cpuOptions_.enabled; r.cpuReportValid = cpuReportValid_; r.cpu = cpuReport_;
    r.timerQueued = timerQueued_; r.timerAcknowledged = timerAcknowledged_;
    r.memoryMode = cpuOptions_.memory; r.memoryReportValid = memoryReportValid_; r.memory = memoryReport_; r.physicalRequests = physicalRequests_;
    r.threadMode=cpuOptions_.threads;r.threadReportValid=threadReportValid_;r.threads=threadReport_;
    r.processMode=cpuOptions_.processes;r.processReportValid=processReportValid_;r.processes=processReport_;
    r.wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    r.stopStatus = channel_.Status(); r.diagnostic = channel_.Diagnostic(); r.console = console_;
    r.exception = vm_.LastExceptionVector(); r.rip = vm_.LastExceptionRip();
    r.memoryFault = vm_.LastMemoryAccessFault(); r.faultGpa = vm_.LastMemoryAccessGpa();
    r.storageMode=cpuOptions_.storage;r.storageReportValid=storageReportValid_;r.storage=storageReport_;
    if(storageBackend_){r.storageRecords=storageBackend_->Records();r.storageRecoveredTail=storageBackend_->RecoveredTailBytes();}
    r.ioMode=cpuOptions_.io;r.ioReportValid=ioReportValid_;r.io=ioReport_;
    if(ioBackend_){r.ioHostRequests=ioBackend_->Requests();r.ioHostConsole=ioBackend_->ConsoleSize();r.ioHostChannel=ioBackend_->ChannelBytes();r.ioHostBlock=ioBackend_->BlockTransferred();r.ioConsole.assign(reinterpret_cast<const char*>(ioBackend_->ConsoleData()),static_cast<size_t>(r.ioHostConsole));}
#if defined(__linux__)
    const bool cpuRead=vm_.ReadCpl(r.cpl);
#else
    WHV_REGISTER_VALUE cs{}; const WHV_REGISTER_NAME name = WHvX64RegisterCs;
    const bool cpuRead = vm_.GetRegisters(&name, 1, &cs);
    if (cpuRead) r.cpl = cs.Segment.Selector & 3;
#endif
    r.graphicsMode=cpuOptions_.graphics;r.graphicsReportValid=graphicsReportValid_;r.graphics=graphicsReport_;
    if(graphicsBackend_){r.graphicsFrames=graphicsBackend_->Frames();r.graphicsInputs=graphicsBackend_->Inputs();r.graphicsHash=graphicsBackend_->Hash();}
    r.networkMode=cpuOptions_.network;r.networkReportValid=networkReportValid_;r.network=networkReport_;
    if(networkBackend_){r.networkSent=networkBackend_->Sent();r.networkReceived=networkBackend_->Received();r.networkDenied=networkBackend_->Denied();}
    r.success = completed && !r.timedOut && cpuRead && r.cpl == 0 && channel_.Hello() && channel_.Stopped() &&
        !r.stopStatus && channel_.Error().empty() && r.diagnostic == (cpuOptions_.processes ? (cpuOptions_.io?(cpuOptions_.storage?"kernel storage self-test PASS":"kernel IO self-test PASS"):"kernel processes self-test PASS") : (cpuOptions_.threads ? "kernel threads self-test PASS" : (cpuOptions_.memory ? "kernel memory self-test PASS" :
            (cpuOptions_.enabled ? (cpuOptions_.graphics?"kernel graphics self-test PASS":(cpuOptions_.network?"kernel network self-test PASS":"kernel CPU self-test PASS")) : "kernel self-test PASS"))));
    if(cpuOptions_.driver)r.success=r.success&&r.driverReportValid&&r.driver.functional&&r.driver.unloaded&&!r.driver.error;
    if(externalGuiKernel_||externalPeKernel_)r.success=r.success&&cpuReportValid_&&!cpuReport_.fatal&&processReportValid_&&
        processReport_.checks==guardian::externalgui::Checks&&processReport_.coreChecks>=693&&processReport_.completed==1&&!processReport_.faulted&&
        processReport_.freeBefore==processReport_.freeAfter+21&&!processReport_.liveProcesses&&!processReport_.liveObjects&&!processReport_.liveHandles&&!processReport_.liveReferences&&timerAcknowledged_==processReport_.ticks;
    else if(analysisKernel_)r.success=r.success&&cpuReportValid_&&!cpuReport_.fatal&&processReportValid_&&
        processReport_.checks==guardian::telemetry::AnalysisChecks&&processReport_.coreChecks>=693&&processReport_.completed>=1&&
        processReport_.freeBefore==processReport_.freeAfter+21&&!processReport_.liveProcesses&&!processReport_.liveObjects&&!processReport_.liveHandles&&!processReport_.liveReferences&&timerAcknowledged_==processReport_.ticks;
    else if(cpuOptions_.processes)r.success=r.success && cpuReportValid_ && !cpuReport_.fatal && processReportValid_ &&
        processReport_.checks==guardian::processes::RequiredChecks && processReport_.completed==2 && processReport_.faulted==1 &&
        processReport_.freeBefore==processReport_.freeAfter+21 && !processReport_.liveProcesses && !processReport_.liveObjects && !processReport_.liveHandles && !processReport_.liveReferences && timerAcknowledged_==processReport_.ticks;
    else if(cpuOptions_.threads) r.success=r.success && cpuReportValid_ && !cpuReport_.fatal && threadReportValid_ &&
        threadReport_.checks==guardian::threads::RequiredChecks && threadReport_.ticks>=4 && threadReport_.preemptions>=4 &&
        threadReport_.idleTicks>0 && threadReport_.completed==8 && threadReport_.freeBefore==threadReport_.freeAfter+21 && timerAcknowledged_==threadReport_.ticks;
    else if (cpuOptions_.memory) r.success = r.success && cpuReportValid_ && !cpuReport_.fatal && memoryReportValid_ &&
        memoryReport_.checks == guardian::memory::RequiredChecks && memoryReport_.userFaults == 8 && memoryReport_.guardFaults == 1 &&
        memoryReport_.freeAfter + 21 == memoryReport_.freeBefore && memoryReport_.cycles == 128;
    else if (cpuOptions_.enabled) r.success = r.success && cpuReportValid_ && !cpuReport_.fatal &&
        cpuReport_.checks == guardian::cpu::RequiredChecks && cpuReport_.userFaults == 6 && cpuReport_.kernelFaults == 2 &&
        cpuReport_.userCompleted == 3 && cpuReport_.timerTicks >= 4 && cpuReport_.timerUserTicks >= 2 && timerAcknowledged_ == cpuReport_.timerTicks;
    if(cpuOptions_.io)r.success=r.success && ioReportValid_ && ioReport_.checks==guardian::io::RequiredChecks && ioReport_.coreChecks>=630 &&
        !ioReport_.liveDrivers && !ioReport_.liveRequests && !ioReport_.livePins && ioReport_.submitted==ioReport_.completed && !ioReport_.failed &&
        ioReport_.loads==3 && ioReport_.unloads==3 && ioReport_.consoleBytes==r.ioHostConsole && ioReport_.channelBytes==r.ioHostChannel && ioReport_.blockBytes==r.ioHostBlock;
    if(cpuOptions_.storage)r.success=r.success && storageReportValid_ && storageReport_.checks==guardian::storage::RequiredChecks && storageReport_.coreChecks>=700 &&
        storageReport_.sequenceAfter>storageReport_.sequenceBefore && !storageReport_.liveHandles && !storageReport_.liveWatches && storageReport_.userCalls>=10 && r.storageRecords>0;
    if(cpuOptions_.graphics)r.success=r.success&&graphicsReportValid_&&graphicsReport_.checks==((externalGuiKernel_||externalPeKernel_)?guardian::externalgui::Checks:guardian::graphics::RequiredChecks)&&graphicsReport_.applications==((externalGuiKernel_||externalPeKernel_)?1u:2u)&&graphicsReport_.frames==r.graphicsFrames&&graphicsReport_.inputs==r.graphicsInputs&&graphicsReport_.pixelHash==r.graphicsHash&&(externalPeKernel_?true:(externalGuiKernel_?graphicsReport_.frames>=1:(graphicsReport_.calls>=20&&graphicsReport_.denied>=6)));
    if(graphicsBackend_)graphicsBackend_->Export();
    if(windowBridge_){r.success=r.success&&windowBridge_->Completed();windowBridge_->Export(windowOutput_);}
    if(desktopBackend_){r.success=r.success&&desktopBackend_->Completed();desktopBackend_->Export();}
    if(cpuOptions_.network)r.success=r.success&&networkReportValid_&&networkReport_.checks==(analysisKernel_?1u:guardian::net::RequiredChecks)&&(!analysisKernel_||networkReport_.userCalls>=1)&&networkReport_.sent==r.networkSent&&networkReport_.received==r.networkReceived&&networkReport_.denied==r.networkDenied;
    if(networkBackend_)networkBackend_->Export();
    if(internetBackend_&&!internetReport_.empty())state::AtomicText(internetReport_,internetBackend_->Json());
    if (!r.success && !r.paused) {
        if (!channel_.Error().empty()) r.error = channel_.Error();
        else if (!vm_.Error().empty()) r.error = Narrow(vm_.Error());
        else if (channel_.Stopped() && r.stopStatus) r.error = "Kernel self-test failed with status " + std::to_string(r.stopStatus);
        else r.error = "Kernel did not complete Hello/self-test/StopResult with status zero in CPL0";
    }
#if !defined(__linux__)
    PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
    r.privateBytesValid = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != 0;
    if (r.privateBytesValid) r.hostPrivateBytes = counters.PrivateUsage;
#endif
    if(telemetryEnabled_&&!r.paused)telemetry_.Complete(r.success);
    return r;
}
void WriteBootReport(const std::filesystem::path& path, const std::vector<BootResult>& rows) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc); Require(bool(out), "Cannot create boot report");
#if defined(__linux__)
    out << "{\"schema\":2,\"backend\":\"KVM\",\"wine\":false,\"secondStageExecuteProtection\":false,\"workload\":\"guardian-kernel boot\",\"vcpus\":"
        << (rows.empty()?1u:rows.front().resourceMode?rows.front().resource.vcpus:1u) << ",\"configuration\":\""
        << GUARDIAN_BOOT_CONFIGURATION << "\",\"compiler\":\"GCC " << __VERSION__ << "\",\"boots\":[\n"
        << std::boolalpha << std::fixed << std::setprecision(3);
#else
    out << "{\"schema\":2,\"backend\":\"WHP\",\"workload\":\"guardian-kernel boot\",\"vcpus\":"
        << (rows.empty()?1u:rows.front().resourceMode?rows.front().resource.vcpus:1u) << ",\"configuration\":\""
        << GUARDIAN_BOOT_CONFIGURATION << "\",\"msvcFullVersion\":" << _MSC_FULL_VER << ",\"boots\":[\n"
        << std::boolalpha << std::fixed << std::setprecision(3);
#endif
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i]; if (i) out << ",\n";
        out << "{\"bootId\":" << r.bootId << ",\"ntCompatibility\":" << (r.ntCompatibility.empty()?"null":r.ntCompatibility) << ",\"success\":" << r.success << ",\"ramBytes\":" << r.ramBytes
            << ",\"paused\":" << r.paused << ",\"wallMs\":" << r.wallMs << ",\"exits\":" << r.exits << ",\"stopStatus\":" << r.stopStatus
            << ",\"cpl\":" << r.cpl << ",\"timedOut\":" << r.timedOut << ",\"hostPrivateBytes\":" << r.hostPrivateBytes
            << ",\"privateBytesValid\":" << r.privateBytesValid << ",\"exception\":" << r.exception
            << ",\"rip\":" << r.rip << ",\"exceptionExitBitmap\":" << r.exceptionExitBitmap << ",\"memoryFault\":" << r.memoryFault << ",\"faultGpa\":" << r.faultGpa
            << ",\"diagnostic\":" << Quoted(r.diagnostic) << ",\"console\":" << Quoted(r.console) << ",\"error\":" << Quoted(r.error)
            << ",\"cpuMode\":" << r.cpuMode << ",\"cpuReportValid\":" << r.cpuReportValid;
        out<<",\"resourceMode\":"<<r.resourceMode<<",\"resourceReportValid\":"<<r.resourceReportValid;
        if(r.resourceMode){const auto& p=r.resource;out<<",\"resource\":{\"vcpus\":"<<p.vcpus<<",\"readyMask\":"<<p.readyMask<<",\"topologyMask\":"<<p.topologyMask<<",\"doneMask\":"<<p.doneMask<<",\"fpuMask\":"<<p.fpuMask<<",\"counter\":"<<p.counter<<",\"capacityBytes\":"<<p.capacity<<",\"fileBytes\":"<<p.fileBytes<<",\"allocatedBytes\":"<<p.space.allocated<<",\"freeBytes\":"<<p.space.free<<",\"availableBytes\":"<<p.space.available<<",\"checks\":"<<p.checks<<",\"status\":"<<p.status<<"}";}
        if (r.cpuMode) {
            const auto& c = r.cpu;
            out << ",\"cpu\":{\"checks\":" << c.checks << ",\"fatal\":" << c.fatal << ",\"syscalls\":" << c.syscalls
                << ",\"echoes\":" << c.echoes << ",\"unknownSyscalls\":" << c.unknownSyscalls << ",\"userEntries\":" << c.userEntries
                << ",\"userCompleted\":" << c.userCompleted << ",\"userFaults\":" << c.userFaults << ",\"kernelFaults\":" << c.kernelFaults
                << ",\"returnRejects\":" << c.returnRejects << ",\"timerTicks\":" << c.timerTicks << ",\"timerUserTicks\":" << c.timerUserTicks
                << ",\"timerQueued\":" << r.timerQueued << ",\"timerAcknowledged\":" << r.timerAcknowledged
                << ",\"clockStartNs\":" << c.clockStartNs << ",\"clockEndNs\":" << c.clockEndNs << ",\"deadlineNs\":" << c.deadlineNs
                << ",\"deadlineObservedNs\":" << c.deadlineObservedNs << ",\"userProgress\":" << c.userProgress << ",\"faults\":[";
            const auto faultJson = [&](const guardian::cpu::Fault& f) {
                out << "{\"vector\":" << f.vector << ",\"cpl\":" << f.cpl << ",\"rip\":" << f.rip << ",\"rsp\":" << f.rsp
                    << ",\"rflags\":" << f.rflags << ",\"cr2\":" << f.cr2 << ",\"error\":" << f.error << '}';
            };
            for (uint32_t n = 0; n < c.faultCount && n < 12; ++n) { if (n) out << ','; faultJson(c.faults[n]); }
            out << "],\"fatalFault\":"; faultJson(c.fatalFault); out << '}';
        }
        out << ",\"memoryMode\":" << r.memoryMode << ",\"memoryReportValid\":" << r.memoryReportValid;
        if (r.memoryMode) {
            const auto& m = r.memory;
            out << ",\"memory\":{\"checks\":" << m.checks << ",\"freeBefore\":" << m.freeBefore << ",\"freeAfter\":" << m.freeAfter
                << ",\"peakFrames\":" << m.peakFrames << ",\"allocationFailures\":" << m.allocationFailures << ",\"cycles\":" << m.cycles
                << ",\"switches\":" << m.switches << ",\"guardFaults\":" << m.guardFaults << ",\"userFaults\":" << m.userFaults
                << ",\"heapChecks\":" << m.heapChecks << ",\"exhaustedFrames\":" << m.exhaustedFrames << ",\"physicalRequests\":" << r.physicalRequests << ",\"faults\":[";
            for (uint64_t n = 0; n < m.userFaults && n < 8; ++n) { if (n) out << ','; out << "{\"address\":" << m.faultAddress[n] << ",\"error\":" << m.faultError[n] << '}'; }
            out << "]}";
        }
        out << ",\"threadMode\":" << r.threadMode << ",\"threadReportValid\":" << r.threadReportValid;
        if(r.threadMode) {
            const auto& t=r.threads;
            out << ",\"threads\":{\"checks\":"<<t.checks<<",\"coreChecks\":"<<t.coreChecks<<",\"ticks\":"<<t.ticks<<",\"idleTicks\":"<<t.idleTicks
                <<",\"preemptions\":"<<t.preemptions<<",\"contextSwitches\":"<<t.contextSwitches<<",\"userSyscalls\":"<<t.userSyscalls
                <<",\"completed\":"<<t.completed<<",\"faults\":"<<t.faults<<",\"apcs\":"<<t.apcs<<",\"freeBefore\":"<<t.freeBefore
                <<",\"freeAfter\":"<<t.freeAfter<<",\"counters\":["<<t.counters[0]<<','<<t.counters[1]<<','<<t.counters[2]<<','<<t.counters[3]<<"]}";
        }
        out<<",\"processMode\":"<<r.processMode<<",\"processReportValid\":"<<r.processReportValid;
        if(r.processMode){const auto& p=r.processes;
            out<<",\"processes\":{\"checks\":"<<p.checks<<",\"coreChecks\":"<<p.coreChecks<<",\"ticks\":"<<p.ticks<<",\"switches\":"<<p.switches
                <<",\"completed\":"<<p.completed<<",\"faulted\":"<<p.faulted<<",\"denied\":"<<p.denied<<",\"inheritance\":"<<p.inheritance
                <<",\"remoteCopies\":"<<p.remoteCopies<<",\"freeBefore\":"<<p.freeBefore<<",\"freeAfter\":"<<p.freeAfter<<",\"liveProcesses\":"<<p.liveProcesses
                <<",\"liveObjects\":"<<p.liveObjects<<",\"liveHandles\":"<<p.liveHandles<<",\"liveReferences\":"<<p.liveReferences<<'}';}
        out<<",\"driverMode\":"<<r.driverMode<<",\"driverReportValid\":"<<r.driverReportValid<<",\"driverInstalled\":"<<r.driver.installed<<",\"driverInitialized\":"<<r.driver.initialized<<",\"driverFunctional\":"<<r.driver.functional<<",\"driverUnloaded\":"<<r.driver.unloaded<<",\"driverOperations\":"<<r.driver.operations<<",\"driverError\":"<<r.driver.error;
        out<<",\"graphicsMode\":"<<r.graphicsMode<<",\"graphicsReportValid\":"<<r.graphicsReportValid<<",\"graphicsChecks\":"<<r.graphics.checks<<",\"graphicsFrames\":"<<r.graphicsFrames<<",\"graphicsInputs\":"<<r.graphicsInputs<<",\"graphicsHash\":"<<r.graphicsHash<<",\"graphicsUserCalls\":"<<r.graphics.calls<<",\"graphicsDenied\":"<<r.graphics.denied;
        out<<",\"networkMode\":"<<r.networkMode<<",\"networkReportValid\":"<<r.networkReportValid<<",\"networkChecks\":"<<r.network.checks<<",\"networkSent\":"<<r.networkSent<<",\"networkUserCalls\":"<<r.network.userCalls<<",\"networkReceived\":"<<r.networkReceived<<",\"networkDenied\":"<<r.networkDenied;
        out<<",\"ioMode\":"<<r.ioMode<<",\"ioReportValid\":"<<r.ioReportValid;
        if(r.ioMode){const auto& x=r.io;
            out<<",\"io\":{\"checks\":"<<x.checks<<",\"coreChecks\":"<<x.coreChecks<<",\"submitted\":"<<x.submitted<<",\"completed\":"<<x.completed
                <<",\"cancelled\":"<<x.cancelled<<",\"failed\":"<<x.failed<<",\"loads\":"<<x.loads<<",\"unloads\":"<<x.unloads
                <<",\"liveDrivers\":"<<x.liveDrivers<<",\"liveRequests\":"<<x.liveRequests<<",\"livePins\":"<<x.livePins
                <<",\"consoleBytes\":"<<x.consoleBytes<<",\"channelBytes\":"<<x.channelBytes<<",\"blockBytes\":"<<x.blockBytes<<",\"ticks\":"<<x.ticks
                <<",\"hostRequests\":"<<r.ioHostRequests<<",\"hostConsole\":"<<r.ioHostConsole<<",\"hostChannel\":"<<r.ioHostChannel<<",\"hostBlock\":"<<r.ioHostBlock
                <<",\"console\":"<<Quoted(r.ioConsole)<<'}';}
        out<<",\"storageMode\":"<<r.storageMode<<",\"storageReportValid\":"<<r.storageReportValid;
        if(r.storageMode){const auto& x=r.storage;
            out<<",\"storage\":{\"checks\":"<<x.checks<<",\"coreChecks\":"<<x.coreChecks<<",\"sequenceBefore\":"<<x.sequenceBefore<<",\"sequenceAfter\":"<<x.sequenceAfter
                <<",\"recovered\":"<<x.recovered<<",\"userCalls\":"<<x.userCalls<<",\"reads\":"<<x.reads<<",\"writes\":"<<x.writes<<",\"registryChanges\":"<<x.registryChanges
                <<",\"notifications\":"<<x.notifications<<",\"denials\":"<<x.denials<<",\"liveHandles\":"<<x.liveHandles<<",\"liveWatches\":"<<x.liveWatches
                <<",\"hostRecords\":"<<r.storageRecords<<",\"recoveredTailBytes\":"<<r.storageRecoveredTail<<'}';}
        out << '}';
    }
    out << "\n]}\n"; out.flush(); Require(bool(out), "Cannot write complete boot report");
}
}
