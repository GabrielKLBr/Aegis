#include "Services.h"
#include "distribution/Distribution.h"
#include "scanner/Scanner.h"
#include <shlobj.h>
#include <fstream>
namespace av::services {
namespace {
namespace d=aegis::distribution;
fs::path Root(){PWSTR folder=nullptr;Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&folder)),"Locate installed engines");const auto root=fs::path(folder)/L"AegisEngineServices";CoTaskMemFree(folder);d::Ordinary(root);return root;}
void Validate(const d::VerifiedTree& tree){
    const auto& files=tree.manifest.at("files");Check(files.size()==2&&files.contains("signatures.txt")&&files.contains("rules.json"),"Definition bundle must contain signatures.txt and rules.json only");
    const auto signatures=scan::SignatureRules(tree.root/L"signatures.txt");const auto rules=scan::ReadJson(tree.root/L"rules.json",128u<<10);scan::Limits limits;const std::string inert="Aegis definition validation";
    const auto report=scan::Inspect({reinterpret_cast<const uint8_t*>(inert.data()),inert.size()},"validation.txt",rules,signatures,limits);Check(report.value("complete",false),"Definition compilation/validation failed");
}
void Name(const std::string& name){
    const bool set=name.starts_with("definitions/set-")&&name.size()==80&&name.substr(16).find_first_not_of("0123456789abcdef")==name.npos;
    const bool bundled=name.starts_with("install-")&&name.ends_with("/definitions")&&name.size()>20&&name.substr(8,name.size()-20).find_first_not_of("0123456789-")==name.npos;
    Check(set||bundled,"Invalid active definition path");
}
Json InitialState(const fs::path& root){
    const auto deployment=d::ReadJson(root/L"deployment.state.json");const auto slot=deployment.at("current").at("slot").get<std::string>();const auto name=slot+"/definitions";Name(name);auto baseline=d::Verify(root/Wide(name),"definitions");
    return {{"schema",1},{"active",name},{"previous",""},{"highestSequence",baseline.manifest.at("sequence")}};
}
void Checkpoint(const char* phase){
#ifdef AEGIS_DEPLOYMENT_FAULT_TEST
    wchar_t requested[128]{};GetEnvironmentVariableW(L"AEGIS_DEFINITIONS_FAIL_AT",requested,128);if(Wide(phase)==requested)TerminateProcess(GetCurrentProcess(),77);
#else
    (void)phase;
#endif
}
}
fs::path ActiveDefinitions(){
    const auto root=Executable().parent_path().parent_path();
    if(fs::exists(root/L"definitions.active.json")){const auto state=d::ReadJson(root/L"definitions.active.json");Check(state.at("schema")==1,"Definition selection schema");const auto active=state.at("active").get<std::string>();Name(active);return root/Wide(active);}
    return Executable().parent_path()/L"definitions";
}
int InstallDefinitions(const fs::path& source,bool rollback){
    const auto root=Root();Check(Query(Kind::Antivirus).installed,"Install engine services before updating definitions");
    Handle lock(CreateFileW((root/L"definitions.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(lock.value!=INVALID_HANDLE_VALUE,"Another definition update is running");d::Ordinary(root/L"definitions.lock");ProtectDeploymentTree(root/L"definitions.lock");
    auto state=fs::exists(root/L"definitions.active.json")?d::ReadJson(root/L"definitions.active.json"):InitialState(root);
    if(rollback){Check(state.at("previous")!="","No prior definition bundle");const auto previous=state.at("previous").get<std::string>();Name(previous);auto verified=d::Verify(root/Wide(previous),"definitions");Validate(verified);auto old=state.at("active");state["active"]=previous;state["previous"]=old;d::AtomicJson(root/L"definitions.active.json",state);ProtectDeploymentTree(root/L"definitions.active.json");return 0;}
    auto trusted=d::Verify(source,"definitions");Validate(trusted);const auto selected=state.at("active").get<std::string>();Name(selected);auto current=d::Verify(root/Wide(selected),"definitions");
    Check(trusted.manifest.at("configuration")==current.manifest.at("configuration"),"Definition configuration mismatch");
    const auto sequence=trusted.manifest.at("sequence").get<uint64_t>(),highest=state.at("highestSequence").get<uint64_t>();
    if(sequence==highest&&trusted.identity==current.identity)return 0; // Repair of the already selected, verified bundle is a no-op.
    Check(sequence>highest,"Definition downgrade/replay refused");
    const auto name="definitions/set-"+trusted.identity;const auto target=root/Wide(name);d::CreateDirectorySecure(root/L"definitions");ProtectDeploymentTree(root/L"definitions");
    size_t retained=0;for(const auto& entry:fs::directory_iterator(root/L"definitions")){(void)entry;++retained;}Check(retained<128,"Definition storage limit reached; remove inactive bundles before updating");
    ULARGE_INTEGER free{};Check(GetDiskFreeSpaceExW(root.c_str(),&free,nullptr,nullptr)&&free.QuadPart>8u<<20,"Insufficient space for definition staging");
    if(fs::exists(target)){try{auto prior=d::Verify(target,"definitions");Check(prior.identity==trusted.identity,"Prior definition identity mismatch");}catch(const std::exception&){Check(state.at("active")!=name&&state.at("previous")!=name,"Selected definition bundle is corrupt; use rollback first");const auto saved=root/L"definitions"/Wide("incomplete-"+trusted.identity+"-"+std::to_string(GetTickCount64()));Check(MoveFileExW(target.c_str(),saved.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"Retain incomplete definition bundle");}}
    if(!fs::exists(target)){
        const auto staging=root/L"definitions"/Wide("staging-"+trusted.identity+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentProcessId()));d::CreateDirectorySecure(staging);size_t count=0;
        for(const auto* file:{L"signatures.txt",L"rules.json",d::ManifestName,d::SignatureName}){d::CopyMember(trusted.root/file,staging/file);if(++count==1)Checkpoint("definitions.first-member");}
        {auto candidate=d::Verify(staging,"definitions");Validate(candidate);}ProtectDeploymentTree(staging);Check(MoveFileExW(staging.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"Commit definition staging");
    }
    Checkpoint("definitions.before-activate");
    auto copied=d::Verify(target,"definitions");Validate(copied);state["previous"]=state.at("active");state["active"]=name;state["highestSequence"]=trusted.manifest.at("sequence");d::AtomicJson(root/L"definitions.active.json",state);ProtectDeploymentTree(root/L"definitions.active.json");return 0;
}
} // namespace av::services
