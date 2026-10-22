#pragma once
#include "guardian/Network.h"
namespace guardian::net {
// Kernel-only driver, fixed GPA packets; guest application addresses never
// cross the host boundary. IP/UDP and DNS serialization run in the guest.
class Driver {
  public:
    bool Initialize(uint64_t boot) noexcept;
    Status Transmit(const Frame &) noexcept;
    Status Receive(Frame &) noexcept;
    void AuditActor(uint32_t pid,uint32_t user) noexcept {auditPid_=pid;auditUser_=user;}
    uint64_t Sent() const noexcept {return sent_;}
    uint64_t Received() const noexcept {return received_;}
    uint64_t Denied() const noexcept {return denied_;}
    const Info &Enumeration() const noexcept { return info_; }

  private:
    Status Exchange(Op, Frame &) noexcept;
    Info info_{};
    uint64_t sequence_ = 0,sent_=0,received_=0,denied_=0;
    uint32_t auditPid_=0,auditUser_=0;
};
} // namespace guardian::net
void NetworkSelfTest(void (*check)(bool, uint32_t) noexcept) noexcept;
void NetworkPublish() noexcept;

uint32_t NetworkSyscall(uint64_t operation, uint64_t address, uint64_t bytes, uint64_t &transferred) noexcept;
extern "C" uint64_t NetworkUserInvoke(uint64_t operation, uint64_t address, uint64_t bytes) noexcept;
extern "C" uint32_t NetworkUserExchange(uint32_t operation, guardian::net::Frame &) noexcept;

bool NetworkRuntimeInitialize(uint64_t) noexcept;
uint32_t NetworkRuntimeEcho(uint32_t,uint32_t) noexcept;
