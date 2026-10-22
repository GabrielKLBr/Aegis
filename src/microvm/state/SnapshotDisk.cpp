#include "SnapshotDisk.h"
#include <algorithm>
#include <array>
#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace av::microvm::state {
namespace {
class Input {
public:
    explicit Input(const std::filesystem::path& p,uint64_t limit){
        Require(Ordinary(p),"Snapshot file type rejected");
#ifdef _WIN32
        file_.handle=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        Require(file_.handle!=INVALID_HANDLE_VALUE,"Cannot open snapshot");
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        Require(GetFileInformationByHandle(file_.handle,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&GetFileType(file_.handle)==FILE_TYPE_DISK&&GetFileSizeEx(file_.handle,&size)&&size.QuadPart>=0,"Snapshot handle rejected");
        size_=uint64_t(size.QuadPart);
#else
        file_.handle=::open(p.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        struct stat info{};Require(file_.handle>=0&&::fstat(file_.handle,&info)==0&&S_ISREG(info.st_mode)&&info.st_size>=0,"Snapshot handle rejected");size_=uint64_t(info.st_size);
#endif
        Require(size_>=24&&size_<=limit,"Checkpoint exceeds configured RAM bound");
        const auto magic=Value<uint64_t>(false),bytes=Value<uint64_t>(false);expected_=Value<uint64_t>(false);
        Require(magic==0x393050414e535647ull&&bytes==size_-24,"State format/length rejected");
    }
    template<class T>T Value(bool hash=true){T out{};Read(&out,sizeof(out),hash);return out;}
    void Read(void* raw,uint64_t bytes,bool hash=true){
        Require(bytes<=size_-position_,"Truncated checkpoint");auto* out=static_cast<uint8_t*>(raw);
        uint64_t done=0;
        while(done<bytes){const auto n=uint32_t(std::min<uint64_t>(bytes-done,65536));
#ifdef _WIN32
            DWORD actual=0;Require(::ReadFile(file_.handle,out+done,n,&actual,nullptr)&&actual==n,"Cannot read checkpoint");const auto count=n;
#else
            const auto actual=::pread(file_.handle,out+done,n,static_cast<off_t>(position_));
            if(actual<0&&errno==EINTR)continue;Require(actual>0,"Cannot read checkpoint");const auto count=uint32_t(actual);
#endif
            if(hash)for(uint32_t i=0;i<count;++i)hash_=(hash_^out[done+i])*1099511628211ull;
            position_+=count;done+=count;
        }
    }
    void Skip(uint64_t bytes,bool access=false){
        Require(bytes<=size_-position_,"Truncated checkpoint blob");
        while(bytes){const auto n=uint32_t(std::min<uint64_t>(bytes,buffer_.size()));Read(buffer_.data(),n);if(access)for(uint32_t i=0;i<n;++i){const auto a=buffer_[i];Require(a==0||a==1||a==3||a==5,"Invalid saved mapping");}bytes-=n;}
    }
    uint64_t Blob(uint64_t limit){const auto n=Value<uint64_t>();Require(n<=limit&&n<=size_-position_,"Checkpoint blob outside bounds");return n;}
    void End(){Require(position_==size_&&hash_==expected_,"State checksum/extent rejected");}
private:
    struct File {
#ifdef _WIN32
        HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
#else
        int handle=-1;~File(){if(handle>=0)::close(handle);}
#endif
    }file_;
    uint64_t size_=0,position_=0,expected_=0,hash_=14695981039346656037ull;
    std::array<uint8_t,65536> buffer_{};
};
}
std::vector<uint8_t> SnapshotDisk(const std::filesystem::path& p,uint32_t ramMiB,uint64_t imageHash,uint64_t scalableGrowth){
    Require(ramMiB>=16&&ramMiB<=1024,"Checkpoint RAM bound rejected");
    Require(!scalableGrowth||(scalableGrowth>=(4ull<<20)&&scalableGrowth<=(256ull<<20)&&!(scalableGrowth%65536)),"Checkpoint disk bound rejected");
    Input r(p,(uint64_t(ramMiB)<<20)+(scalableGrowth?scalableGrowth+(16ull<<20):(32ull<<20)));
    Require(r.Value<uint32_t>()==1,"Snapshot version rejected");
    const auto backend=r.Value<uint32_t>(),version=r.Value<uint32_t>();(void)r.Value<uint64_t>();
    Require((backend==1||backend==2)&&version==1,"Snapshot machine version rejected");
    const auto ram=r.Blob(1ull<<30);Require(ram==(uint64_t(ramMiB)<<20),"Snapshot RAM mismatch");r.Skip(ram);
    const auto access=r.Blob(262144);Require(access==ram/4096,"Snapshot mappings mismatch");r.Skip(access,true);
    const auto cpu=r.Blob(scalableGrowth?(512ull<<10):(128ull<<10));Require(cpu!=0,"Snapshot processor missing");r.Skip(cpu);
    const auto metadata=r.Blob(32ull<<20);Require(metadata>=24,"Snapshot metadata missing");
    const auto boot=r.Value<uint64_t>(),savedRam=r.Value<uint64_t>(),image=r.Value<uint64_t>();
    Require(boot&&savedRam==ram&&image==imageHash,"Snapshot identity mismatch");r.Skip(metadata-24);
    const auto disk=r.Blob(scalableGrowth?scalableGrowth:(3ull<<20));std::vector<uint8_t> out(static_cast<size_t>(disk));
    if(disk)r.Read(out.data(),disk);r.End();return out;
}
}
