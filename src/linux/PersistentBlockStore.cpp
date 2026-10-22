#include "../microvm/boot/PersistentBlockStore.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <cstring>
#include "../microvm/state/StorageBudget.h"
#include <stdexcept>
namespace av::microvm::boot {
namespace {
constexpr uint64_t Magic=0x314C564F415647ull,RecordMagic=0x315243415647ull,MaxRecords=4096;
struct Header {uint64_t magic,baseHash;uint32_t version,bytes;uint64_t reserved[61];};
struct Record {uint64_t magic,sequence;uint32_t sector,bytes;uint64_t checksum;uint8_t data[512];};
static_assert(sizeof(Header)==512 && sizeof(Record)==544);
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool Transfer(int fd,uint64_t offset,void* data,uint32_t bytes,bool write) noexcept {
    auto* p=static_cast<uint8_t*>(data);uint32_t done=0;
    while(done<bytes){const auto n=write?::pwrite(fd,p+done,bytes-done,offset+done) : ::pread(fd,p+done,bytes-done,offset+done);
        if(n<0 && errno==EINTR)continue;if(n<=0)return false;done+=static_cast<uint32_t>(n);}return true;
}
uint64_t Size(int fd){struct stat s{};Require(fstat(fd,&s)==0 && s.st_size>=0,"Cannot query volume size");return static_cast<uint64_t>(s.st_size);}
bool Ordinary(int fd) noexcept {struct stat s{};return fd>=0 && fstat(fd,&s)==0 && S_ISREG(s.st_mode);}
bool Path(const std::filesystem::path& path){return path.is_absolute() && path.has_filename();}

}

PersistentBlockStore::PersistentBlockStore(const StorageOptions& o):options_(o){
    Require(o.overlayLimitBytes>=512 && o.overlayLimitBytes<=512+MaxRecords*sizeof(Record),"Overlay quota outside supported bounds");
    using guardian::storage::Hash;
    Require(Path(o.base) && Path(o.overlay) && o.base.lexically_normal()!=o.overlay.lexically_normal(),"Volume needs distinct absolute ordinary file paths");
    Require(o.overlayLimitBytes>=state::AllocationUnit(o.overlay.parent_path()),"Overlay limit is smaller than one host allocation unit");
    if(o.createIfMissing){
        File fresh;fresh.handle=::open(o.base.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
        if(fresh.handle>=0){Require(Ordinary(fresh.handle),"Base must be ordinary");Require(Transfer(fresh.handle,0,blocks_,sizeof(blocks_),true) && ::fsync(fresh.handle)==0,"Cannot initialize base");}
        else Require(errno==EEXIST,"Cannot create base volume");
    }
    base_.handle=::open(o.base.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    Require(Ordinary(base_.handle) && ::flock(base_.handle,LOCK_SH|LOCK_NB)==0 && Size(base_.handle)==sizeof(blocks_) && Transfer(base_.handle,0,blocks_,sizeof(blocks_),false),"Base volume rejected");
    const auto fingerprint=Hash(blocks_,sizeof(blocks_));bool created=false;
    overlay_.handle=::open(o.overlay.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
    if(overlay_.handle>=0)created=true;
    else {Require(errno==EEXIST,"Cannot create overlay");overlay_.handle=::open(o.overlay.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);}
    Require(Ordinary(overlay_.handle),"Overlay must be ordinary");
    struct stat bs{},os{};Require(::fstat(base_.handle,&bs)==0 && ::fstat(overlay_.handle,&os)==0 &&
        (bs.st_dev!=os.st_dev || bs.st_ino!=os.st_ino),"Base and overlay must be distinct files");
    Require(::flock(overlay_.handle,LOCK_EX|LOCK_NB)==0,"Overlay is already in use");
    Header h{};
    if(created){h.magic=Magic;h.baseHash=fingerprint;h.version=1;h.bytes=sizeof(h);Require(Transfer(overlay_.handle,0,&h,sizeof(h),true) && (::fsync(overlay_.handle)==0),"Cannot initialize overlay");}
    const auto bytes=Size(overlay_.handle);Require(bytes<=o.overlayLimitBytes,"Existing overlay exceeds configured quota");Require(bytes>=sizeof(h) && bytes<=sizeof(h)+MaxRecords*sizeof(Record)+sizeof(Record)-1,"Overlay size rejected");
    Require(Transfer(overlay_.handle,0,&h,sizeof(h),false) && h.magic==Magic && h.version==1 && h.bytes==sizeof(h) && h.baseHash==fingerprint,"Overlay format or base fingerprint rejected");
    for(auto n:h.reserved)Require(!n,"Overlay reserved bits rejected");
    while(bytes-tail_>=sizeof(Record)){
        Record r{};Require(Transfer(overlay_.handle,tail_,&r,sizeof(r),false),"Cannot read overlay record");
        Require(r.magic==RecordMagic && r.sequence==sequence_+1 && r.sector<guardian::io::BlockBytes/512 && r.bytes==512 && r.checksum==Hash(&r,sizeof(r),offsetof(Record,checksum),sizeof(r.checksum)),"Corrupt overlay record");
        std::memcpy(blocks_+r.sector*512,r.data,512);++sequence_;tail_+=sizeof(r);
    }
    recoveredTail_=bytes-tail_;
    if(recoveredTail_)Require(::ftruncate(overlay_.handle,static_cast<off_t>(tail_))==0 && ::fsync(overlay_.handle)==0,"Cannot recover incomplete overlay tail");
}
bool PersistentBlockStore::Read(uint64_t offset,uint8_t* out) noexcept {status_=guardian::storage::IoError;if(!out || (offset&511) || offset>=sizeof(blocks_))return false;std::memcpy(out,blocks_+offset,512);status_=0;return true;}
bool PersistentBlockStore::Write(uint64_t offset,const uint8_t* data) noexcept {
    status_=guardian::storage::IoError;
    if(failed_ || !data || (offset&511) || offset>=sizeof(blocks_) || budget_==0)return false;
    if(sequence_>=MaxRecords || Space().available<sizeof(Record)){status_=guardian::storage::NoSpace;return false;}
    Record r{};r.magic=RecordMagic;r.sequence=sequence_+1;r.sector=static_cast<uint32_t>(offset/512);r.bytes=512;std::memcpy(r.data,data,512);r.checksum=guardian::storage::Hash(&r,sizeof(r),offsetof(Record,checksum),sizeof(r.checksum));
    if(!Transfer(overlay_.handle,tail_,&r,sizeof(r),true)){if(errno==ENOSPC||errno==EDQUOT)status_=guardian::storage::NoSpace;failed_=true;return false;}
    std::memcpy(blocks_+offset,data,512);++sequence_;tail_+=sizeof(r);if(budget_>0)--budget_;status_=0;return true;
}
bool PersistentBlockStore::Flush() noexcept {status_=guardian::storage::IoError;if(failed_)return false;if(!(::fsync(overlay_.handle)==0)){failed_=true;return false;}status_=0;return true;}
std::vector<uint8_t> PersistentBlockStore::Snapshot(){
    Require(Flush(),"Cannot flush disk snapshot");std::vector<uint8_t> out(static_cast<size_t>(tail_));
    Require(Transfer(overlay_.handle,0,out.data(),static_cast<uint32_t>(out.size()),false),"Cannot capture disk snapshot");return out;
}
bool PersistentBlockStore::Matches(const std::vector<uint8_t>& bytes){return Snapshot()==bytes;}

}
