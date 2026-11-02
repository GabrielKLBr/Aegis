#include "services/Services.h"
#include "distribution/Distribution.h"
#include <fstream>
#include <iostream>
// Development-only process termination points; this executable is not packaged.
int wmain(int argc,wchar_t** argv){try{
    if(argc==4&&std::wstring(argv[1])==L"--definitions-test"){SetEnvironmentVariableW(L"AEGIS_DEFINITIONS_FAIL_AT",argv[3]);return av::services::InstallDefinitions(argv[2],false);}
    if(argc==3&&std::wstring(argv[1])==L"--metadata-test"){
        namespace d=aegis::distribution;const auto root=d::fs::absolute(argv[2]);d::Require(!d::fs::exists(root),"Fresh metadata test root required");d::CreateDirectorySecure(root,true);d::AtomicJson(root/L"state.json",{{"schema",1},{"inert",true}});const auto source=root.parent_path()/L"metadata-source.txt";{std::ofstream file(source);file<<"Inert protected-copy fixture";}d::CopyMember(source,root/L"copied.txt");std::cout<<"Protected metadata fixture created\n";return 0;
    }
    av::services::Check(argc==4,"Use SIGNED_ENGINES OWNER_SID CHECKPOINT");SetEnvironmentVariableW(L"AEGIS_DEPLOYMENT_TEST_SOURCE",argv[1]);SetEnvironmentVariableW(L"AEGIS_DEPLOYMENT_FAIL_AT",argv[3]);return av::services::Install(argv[2],false);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
