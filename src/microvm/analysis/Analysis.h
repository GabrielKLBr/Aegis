#pragma once
#include "guardian/Telemetry.h"
#include "../state/State.h"
#include <string>
#include <vector>
namespace av::microvm::analysis {
using namespace guardian::telemetry;
struct Record {Event event{};uint64_t observedNs=0;};
class Trace {
public:
    void Configure(uint64_t boot,const Policy& policy={});
    bool Accept(Event&,uint64_t observedNs);
    void RecordCapacity(uint32_t);
    void Reject() noexcept {++rejected_;complete_=false;}
    void Complete(bool success) noexcept {finished_=true;complete_=success&&!rows_.empty()&&!dropped_&&!rejected_&&pending_.empty();}
    void Save(state::Writer&) const;
    void Load(state::Reader&,uint64_t boot,const Policy&);
    std::string Json() const;
    std::string Html() const;
    const std::vector<Record>& Records() const {return rows_;}
    uint64_t Boot() const {return boot_;}
    uint64_t Dropped() const {return dropped_;}
    uint64_t Rejected() const {return rejected_;}
    bool Coherent() const {return finished_&&complete_&&!dropped_&&!rejected_;}
    const Policy& Configuration() const {return policy_;}
private:
    uint64_t boot_=0,lastSequence_=0,lastNs_=0,dropped_=0,rejected_=0;
    Policy policy_{};uint32_t recordCapacity_=DefaultEvents;bool finished_=false,complete_=false;
    std::vector<Record> rows_;
    std::vector<Event> pending_;
};
std::string Quote(const std::string&);
std::string DomainName(Domain);
Domain ParseDomain(const std::string&);
}
