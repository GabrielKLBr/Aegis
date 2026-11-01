#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <vector>
// The root contains one entry point; SDK resource directories remain in runtime/.
namespace {
std::wstring Quote(const std::wstring& value){
    std::wstring out=L"\"";size_t slashes=0;
    for(auto c:value){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}
    out.append(slashes*2,L'\\');return out+L"\"";
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n==32768)return ERROR_BAD_PATHNAME;
    const auto exe=std::filesystem::path(path).parent_path()/L"runtime"/L"Aegis.exe";
    int count=0;auto** args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return int(GetLastError());
    auto command=Quote(exe.wstring());for(int i=1;i<count;++i)command+=L" "+Quote(args[i]);LocalFree(args);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&startup,&process)){
        const auto error=GetLastError();MessageBoxW(nullptr,L"Extract the complete portable ZIP before opening Aegis.",L"Aegis",MB_OK|MB_ICONERROR);return int(error);
    }
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);return int(code);
}
