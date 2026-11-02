#include "Distribution.h"
#include <WinHvPlatform.h>
namespace aegis::distribution {
Json Prerequisites(){
    SYSTEM_INFO system{};GetNativeSystemInfo(&system);OSVERSIONINFOW os{};os.dwOSVersionInfoSize=sizeof(os);
    using Version=LONG(WINAPI*)(OSVERSIONINFOW*);auto version=reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));const bool versionKnown=version&&version(&os)>=0;
    const bool architecture=system.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64;
    const bool windows=versionKnown&&os.dwMajorVersion>=10&&os.dwBuildNumber>=19041;
    const bool nx=IsProcessorFeaturePresent(PF_NX_ENABLED)!=FALSE;
    auto whp=LoadLibraryExW(L"WinHvPlatform.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);bool hypervisor=false;
    if(whp){auto query=reinterpret_cast<decltype(&WHvGetCapability)>(GetProcAddress(whp,"WHvGetCapability"));WHV_CAPABILITY capability{};UINT32 written=0;if(query&&SUCCEEDED(query(WHvCapabilityCodeHypervisorPresent,&capability,sizeof(capability),&written))&&written>=sizeof(BOOL))hypervisor=capability.HypervisorPresent!=FALSE;FreeLibrary(whp);}
    const bool antivirus=windows&&architecture;Json issues=Json::array();if(!architecture)issues.push_back("An x64 Windows host is required");if(!windows)issues.push_back("Windows 10 build 19041 or later is required");if(!nx)issues.push_back("Enable NX/DEP in the firmware for MiniNT");if(!hypervisor)issues.push_back("MiniNT needs the running Windows hypervisor and Windows Hypervisor Platform; antivirus scanning remains independent");
    return {{"schema",1},{"antivirusReady",antivirus},{"sandboxReady",antivirus&&nx&&hypervisor},{"x64",architecture},{"windowsBuild",os.dwBuildNumber},{"nx",nx},{"hypervisor",hypervisor},{"issues",issues},{"automaticSystemChanges",false}};
}
}
