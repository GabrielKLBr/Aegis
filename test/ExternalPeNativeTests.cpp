#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)throw std::runtime_error("Owned fixture directory required");const auto root=std::filesystem::absolute(argv[1]);
    const auto leaf=LoadLibraryExW((root/L"guardian_leaf.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!leaf)throw std::runtime_error("Owned native leaf failed");
    const auto ordinal=reinterpret_cast<uint32_t(*)()>(GetProcAddress(leaf,MAKEINTRESOURCEA(7)));
    const auto data=reinterpret_cast<const uint32_t*>(GetProcAddress(leaf,"ProbeData"));
    if(!ordinal||ordinal()!=0x43||!data||*data!=0x12345678)throw std::runtime_error("Native TLS/ordinal/data mismatch");
    const auto bridge=LoadLibraryExW((root/L"guardian-bridge.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto forward=reinterpret_cast<uint32_t(*)()>(GetProcAddress(bridge,"ForwardValue"));
    if(!bridge||!forward||forward()!=0x43||GetProcAddress(bridge,"ForwardData")!=reinterpret_cast<FARPROC>(const_cast<uint32_t*>(data)))throw std::runtime_error("Native forwarder mismatch");
    if(LoadLibraryExW((root/L"guardian-reject.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))throw std::runtime_error("Rejected DLL was accepted natively");
    const auto tracker=GetModuleHandleW(L"guardian-tracker.dll");const auto count=reinterpret_cast<uint32_t(*)(uint32_t)>(GetProcAddress(tracker,"RecordCount"));
    if(!count||count(11)!=1||count(21)!=1||count(61)!=1||count(60)!=1)throw std::runtime_error("Native DLL init/failure callback mismatch");
    if(GetModuleHandleW(L"guardian-late.dll"))throw std::runtime_error("Unused forwarder target loaded eagerly");
    const auto late=reinterpret_cast<uint32_t(*)()>(GetProcAddress(bridge,"ForwardLate"));
    if(!late||late()!=0x7777||count(101)!=1)throw std::runtime_error("Native late forwarder initialization mismatch");
    if(GetProcAddress(bridge,"ForwardRejected")||GetModuleHandleW(L"guardian-reject.dll")||count(61)!=2||count(60)!=2)throw std::runtime_error("Native forwarded initialization rollback mismatch");
    if(!FreeLibrary(bridge)||!FreeLibrary(leaf))throw std::runtime_error("Native release failed");
    std::cout<<"Owned Windows reference: TLS, ordinals, data, forwarders and failed DllMain detach PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
