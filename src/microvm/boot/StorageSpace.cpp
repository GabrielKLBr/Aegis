#include "PersistentBlockStore.h"
#include "../state/StorageBudget.h"
#include <algorithm>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace av::microvm::boot {
guardian::storage::BackingSpace PersistentBlockStore::Space() const noexcept {
    guardian::storage::BackingSpace out{};
    out.limit=options_.overlayLimitBytes;out.used=tail_;out.hostReserve=options_.hostReserveBytes;
    try {
        out.hostAvailable=std::filesystem::space(options_.overlay.parent_path()).available;
        const auto unit=state::AllocationUnit(options_.overlay.parent_path());
        const auto ceiling=(out.limit/unit)*unit;
        const auto quota=tail_<ceiling?ceiling-tail_:0;
        const auto host=out.hostAvailable>out.hostReserve?out.hostAvailable-out.hostReserve:0;
        const auto records=sequence_<4096?4096-sequence_:0;
        const auto aggregate=options_.aggregateLimitBytes?state::StorageAvailable(options_.budgetRoot,options_.aggregateLimitBytes):UINT64_MAX;
        out.aggregateLimit=options_.aggregateLimitBytes;
        out.aggregateAvailable=aggregate==UINT64_MAX?0:aggregate;
        out.available=std::min(std::min(std::min(quota,(host/unit)*unit),(aggregate/unit)*unit),records*544);
        out.available=(out.available/544)*544;
#ifdef _WIN32
        FILE_STANDARD_INFO info{};
        if(GetFileInformationByHandleEx(overlay_.handle,FileStandardInfo,&info,sizeof(info))&&info.AllocationSize.QuadPart>=0)
            out.allocated=uint64_t(info.AllocationSize.QuadPart);
#else
        struct stat info{};if(::fstat(overlay_.handle,&info)==0&&info.st_blocks>=0)out.allocated=uint64_t(info.st_blocks)*512;
#endif
    }catch(...){out.available=0;}
    return out;
}
}
