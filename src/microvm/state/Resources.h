#pragma once
#include "Files.h"
#include <memory>
namespace av::microvm::state::resources {
inline constexpr uint64_t MiB=1ull<<20, MaxRamMiB=1024;
// Capture, encoding, restore and replacement can coexist with live guest RAM.
inline constexpr uint64_t RamCopies=8, WorkerOverhead=128*MiB;
struct Host { uint64_t total=0, available=0; uint32_t cpus=0; };
struct Usage { uint64_t committed=0; uint32_t cpus=0, workers=0; uint64_t reserve=0; };
struct Plan { uint32_t ramMiB=0, vcpus=0; uint64_t commitment=0, reserve=0; };
Host Probe();
Usage Inspect();
uint64_t Reserve(const Host&,uint32_t reserveMiB=0) noexcept;
Plan Admit(uint32_t requestedMiB,uint32_t vcpus,bool maximum,const Host&,Usage={},uint32_t reserveMiB=0,uint64_t diskBytes=0);
// A process lifetime lease shared by every catalog owned by this OS user.
class Reservation {
public:
    explicit Reservation(uint32_t ramMiB,uint32_t vcpus=1,uint32_t reserveMiB=0,uint64_t diskBytes=0);
private:
    std::unique_ptr<Lease> lease_;
};
}
