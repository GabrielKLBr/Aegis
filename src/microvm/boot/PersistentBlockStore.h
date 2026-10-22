#pragma once
#include "guardian/Storage.h"
#include "guardian/StorageQuota.h"
#if defined(__linux__)
#include <unistd.h>
#else
#include <Windows.h>
#endif
#include <filesystem>
#include <vector>
namespace av::microvm::boot {
struct StorageOptions {
    std::filesystem::path base,overlay;bool createIfMissing=false;
    uint64_t overlayLimitBytes=512+4096ull*544,volumeQuotaBytes=guardian::storage::VolumeCapacity;
    uint64_t hostReserveBytes=256ull<<20;
    std::filesystem::path budgetRoot{};uint64_t aggregateLimitBytes=0;
};
class PersistentBlockStore {
public:
    explicit PersistentBlockStore(const StorageOptions&);
    ~PersistentBlockStore()=default;
    PersistentBlockStore(const PersistentBlockStore&)=delete;
    PersistentBlockStore& operator=(const PersistentBlockStore&)=delete;
    bool Read(uint64_t offset,uint8_t*) noexcept;
    bool Write(uint64_t offset,const uint8_t*) noexcept;
    bool Flush() noexcept;
    std::vector<uint8_t> Snapshot();
    bool Matches(const std::vector<uint8_t>&);
    void FailAfterWrites(int64_t count) noexcept {budget_=count;}
    uint64_t Records() const noexcept {return sequence_;}
    uint64_t RecoveredTailBytes() const noexcept {return recoveredTail_;}
    uint32_t LastStatus() const noexcept {return status_;}
    guardian::storage::BackingSpace Space() const noexcept;
private:
    #if defined(__linux__)
    struct File {int handle=-1;~File(){if(handle>=0)::close(handle);}};
#else
    struct File {HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
#endif
    File base_,overlay_;uint8_t blocks_[guardian::io::BlockBytes]{};
    uint64_t sequence_=0,tail_=512,recoveredTail_=0;int64_t budget_=-1;bool failed_=false;
    StorageOptions options_;
    uint32_t status_=0;
};
}
