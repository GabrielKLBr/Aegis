#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cwchar>
#include "MemoryHost.h"
int wmain(int argc,wchar_t** argv) {
    if(argc!=6)return ERROR_INVALID_PARAMETER;
    uint64_t arguments[5]{};
    for(int i=0;i<5;++i){wchar_t* end=nullptr;arguments[i]=std::wcstoull(argv[i+1],&end,10);if(!end || *end || !arguments[i])return ERROR_INVALID_PARAMETER;}
    const auto bytes=arguments[4];if((bytes&4095) || bytes>(1ull<<30))return ERROR_INVALID_PARAMETER;
    const auto mapping=reinterpret_cast<HANDLE>(arguments[0]),ready=reinterpret_cast<HANDLE>(arguments[1]),
               stop=reinterpret_cast<HANDLE>(arguments[2]),owner=reinterpret_cast<HANDLE>(arguments[3]);
    auto* view=static_cast<uint8_t*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,static_cast<SIZE_T>(bytes+av::microvm::MemoryHostPage)));
    if(!view)return static_cast<int>(GetLastError());
    auto* header=reinterpret_cast<av::microvm::MemoryHostHeader*>(view);
    if(header->magic!=av::microvm::MemoryHostMagic || header->bytes!=bytes){UnmapViewOfFile(view);return ERROR_INVALID_DATA;}
    header->address=reinterpret_cast<uint64_t>(view+av::microvm::MemoryHostPage);
    MemoryBarrier();const bool signaled=SetEvent(ready)!=0;
    if(signaled){const HANDLE exits[]{stop,owner};WaitForMultipleObjects(2,exits,FALSE,INFINITE);}
    UnmapViewOfFile(view);return signaled?0:ERROR_INVALID_HANDLE;
}
