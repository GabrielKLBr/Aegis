#include "Win32Api.h"
using namespace guardian;using namespace guardian::win;
namespace {
const uint16_t* W(const wchar_t* p){return reinterpret_cast<const uint16_t*>(p);}Handle Invalid(){return reinterpret_cast<Handle>(UINT64_MAX);}
volatile uint32_t callbacks=0,ioCalls=0;Dword tls=0;
Handle gate=nullptr;
void BlockPool(void*,void*,void*){WaitForSingleObject(gate,UINT32_MAX);}
Dword Worker(void*){if(TlsGetValue(tls)||!TlsSetValue(tls,reinterpret_cast<void*>(77)))return 91;return 7;}
void WorkCallback(void*,void*,void*){if(!TlsGetValue(tls))++callbacks;}
void IoDone(Dword error,Dword bytes,Overlapped*){if(!error&&bytes==8)++ioCalls;}
int Run(){
    native::VersionInfo version;if(!GetVersionExW(&version)||!version.build)return 1;SystemInfo system{};GetSystemInfo(&system);MemoryStatus memory;
    if(system.architecture!=9||system.page!=4096||system.processors!=1||!GlobalMemoryStatusEx(&memory)||memory.total!=(256ull<<20)||memory.available>memory.total)return 2;
    uint16_t unicode[32]{};char utf8[32]{};const char input[]="a\xC3\xA7\xC3\xA3o \xF0\x9F\x99\x82";
    if(MultiByteToWideChar(65001,8,input,-1,unicode,32)!=8||WideCharToMultiByte(65001,0x80,unicode,-1,utf8,32,nullptr,nullptr)!=12)return 3;
    for(uint32_t i=0;i<sizeof(input);++i)if(input[i]!=utf8[i])return 4;
    const char bad[]{char(-64),char(-81)};if(MultiByteToWideChar(65001,8,bad,2,unicode,32)||GetLastError()!=1113||CompareStringOrdinal(W(L"ação"),-1,W(L"AÇÃO"),-1,1)!=2||LCMapStringEx(nullptr,0x200,W(L"ação"),-1,unicode,32,nullptr,nullptr,0)!=5)return 5;
    if(LCMapStringEx(nullptr,0x200,W(L"α"),-1,unicode,32,nullptr,nullptr,0)||GetLastError()!=50)return 42;
    if(!SetEnvironmentVariableW(W(L"AEGIS_PHASE"),W(L"ação"))||GetEnvironmentVariableW(W(L"aegis_phase"),unicode,32)!=4||CompareStringOrdinal(unicode,-1,W(L"ação"),-1,0)!=2)return 6;
    auto* environment=GetEnvironmentStringsW();if(!environment||!FreeEnvironmentStringsW(environment))return 7;
    uint16_t path[128]{};if(!GetFullPathNameW(W(L".\\probe\\..\\phase14-4.bin"),128,path,nullptr)||CompareStringOrdinal(path,-1,W(L"C:\\App\\phase14-4.bin"),-1,1)!=2)return 8;
    if(SetCurrentDirectoryW(W(L"C:\\missing"))||!SetCurrentDirectoryW(W(L"C:\\App")))return 9;
    void* block=VirtualAlloc(nullptr,8192,0x3000,4);if(!block)return 10;memory::Information info{};Dword old=0;
    if(!VirtualQuery(block,&info,sizeof(info))||info.protect!=4||!VirtualProtect(block,8192,2,&old)||old!=4||!VirtualFree(block,0,0x8000))return 11;
    auto* allocation=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),8,256));if(!allocation||allocation[255]||HeapSize(GetProcessHeap(),0,allocation)!=256||!HeapFree(GetProcessHeap(),0,allocation))return 12;
    tls=TlsAlloc();if(tls==UINT32_MAX||!TlsSetValue(tls,reinterpret_cast<void*>(55)))return 13;auto thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);Dword code=0;
    if(!thread||WaitForSingleObject(thread,10000)||!GetExitCodeThread(thread,&code)||code!=7||!CloseHandle(thread)||TlsGetValue(tls)!=reinterpret_cast<void*>(55))return 14;
    auto* work=CreateThreadpoolWork(WorkCallback,nullptr,nullptr);if(!work)return 15;for(uint32_t i=0;i<3;++i)SubmitThreadpoolWork(work);WaitForThreadpoolWorkCallbacks(work,0);CloseThreadpoolWork(work);if(callbacks!=3||!TlsFree(tls))return 16;
    Handle events[2]{CreateEventW(nullptr,1,0,nullptr),CreateEventW(nullptr,1,0,nullptr)};if(!events[0]||!events[1]||!SetEvent(events[1])||WaitForMultipleObjects(2,events,0,0)!=1||!CloseHandle(events[0])||!CloseHandle(events[1]))return 17;
    auto file=CreateFileW(path,0xC0000000,7,nullptr,2,0x80,nullptr);if(file==Invalid())return 18;uint64_t value=0x1020304050607080ull,read=0;Dword count=0;
    if(!WriteFile(file,&value,8,&count,nullptr)||count!=8||!SetFilePointerEx(file,0,nullptr,0)||!ReadFile(file,&read,8,&count,nullptr)||read!=value||!FlushFileBuffers(file))return 19;
    auto mapping=CreateFileMappingW(file,nullptr,4,0,0,nullptr);if(!mapping)return 20;auto* view=static_cast<uint64_t*>(MapViewOfFile(mapping,2,0,0,0));if(!view||*view!=value)return 21;
    *view=0xDEADBEEF;if(!CloseHandle(mapping)||!FlushViewOfFile(view,8)||!UnmapViewOfFile(view)||!SetFilePointerEx(file,0,nullptr,0)||!ReadFile(file,&read,8,&count,nullptr)||read!=0xDEADBEEF||!CloseHandle(file))return 22;
    file=CreateFileW(path,0xC0000000,7,nullptr,3,0x40000080,nullptr);if(file==Invalid())return 23;Overlapped ov;ov.event=CreateEventW(nullptr,1,0,nullptr);
    if(!ov.event||ReadFile(file,&read,8,&count,&ov)||GetLastError()!=997||!GetOverlappedResult(file,&ov,&count,1)||count!=8||read!=0xDEADBEEF||!CloseHandle(ov.event))return 24;
    ov={};if(!ReadFileEx(file,&read,8,&ov,IoDone))return 25;for(uint32_t i=0;i<100&&!ioCalls;++i)SleepEx(10,1);if(ioCalls!=1||!CloseHandle(file))return 26;
    file=CreateFileW(path,0x80000000,7,nullptr,3,0x40000080,nullptr);gate=CreateEventW(nullptr,1,0,nullptr);work=CreateThreadpoolWork(BlockPool,nullptr,nullptr);if(file==Invalid()||!gate||!work)return 37;
    SubmitThreadpoolWork(work);ov={};ov.event=CreateEventW(nullptr,1,0,nullptr);if(!ov.event||ReadFile(file,&read,8,&count,&ov)||GetLastError()!=997||!CancelIoEx(file,&ov)||!SetEvent(gate))return 38;
    if(GetOverlappedResult(file,&ov,&count,1)||GetLastError()!=995||count)return 39;WaitForThreadpoolWorkCallbacks(work,0);CloseThreadpoolWork(work);if(!CloseHandle(gate)||!CloseHandle(ov.event)||!CloseHandle(file))return 40;
    file=CreateFileW(path,0x80000000,7,nullptr,3,0x80,nullptr);uint8_t large[1024]{};if(file==Invalid()||!ReadFile(file,large,sizeof(large),&count,nullptr)||count!=8||WriteFile(file,&read,8,&count,nullptr)||GetLastError()!=5||!CloseHandle(file))return 41;
    StartupInfoA startup{};startup.cb=sizeof(startup);ProcessInformation child{};uint16_t command[]{'"','C',':','\\','A','p','p','\\','g','u','a','r','d','i','a','n','-','w','i','n','3','2','-','c','h','i','l','d','.','e','x','e','"',' ','-','-','c','h','i','l','d',0};
    if(!CreateProcessW(W(L"C:\\App\\guardian-win32-child.exe"),command,nullptr,nullptr,0,4,nullptr,nullptr,&startup,&child)||child.processId==GetCurrentProcessId()||WaitForSingleObject(child.process,0)!=0x102||ResumeThread(child.thread)!=1)return 27;
    if(WaitForSingleObject(child.process,10000)||!GetExitCodeProcess(child.process,&code)||code!=37||!CloseHandle(child.thread)||!CloseHandle(child.process))return 28;
    if(GetEnvironmentVariableW(W(L"AEGIS_PHASE"),unicode,32)!=4||CompareStringOrdinal(unicode,-1,W(L"ação"),-1,0)!=2)return 29;
    char missing[]="missing";if(CreateProcessA("missing.exe",missing,nullptr,nullptr,0,0,nullptr,nullptr,&startup,&child)||child.process||child.thread)return 30;
    const auto resource=FindResourceA(nullptr,reinterpret_cast<const char*>(101),reinterpret_cast<const char*>(10));const auto data=LoadResource(nullptr,resource);
    if(!resource||SizeofResource(nullptr,resource)!=4||!data||*static_cast<const uint16_t*>(LockResource(data))!=0x1434)return 31;
    auto module=LoadLibraryW(W(L"guardian-win32-plugin.dll"));if(!module)return 32;auto function=GetProcAddress(module,"Win32PluginProbe");if(!function||reinterpret_cast<Dword(*)()>(function)()!=GetCurrentProcessId()||!FreeLibrary(module))return 33;
    auto kernel=GetModuleHandleA("kernel32.dll");const auto precise=GetProcAddress(kernel,"GetSystemTimePreciseAsFileTime");if((version.major==6&&precise)||(version.major==10&&!precise))return 34;
    Dword caught=0;__try {RaiseException(0xE0144001,0,0,nullptr);}__except(1){caught=1;}if(!caught)return 35;
    if(!GetModuleFileNameW(nullptr,path,128)||!DeleteFileW(W(L"C:\\App\\phase14-4.bin")))return 36;return 0;
}
}
extern "C" int GuardianEntry(){const auto code=Run();Dword wrote=0;if(!code){const char text[]="Win32 14.4 console threads pool child files mappings async Unicode API sets SEH PASS\n";WriteFile(GetStdHandle(Dword(-11)),text,sizeof(text)-1,&wrote,nullptr);}else {char text[]="Win32 14.4 failure 00\n";text[19]=char('0'+code/10);text[20]=char('0'+code%10);WriteFile(GetStdHandle(Dword(-11)),text,sizeof(text)-1,&wrote,nullptr);}return code;}
const void* volatile win32RelocationAnchor=reinterpret_cast<const void*>(GuardianEntry);
