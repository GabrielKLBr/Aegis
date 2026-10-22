#pragma once
#include "guardian/Driver.h"
#include "../state/State.h"
#if defined(__linux__)
#include "../../linux/KvmMachine.h"
#else
#include "../vmm/MicroVmMonitor.h"
#endif
namespace av::microvm::hardware {
struct DriverOptions {std::vector<uint8_t> image,api;std::string service;};
class DriverBackend {
public:
    DriverBackend(MicroVm&,uint64_t,const DriverOptions&);
    void Control(const MicroVm::IoAccess&);
    void Exchange(const MicroVm::IoAccess&);
    void Save(state::Writer&) const;
    void Load(state::Reader&);
    void ValidateSnapshot(const state::MachineState&) const;
    bool Valid() const {return valid_;}
    uint32_t Writes() const {return writes_;}
    const guardian::driver::Report& Report() const {return report_;}
    uint64_t PackageHash() const {return boot_.packageHash;}
private:
    MicroVm& vm_;DriverOptions options_;guardian::driver::Boot boot_{};
    guardian::driver::Report report_{};uint64_t sequence_=0;
    uint32_t writes_=0,reads_=0,pending_=0;bool ready_=false,valid_=false;
};
}
