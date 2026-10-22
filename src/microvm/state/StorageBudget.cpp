#include "StorageBudget.h"
#include "Files.h"
#include <algorithm>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif
namespace av::microvm::state {
namespace fs=std::filesystem;
namespace {
void OrdinaryEntry(const fs::path& p,bool directory){
    const auto status=fs::symlink_status(p);Require(!fs::is_symlink(status)&&(directory?fs::is_directory(status):fs::is_regular_file(status)),"Storage budget entry type rejected");
#ifdef _WIN32
    const auto attr=GetFileAttributesW(p.c_str());Require(attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT),"Storage budget reparse entry rejected");
#endif
}
}
uint64_t AllocationUnit(const fs::path& path){
#ifdef _WIN32
    wchar_t volume[32768]{};DWORD sectors=0,bytes=0,free=0,total=0;
    Require(GetVolumePathNameW(path.c_str(),volume,32768)&&GetDiskFreeSpaceW(volume,&sectors,&bytes,&free,&total)&&sectors&&bytes,"Cannot query host allocation unit");
    return uint64_t(sectors)*bytes;
#else
    struct statvfs s{};Require(::statvfs(path.c_str(),&s)==0&&s.f_frsize,"Cannot query host allocation unit");return s.f_frsize;
#endif
}
uint64_t AllocatedBytes(const fs::path& path){
    OrdinaryEntry(path,false);
#ifdef _WIN32
    const auto h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Require(h!=INVALID_HANDLE_VALUE,"Cannot query storage allocation");FILE_STANDARD_INFO info{};const auto ok=GetFileInformationByHandleEx(h,FileStandardInfo,&info,sizeof(info));CloseHandle(h);
    Require(ok&&info.AllocationSize.QuadPart>=0,"Cannot query storage allocation");
    const auto attributes=GetFileAttributesW(path.c_str());Require(attributes!=INVALID_FILE_ATTRIBUTES,"Cannot inspect storage allocation attributes");
    if(attributes&FILE_ATTRIBUTE_SPARSE_FILE){DWORD high=0;SetLastError(ERROR_SUCCESS);const auto low=GetCompressedFileSizeW(path.c_str(),&high);Require(low!=INVALID_FILE_SIZE||GetLastError()==ERROR_SUCCESS,"Cannot query sparse physical allocation");return (uint64_t(high)<<32)|low;}
    return uint64_t(info.AllocationSize.QuadPart);
#else
    struct stat s{};Require(::lstat(path.c_str(),&s)==0&&S_ISREG(s.st_mode)&&s.st_blocks>=0,"Cannot query storage allocation");return uint64_t(s.st_blocks)*512;
#endif
}
DiskUsage StorageUsage(const fs::path& root){
    OrdinaryEntry(root,true);DiskUsage out;
    for(const auto& e:fs::recursive_directory_iterator(root)){
        try {
        const auto type=fs::symlink_status(e.path());if(fs::is_directory(type)){OrdinaryEntry(e.path(),true);continue;}
        OrdinaryEntry(e.path(),false);const auto logical=fs::file_size(e.path()),allocated=AllocatedBytes(e.path());
        Require(logical<=UINT64_MAX-out.logical&&allocated<=UINT64_MAX-out.allocated,"Storage accounting overflow");out.logical+=logical;out.allocated+=allocated;
        }catch(...){std::error_code ec;const auto present=fs::exists(e.path(),ec);if(!present&&!ec)continue;throw;}
    }return out;
}
DiskUsage CombinedUsage(const fs::path& root,const fs::path& extra){
    auto out=StorageUsage(root);out.charged=std::max(out.logical,out.allocated);
    if(extra.empty())return out;
    OrdinaryEntry(extra,true);
    const auto relative=fs::absolute(extra).lexically_normal().lexically_relative(fs::absolute(root).lexically_normal());
    Require(relative.empty()||relative.is_absolute()||(!relative.empty()&&*relative.begin()==".."),"Independent disk root must be outside VM state");
    for(const auto& e:fs::recursive_directory_iterator(extra)){
        try{
            const auto type=fs::symlink_status(e.path());if(fs::is_directory(type)){OrdinaryEntry(e.path(),true);continue;}
            OrdinaryEntry(e.path(),false);const auto logical=fs::file_size(e.path()),allocated=AllocatedBytes(e.path());
            const bool raw=e.path().filename()=="payload.gvd2"&&Ordinary(e.path().parent_path()/".guardian-disk-owner");
            const auto cost=raw?allocated:std::max(logical,allocated);
            Require(logical<=UINT64_MAX-out.logical&&allocated<=UINT64_MAX-out.allocated&&cost<=UINT64_MAX-out.charged,"Storage accounting overflow");
            out.logical+=logical;out.allocated+=allocated;out.charged+=cost;
        }catch(...){std::error_code ec;if(!fs::exists(e.path(),ec)&&!ec)continue;throw;}
    }return out;
}
uint64_t StorageAvailable(const fs::path& root,uint64_t limit,const fs::path& extra){
    const auto used=CombinedUsage(root,extra).charged;
    return limit>StorageControlReserve&&used<limit-StorageControlReserve?limit-StorageControlReserve-used:0;
}
thread_local const StorageBudget* StorageBudget::active_=nullptr;
StorageBudget::StorageBudget(fs::path root,uint64_t limit,fs::path extra):root_(fs::absolute(std::move(root)).lexically_normal()),extra_(extra.empty()?extra:fs::absolute(std::move(extra)).lexically_normal()),limit_(limit),previous_(active_){Require(limit>=1ull<<20&&limit<=64ull<<30,"Storage budget must be 1 MiB..64 GiB");active_=this;}
StorageBudget::~StorageBudget(){active_=previous_;}
void StorageBudget::Check(const fs::path& path,uint64_t bytes){
    if(!active_)return;
    const auto relative=fs::absolute(path).lexically_normal().lexically_relative(active_->root_);
    auto inside=[](const fs::path& p){return !p.empty()&&!p.is_absolute()&&*p.begin()!="..";};
    const auto extraRelative=active_->extra_.empty()?fs::path():fs::absolute(path).lexically_normal().lexically_relative(active_->extra_);
    if(!inside(relative)&&!inside(extraRelative))return;
    const auto name=relative.filename().string();
    const bool control=name=="status.gvt"||name=="reply.gvr"||name=="request.gvr"||name=="launch.gvt";
    const auto unit=AllocationUnit(path.parent_path());Require(bytes<=UINT64_MAX-unit,"Storage growth overflow");const auto growth=((bytes+unit-1)/unit)*unit;
    const auto usage=CombinedUsage(active_->root_,active_->extra_).charged;
    const auto ceiling=control?active_->limit_:active_->limit_-StorageControlReserve;
    Require(usage<=ceiling&&growth<=ceiling-usage,"VM storage quota exhausted (including snapshots and atomic replacement)");
    const auto available=fs::space(path.parent_path()).available;
    Require(control?growth<=available:(available>HostDiskReserve&&growth<=available-HostDiskReserve),"Host disk reserve exhausted");
}
}
