#include "Distribution.h"
#include <iostream>
using namespace aegis::distribution;
int wmain(int argc,wchar_t** argv){try{SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);Require(argc>=2,"Use prerequisites, verify ROOT KIND, status ROOT or rollback ROOT");const auto action=std::wstring(argv[1]);Json result;
    if(action==L"prerequisites"&&argc==2)result=Prerequisites();
    else if(action==L"verify"&&argc==4){auto tree=Verify(argv[2],Utf8(argv[3]),false);result={{"complete",true},{"identity",tree.identity},{"manifest",tree.manifest}};}
    else if(action==L"status"&&argc==3)result=DeploymentStatus(argv[2]);
    else if(action==L"rollback"&&argc==3)result=Rollback(argv[2]);
    else if(action==L"update"&&argc==4)result=ApplyUpdate(argv[2],argv[3]);
    else throw std::runtime_error("Invalid distribution command");std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<Json({{"complete",false},{"error",e.what()}}).dump()<<'\n';return 1;}}
