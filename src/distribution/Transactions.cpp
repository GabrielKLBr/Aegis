#include "Distribution.h"
#include <algorithm>
#include <array>
namespace aegis::distribution {
namespace {
File Lock(const fs::path& root){Ordinary(root);File lock(CreateFileW((root/L"update.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Require(lock.h!=INVALID_HANDLE_VALUE,"Another update is running");BY_HANDLE_FILE_INFORMATION info{};Require(GetFileInformationByHandle(lock.h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&info.nNumberOfLinks==1,"Invalid update lock");return lock;}
void SelectionName(const std::string& value){Require(value=="base"||(value.starts_with("versions/update-")&&value.size()==80&&value.substr(16).find_first_not_of("0123456789abcdef")==std::string::npos),"Invalid update selection");}
Json State(const fs::path& root){
    if(!fs::exists(root/L"active.json")){auto base=Verify(root,"portable",true);return {{"schema",1},{"active","base"},{"previous",""},{"highestSequence",base.manifest.at("sequence")}};}
    auto state=ReadJson(root/L"active.json");Require(state.is_object()&&state.size()==4&&state.at("schema")==1&&state.at("highestSequence").is_number_unsigned(),"Invalid update state");SelectionName(state.at("active").get<std::string>());if(state.at("previous")!="")SelectionName(state.at("previous").get<std::string>());return state;
}
fs::path Path(const fs::path& root,const std::string& selection){SelectionName(selection);return selection=="base"?root:root/Wide(selection);}
void Recover(const fs::path& root){
    if(!fs::exists(root/L"update.pending.json"))return;
    const auto journal=ReadJson(root/L"update.pending.json");Require(journal.at("schema")==1&&journal.at("kind")=="portable-update","Update journal rejected");
    const auto before=journal.at("before");SelectionName(before.at("active").get<std::string>());const auto next=journal.at("next").get<std::string>();SelectionName(next);
    // A pointer replacement is the only activation step. Staged files remain inert.
    auto state=State(root);const bool committed=state.at("active")==next;
    auto selected=Verify(Path(root,state.at("active").get<std::string>()),"portable",state.at("active")=="base");(void)selected;
    auto result=journal;result["recovered"]=true;result["committed"]=committed;AtomicJson(root/L"update.last.json",result);
    Require(DeleteFileW((root/L"update.pending.json").c_str())!=FALSE,"Retire completed update journal");
}
}
void RecoverUpdate(const fs::path& root){if(!fs::exists(root/L"update.pending.json"))return;auto lock=Lock(root);Recover(root);}
fs::path SelectedPackage(const fs::path& root){const auto state=State(root);return Path(root,state.at("active").get<std::string>());}
Json DeploymentStatus(const fs::path& root){RecoverUpdate(root);auto state=State(root);auto tree=Verify(Path(root,state.at("active").get<std::string>()),"portable",state.at("active")=="base");state["version"]=tree.manifest.at("version");state["configuration"]=tree.manifest.at("configuration");state["identity"]=tree.identity;state["rollbackAvailable"]=state.at("previous")!="";state["authenticity"]="publisher-manifest";return state;}
Json ApplyUpdate(const fs::path& root,const fs::path& source,const std::function<void(const char*)>& checkpoint){
    auto lock=Lock(root);Recover(root);auto before=State(root);auto current=Verify(Path(root,before.at("active").get<std::string>()),"portable",before.at("active")=="base");auto incoming=Verify(source,"portable");
    Require(incoming.manifest.at("configuration")==current.manifest.at("configuration"),"Update configuration mismatch");Require(incoming.manifest.at("sequence").get<uint64_t>()>before.at("highestSequence").get<uint64_t>(),"Older or repeated update refused; use explicit rollback instead");
    Require(fs::absolute(source).lexically_normal()!=fs::absolute(root).lexically_normal(),"Update source is the active portable folder");
    ULARGE_INTEGER free{};uint64_t total=16u<<20;for(const auto& member:incoming.manifest.at("files"))total+=member.at("bytes").get<uint64_t>();Require(GetDiskFreeSpaceExW(root.c_str(),&free,nullptr,nullptr)&&free.QuadPart>total,"Not enough space for a recoverable update");
    const auto versions=root/L"versions";fs::create_directory(versions);Ordinary(versions);const auto selection="versions/update-"+incoming.identity;const auto target=root/Wide(selection);
    size_t retained=0;for(const auto& entry:fs::directory_iterator(versions)){(void)entry;++retained;}Require(retained<16,"Portable version limit reached; remove inactive version folders before updating");
    if(fs::exists(target)){auto prior=Verify(target,"portable");Require(prior.identity==incoming.identity,"Staged update identity mismatch");}
    else {
        const auto staging=versions/Wide("staging-"+incoming.identity+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));Require(fs::create_directory(staging),"Create update staging directory");
        for(auto it=incoming.manifest.at("files").begin();it!=incoming.manifest.at("files").end();++it)CopyMember(incoming.root/Wide(it.key()),staging/Wide(it.key()));
        for(const auto* name:{ManifestName,SignatureName})CopyMember(incoming.root/name,staging/name);
        {auto staged=Verify(staging,"portable");Require(staged.identity==incoming.identity,"Staged update verification failed");}
        Require(MoveFileExW(staging.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"Commit update staging directory");
    }
    if(checkpoint)checkpoint("copied");
    AtomicJson(root/L"update.pending.json",{{"schema",1},{"kind","portable-update"},{"before",before},{"next",selection}});if(checkpoint)checkpoint("prepared");
    auto next=before;next["previous"]=before.at("active");next["active"]=selection;next["highestSequence"]=incoming.manifest.at("sequence");AtomicJson(root/L"active.json",next);if(checkpoint)checkpoint("activated");
    Recover(root);return {{"complete",true},{"restartRequired",true},{"version",incoming.manifest.at("version")},{"servicesUpdateSeparate",true}};
}
Json Rollback(const fs::path& root){
    auto lock=Lock(root);Recover(root);auto state=State(root);Require(state.at("previous")!="","No previous portable version");auto previous=Verify(Path(root,state.at("previous").get<std::string>()),"portable",state.at("previous")=="base");auto active=state.at("active");state["active"]=state.at("previous");state["previous"]=active;AtomicJson(root/L"active.json",state);return {{"complete",true},{"restartRequired",true},{"version",previous.manifest.at("version")}};
}
Json MigratePreferences(const fs::path& file){
    auto saved=ReadJson(file);Require(saved.is_object(),"Preferences object required");const auto schema=saved.value("schema",0);Require(schema==0||schema==1,"Preferences from a newer app cannot be migrated");
    if(schema==0){const auto backup=file.parent_path()/L"preferences.schema0.backup.json";if(!fs::exists(backup))Require(fs::copy_file(file,backup),"Back up legacy preferences");saved["schema"]=1;AtomicJson(file,saved);}return saved;
}
} // namespace aegis::distribution
