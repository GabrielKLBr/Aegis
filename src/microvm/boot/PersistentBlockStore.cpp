#include "PersistentBlockStore.h"
#include <cstring>
#include "../state/StorageBudget.h"
#include <stdexcept>
namespace av::microvm::boot {
namespace {
constexpr uint64_t Magic=0x314C564F415647ull,RecordMagic=0x315243415647ull,MaxRecords=4096;
struct Header {uint64_t magic,baseHash;uint32_t version,bytes;uint64_t reserved[61];};
struct Record {uint64_t magic,sequence;uint32_t sector,bytes;uint64_t checksum;uint8_t data[512];};
static_assert(sizeof(Header)==512 && sizeof(Record)==544);
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool Transfer(HANDLE h,uint64_t offset,void* data,DWORD bytes,bool write) noexcept {
    LARGE_INTEGER position{};position.QuadPart=static_cast<LONGLONG>(offset);DWORD actual=0;
    return SetFilePointerEx(h,position,nullptr,FILE_BEGIN) && (write?WriteFile(h,data,bytes,&actual,nullptr):ReadFile(h,data,bytes,&actual,nullptr)) && actual==bytes;
}
uint64_t Size(HANDLE h){LARGE_INTEGER value{};Require(GetFileSizeEx(h,&value)!=0,"Cannot query volume size");Require(value.QuadPart>=0,"Invalid volume size");return static_cast<uint64_t>(value.QuadPart);}
bool Ordinary(HANDLE h) noexcept {BY_HANDLE_FILE_INFORMATION info{};return GetFileInformationByHandle(h,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && GetFileType(h)==FILE_TYPE_DISK;}
bool Path(const std::filesystem::path& path){const auto p=path.native();return path.is_absolute() && p.size()>3 && ((p[0]>=L'A' && p[0]<=L'Z') || (p[0]>=L'a' && p[0]<=L'z')) && p[1]==L':' && (p[2]==L'\\' || p[2]==L'/') && path.has_filename();}
}
PersistentBlockStore::PersistentBlockStore(const StorageOptions& o):options_(o){
    Require(o.overlayLimitBytes>=512 && o.overlayLimitBytes<=512+MaxRecords*sizeof(Record),"Overlay quota outside supported bounds");
    using guardian::storage::Hash;
    Require(Path(o.base) && Path(o.overlay) && o.base.lexically_normal()!=o.overlay.lexically_normal(),"Volume needs distinct absolute ordinary file paths");
    Require(o.overlayLimitBytes>=state::AllocationUnit(o.overlay.parent_path()),"Overlay limit is smaller than one host allocation unit");
    if(o.createIfMissing){
        File fresh;fresh.handle=CreateFileW(o.base.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(fresh.handle!=INVALID_HANDLE_VALUE){Require(Ordinary(fresh.handle),"Base must be an ordinary file");Require(Transfer(fresh.handle,0,blocks_,sizeof(blocks_),true) && FlushFileBuffers(fresh.handle),"Cannot initialize base volume");}
        else Require(GetLastError()==ERROR_FILE_EXISTS || GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create base volume");
    }
    base_.handle=CreateFileW(o.base.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Require(base_.handle!=INVALID_HANDLE_VALUE && Ordinary(base_.handle) && Size(base_.handle)==sizeof(blocks_) && Transfer(base_.handle,0,blocks_,sizeof(blocks_),false),"Base volume rejected");
    const auto fingerprint=Hash(blocks_,sizeof(blocks_));
    bool created=false;
    overlay_.handle=CreateFileW(o.overlay.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(overlay_.handle!=INVALID_HANDLE_VALUE){created=true;}else{
        Require(GetLastError()==ERROR_FILE_EXISTS || GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create overlay");
        overlay_.handle=CreateFileW(o.overlay.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    }
    Require(overlay_.handle!=INVALID_HANDLE_VALUE && Ordinary(overlay_.handle),"Overlay must be an ordinary exclusive file");
    Header h{};
    if(created){h.magic=Magic;h.baseHash=fingerprint;h.version=1;h.bytes=sizeof(h);Require(Transfer(overlay_.handle,0,&h,sizeof(h),true) && FlushFileBuffers(overlay_.handle),"Cannot initialize overlay");}
    const auto bytes=Size(overlay_.handle);Require(bytes<=o.overlayLimitBytes,"Existing overlay exceeds configured quota");Require(bytes>=sizeof(h) && bytes<=sizeof(h)+MaxRecords*sizeof(Record)+sizeof(Record)-1,"Overlay size rejected");
    Require(Transfer(overlay_.handle,0,&h,sizeof(h),false) && h.magic==Magic && h.version==1 && h.bytes==sizeof(h) && h.baseHash==fingerprint,"Overlay format or base fingerprint rejected");
    for(auto n:h.reserved)Require(!n,"Overlay reserved bits rejected");
    while(bytes-tail_>=sizeof(Record)){
        Record r{};Require(Transfer(overlay_.handle,tail_,&r,sizeof(r),false),"Cannot read overlay record");
        Require(r.magic==RecordMagic && r.sequence==sequence_+1 && r.sector<guardian::io::BlockBytes/512 && r.bytes==512 && r.checksum==Hash(&r,sizeof(r),offsetof(Record,checksum),sizeof(r.checksum)),"Corrupt overlay record");
        std::memcpy(blocks_+r.sector*512,r.data,512);++sequence_;tail_+=sizeof(r);
    }
    recoveredTail_=bytes-tail_;
    if(recoveredTail_){LARGE_INTEGER at{};at.QuadPart=static_cast<LONGLONG>(tail_);Require(SetFilePointerEx(overlay_.handle,at,nullptr,FILE_BEGIN) && SetEndOfFile(overlay_.handle) && FlushFileBuffers(overlay_.handle),"Cannot recover incomplete overlay tail");}
}
bool PersistentBlockStore::Read(uint64_t offset,uint8_t* out) noexcept {status_=guardian::storage::IoError;if(!out || (offset&511) || offset>=sizeof(blocks_))return false;std::memcpy(out,blocks_+offset,512);status_=0;return true;}
bool PersistentBlockStore::Write(uint64_t offset,const uint8_t* data) noexcept {
    status_=guardian::storage::IoError;
    if(failed_ || !data || (offset&511) || offset>=sizeof(blocks_) || budget_==0)return false;
    if(sequence_>=MaxRecords || Space().available<sizeof(Record)){status_=guardian::storage::NoSpace;return false;}
    Record r{};r.magic=RecordMagic;r.sequence=sequence_+1;r.sector=static_cast<uint32_t>(offset/512);r.bytes=512;std::memcpy(r.data,data,512);r.checksum=guardian::storage::Hash(&r,sizeof(r),offsetof(Record,checksum),sizeof(r.checksum));
    if(!Transfer(overlay_.handle,tail_,&r,sizeof(r),true)){const auto error=GetLastError();if(error==ERROR_DISK_FULL||error==ERROR_HANDLE_DISK_FULL)status_=guardian::storage::NoSpace;failed_=true;return false;}
    std::memcpy(blocks_+offset,data,512);++sequence_;tail_+=sizeof(r);if(budget_>0)--budget_;status_=0;return true;
}
bool PersistentBlockStore::Flush() noexcept {status_=guardian::storage::IoError;if(failed_)return false;if(!FlushFileBuffers(overlay_.handle)){failed_=true;return false;}status_=0;return true;}
std::vector<uint8_t> PersistentBlockStore::Snapshot(){
    Require(Flush(),"Cannot flush disk snapshot");std::vector<uint8_t> out(static_cast<size_t>(tail_));
    Require(Transfer(overlay_.handle,0,out.data(),static_cast<uint32_t>(out.size()),false),"Cannot capture disk snapshot");return out;
}
bool PersistentBlockStore::Matches(const std::vector<uint8_t>& bytes){return Snapshot()==bytes;}

}
