#pragma once
#include "State.h"
#include <filesystem>
namespace av::microvm::state {
struct DiskUsage {uint64_t logical=0,allocated=0,charged=0;};
uint64_t AllocationUnit(const std::filesystem::path&);
uint64_t AllocatedBytes(const std::filesystem::path&);
DiskUsage StorageUsage(const std::filesystem::path&);
DiskUsage CombinedUsage(const std::filesystem::path&,const std::filesystem::path& extra);
uint64_t StorageAvailable(const std::filesystem::path&,uint64_t limit,const std::filesystem::path& extra={});
// The small control reserve permits shutdown/replies when data storage is full.
inline constexpr uint64_t StorageControlReserve=64ull<<10,HostDiskReserve=256ull<<20;
class StorageBudget {
public:
    StorageBudget(std::filesystem::path root,uint64_t limit,std::filesystem::path extra={});
    ~StorageBudget();
    StorageBudget(const StorageBudget&)=delete;
    StorageBudget& operator=(const StorageBudget&)=delete;
    static void Check(const std::filesystem::path&,uint64_t bytes);
private:
    std::filesystem::path root_,extra_;uint64_t limit_;const StorageBudget* previous_;
    static thread_local const StorageBudget* active_;
};
}
