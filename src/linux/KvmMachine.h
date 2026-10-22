#pragma once
#include "../microvm/state/State.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <linux/kvm.h>
#include <pthread.h>
#include <signal.h>

namespace av::microvm {
// One VM, one vCPU, fixed RAM. The guest gets no host devices or filesystem.
// KVM's public memory API enforces host read-only pages; guest page tables
// enforce NX. Independent second-stage execute permissions are unavailable.
class MicroVm {
public:
    ~MicroVm();
    MicroVm()=default;
    MicroVm(const MicroVm&)=delete;
    MicroVm& operator=(const MicroVm&)=delete;
    static bool Available();
    bool Create(size_t ramBytes,uint64_t exceptionBitmap=0);
    void ConfigureCpuIdentity(const std::string& brand) {
        state::Require(kvm_<0&&!brand.empty()&&brand.size()<=48,"CPU identity must be configured before VM creation");cpuIdentityBrand_=brand;
    }
    bool SetupLongMode(uint64_t entry,uint64_t stack);
    bool BootArguments(uint64_t boot,uint64_t cpu);
    bool SetMemoryAccess(uint64_t gpa,uint64_t bytes,uint8_t access);
    bool CanAccessMemory(uint64_t gpa,uint64_t bytes,uint8_t access) const;
    bool InterruptWindow(bool requested);
    bool CanInjectInterrupt() const;
    bool TimerInterrupt(bool pending);
    bool ReadCpl(uint32_t& cpl);
    bool Run(uint64_t maxExits);
    bool Cancel();
    bool RequestPause();
    bool Paused() const {return paused_;}
    bool Capture(state::MachineState&);
    bool Restore(const state::MachineState&);
    bool WakeForService();
    void RequestStop(){stop_=true;}
    void FailExecution(const std::wstring& reason){fatal_=true;stop_=true;error_=reason;}
    struct IoAccess {uint16_t port;uint8_t bytes;bool write,string,repeat;uint32_t value;uint64_t rip;};
    using DetailedPortTrap=std::function<void(MicroVm&,const IoAccess&)>;
    void MapIoTrapDetailed(uint16_t port,DetailedPortTrap fn){traps_[port]=std::move(fn);}
    void OnServiceWake(std::function<void(MicroVm&)> fn){service_=std::move(fn);}
    void StrictIo(bool){}
    bool SetPortInput(uint32_t,uint8_t);
    void* RamData(){return ram_;}
    const void* RamData() const{return ram_;}
    size_t RamSize() const{return bytes_;}
    uint64_t TotalExitCount() const{return exits_;}
    uint8_t LastExceptionVector() const{return exception_;}
    uint64_t LastExceptionRip() const{return rip_;}
    bool LastMemoryAccessFault() const{return memoryFault_;}
    uint64_t LastMemoryAccessGpa() const{return faultGpa_;}
    const std::wstring& Error() const{return error_;}
private:
    friend class SmpMachine;
    uint32_t topologyCpus_=1,topologyId_=0;
    bool topologyEnabled_=false;
    bool Fail(const char* operation);
    bool Kick();
    bool CompleteIo();
    struct Slot {uint64_t gpa,bytes;uint32_t id;bool readOnly;};
    int kvm_=-1,vm_=-1,cpu_=-1;
    uint8_t* ram_=nullptr;kvm_run* run_=nullptr;
    size_t bytes_=0,runBytes_=0;uint32_t maxSlots_=0;
    std::vector<Slot> slots_;
    std::vector<uint8_t> access_;
    std::map<uint16_t,DetailedPortTrap> traps_;
    std::function<void(MicroVm&)> service_;
    std::atomic<bool> cancel_{false},wake_{false};
    std::atomic<pthread_t> owner_{};
    bool hasRun_=false;
    bool signalInstalled_=false,ioPending_=false,paused_=false;
    std::atomic<bool> pause_{false};
    bool stop_=false,fatal_=false,memoryFault_=false;
    uint64_t cpuidIdentity_=0;
    std::string cpuIdentityBrand_;
    uint8_t exception_=0;uint64_t rip_=0,faultGpa_=0,exits_=0;
    std::wstring error_;
};
}
