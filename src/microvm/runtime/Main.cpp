#include "Unified.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    using namespace av::scan;fs::path input,registry,output,restore;uint64_t checkpoint=0;uint32_t milliseconds=15000;guardian::telemetry::Policy policy;policy.eventLimit=guardian::telemetry::MaxEvents;
    for(int i=1;i<argc;++i){const std::wstring key=argv[i];Require(i+1<argc,"Runtime option requires a value");const std::wstring value=argv[++i];if(key==L"--input")input=value;else if(key==L"--runtime-root")registry=value;else if(key==L"--output")output=value;else if(key==L"--restore")restore=value;else if(key==L"--checkpoint-after")checkpoint=std::stoull(value);else if(key==L"--milliseconds")milliseconds=uint32_t(std::stoul(value));else if(key==L"--deny")policy.denyMask=uint32_t(std::stoul(value));else throw std::runtime_error("Runtime option rejected");}
    Require(!input.empty()&&!output.empty(),"Use --input --runtime-root --output");const auto result=av::microvm::runtime::Run(input,registry,output,policy,milliseconds,checkpoint,restore);std::cout<<result.dump()<<'\n';return result.value("complete",false)||result.value("checkpointCreated",false)?0:3;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
