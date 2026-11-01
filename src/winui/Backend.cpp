#include "Backend.h"
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <fstream>
#include <algorithm>
#include <chrono>
namespace av::ui {
using namespace services;
namespace {
std::string Token() { GUID id{};Check(SUCCEEDED(CoCreateGuid(&id)),"Create request identifier");wchar_t text[40]{};StringFromGUID2(id,text,40);return Text(text); }
void Ordinary(const fs::path& path) {
    fs::path at;
    for(const auto& part:fs::absolute(path)){at/=part;const auto a=GetFileAttributesW(at.c_str());Check(a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_REPARSE_POINT),"Missing UI state path or reparse point rejected");}
}
void Write(const fs::path& path,const Json& value,size_t limit=65536) {
    const auto text=value.dump();Check(text.size()<=limit,"UI request exceeds 64 KiB");Ordinary(path.parent_path());
    PSECURITY_DESCRIPTOR sd=nullptr;const auto acl=L"D:P(A;;FA;;;SY)(A;;FA;;;"+UserSid()+L")";
    Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr)!=FALSE,"Create request ACL");
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));LocalFree(sd);
    Check(file.value!=INVALID_HANDLE_VALUE,"Create private UI request");DWORD written=0;Check(WriteFile(file.value,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file.value),"Write UI request");
}
Json Read(const fs::path& path,size_t limit=65536) {
    Ordinary(path);Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(h.value!=INVALID_HANDLE_VALUE,"Read UI preferences");BY_HANDLE_FILE_INFORMATION info{};Check(GetFileInformationByHandle(h.value,&info)&&info.nNumberOfLinks==1&&info.nFileSizeHigh==0&&info.nFileSizeLow<=limit,"Invalid UI preferences file");
    std::string text(info.nFileSizeLow,'\0');DWORD n=0;Check(ReadFile(h.value,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size(),"Read UI preferences bytes");return Json::parse(text,[](int depth,Json::parse_event_t,Json&){Check(depth<=64,"UI JSON nesting limit exceeded");return true;});
}
void Persist(const fs::path& root,const wchar_t* name,const Json& value){
    const auto temp=root/Wide(Token()+".json");Write(temp,value,3u<<20);
    if(!MoveFileExW(temp.c_str(),(root/name).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ec;fs::remove(temp,ec);WinError("Persist UI session summary");}
}

Json ScanStatus(const Json& status,const std::string& id){
    Json result{{"running",false},{"completed",0},{"results",Json::array()}};
    for(const auto& job:status.at("jobs"))if(job.value("id",std::string())==id){const auto state=job.value("state",std::string());result=job;result["running"]=state=="queued"||state=="running";result["completed"]=job.value("filesScanned",uint64_t(0));result["last_error"]=job.value("error",std::string());result["results"]=Json::array({job});break;}
    return result;
}
}
Backend::Backend(fs::path state){
    PWSTR folder=nullptr;Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder)),"Locate local application data");state_=state.empty()?fs::path(folder)/L"AegisUI17":fs::absolute(state);CoTaskMemFree(folder);fs::create_directory(state_);Ordinary(state_);
    PSECURITY_DESCRIPTOR sd=nullptr;const auto acl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;"+UserSid()+L")";Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr)!=FALSE,"Create preferences ACL");BOOL present=FALSE,def=FALSE;PACL dacl=nullptr;GetSecurityDescriptorDacl(sd,&present,&dacl,&def);const auto err=SetNamedSecurityInfoW(const_cast<wchar_t*>(state_.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,dacl,nullptr);LocalFree(sd);Check(err==ERROR_SUCCESS,"Protect UI state directory");
    preferences_={{"locale","en-US"},{"theme","system"}};session_={{"running",false},{"complete",false},{"state","idle"}};
    if(fs::exists(state_/L"preferences.json")){try{const auto saved=Read(state_/L"preferences.json");for(const auto* key:{"locale","theme"})if(saved.contains(key)&&saved[key].is_string()){const auto value=saved[key].get<std::string>();if(std::string(key)=="locale"?(value=="system"||value=="en-US"||value=="pt-BR"||value=="es-ES"):(value=="system"||value=="light"||value=="dark"))preferences_[key]=value;}}catch(...){preferences_["warning"]="Saved preferences rejected; using defaults.";}}
    if(fs::exists(state_/L"last-session.json")){try{session_=Read(state_/L"last-session.json",3u<<20);Check(session_.is_object(),"Invalid saved session");if(session_.value("running",false)){session_["running"]=false;session_["state"]="interrupted";session_["complete"]=false;}}catch(...){session_={{"running",false},{"complete",false},{"state","incomplete"},{"error","Saved session summary rejected"}};}}
    if(fs::exists(state_/L"last-scan.json")){try{scanId_=Read(state_/L"last-scan.json").value("id",std::string());}catch(...){}}

}
Backend::~Backend(){Close();if(sessionThread_.joinable())sessionThread_.join();}
void Backend::Export(const fs::path& path,const Json& value){Write(path,value,3u<<20);}
void Backend::Close() noexcept {closed_=true;cancel_=true;}
Json Backend::Call(Operation operation,Json request){
    Check(!closed_,"Application is closing");fs::path input=Executable();
    if(!request.empty()){input=state_/Wide(Token()+".json");Write(input,request);}
    try{auto result=services::Call(Kind::Antivirus,operation,input,{},&closed_,15000);if(input!=Executable()){std::error_code ec;fs::remove(input,ec);}Check(result.value("complete",true),result.value("error",std::string("Service operation incomplete")).c_str());return result;}
    catch(...){if(input!=Executable()){std::error_code ec;fs::remove(input,ec);}throw;}
}
Json Backend::StartSession(const std::string& method,const Json& p){
    std::lock_guard startGuard(sessionStartMutex_);
    const auto path=p.at("path").get<std::string>();Check(!path.empty(),"Select an executable, prepared bundle or recovery.json");
    std::unique_lock lock(mutex_);Check(!session_.value("running",false),"A MiniNT session is already running");lock.unlock();if(sessionThread_.joinable())sessionThread_.join();lock.lock();cancel_=false;Check(!closed_,"Application is closing");
    const auto op=method=="session.checkpoint"?Operation::Checkpoint:method=="session.resume"?Operation::Resume:method=="session.bundle"?Operation::DesktopBundle:Operation::Detonate;
    session_={{"id",Token()},{"path",path},{"operation",method},{"running",true},{"state","connecting"},{"complete",false}};
    Persist(state_,L"last-session.json",session_);
    sessionThread_=std::thread([this,op,path]{Json result;
        try{result=services::Call(Kind::Sandbox,op,fs::path(Wide(path)),[this](const Json& event){std::lock_guard guard(mutex_);session_["progress"]=event;session_["state"]=event.value("stage",std::string("running"));},&cancel_,60000);}
        catch(const std::exception& e){result={{"complete",false},{"error",e.what()},{"cancelled",cancel_.load()}};}
        std::lock_guard guard(mutex_);session_["running"]=false;session_["state"]=cancel_?"cancelled":result.value("complete",false)?"completed":"incomplete";session_["complete"]=result.value("complete",false);session_["report"]=std::move(result);try{Persist(state_,L"last-session.json",session_);}catch(const std::exception& e){session_["persistenceError"]=e.what();}
    });return session_;
}
Json Backend::Request(const std::string& method,const Json& p){
    Check(!closed_,"Application is closing");Check(p.is_object(),"UI parameters must be an object");
    if(method=="settings.get"){std::lock_guard lock(preferencesMutex_);return preferences_;}
    if(method=="settings.set"){
        std::lock_guard lock(preferencesMutex_);auto next=preferences_;
        for(const auto& [k,v]:p.items()){Check(k=="locale"||k=="theme","Unsupported UI preference");Check(v.is_string(),"UI preference must be a string");auto x=v.get<std::string>();Check(k=="locale"?(x=="system"||x=="en-US"||x=="pt-BR"||x=="es-ES"):(x=="system"||x=="light"||x=="dark"),"Invalid UI preference");next[k]=v;}
        const auto temp=state_/Wide(Token()+".json");Write(temp,next);if(!MoveFileExW(temp.c_str(),(state_/L"preferences.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ec;fs::remove(temp,ec);WinError("Save preferences");}preferences_=next;return next;
    }
    if(method=="services.get"){
        Json rows=Json::array();for(auto kind:{Kind::Antivirus,Kind::Sandbox}){const auto s=Query(kind);rows.push_back({{"kind",uint32_t(kind)},{"installed",s.installed},{"running",s.Running()},{"state",s.state},{"error",s.error},{"pid",s.process}});}return {{"items",rows}};
    }
    if(method=="services.set"){auto k=p.at("kind").get<uint32_t>();Check(k==1||k==2,"Invalid service kind");SetRunning(Kind(k),p.at("running").get<bool>());return Request("services.get",Json::object());}
    if(method=="status"||method=="protection.get")return Call(Operation::ProtectionStatus);
    if(method=="protection.set"){
        auto policy=Call(Operation::ProtectionStatus).at("policy");policy["enabled"]=p.at("enabled");policy["roots"]=p.at("roots");policy["exclusions"]=p.at("exclusions");if(p.contains("autoQuarantine"))policy["autoQuarantine"]=p.at("autoQuarantine");for(const auto* key:{"maxFiles","maxSeconds"})if(p.contains(key))policy[key]=p.at(key);
        if(p.contains("uiScheduleSeconds")){auto interval=p.at("uiScheduleSeconds").get<int>();Check(interval==0||(interval>=60&&interval<=604800),"Schedule must be disabled or between 60 seconds and seven days");auto schedules=Json::array();for(const auto& entry:policy.at("schedules"))if(entry.value("id",std::string())!="winui17-quick")schedules.push_back(entry);if(interval){const auto next=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()+interval;schedules.push_back({{"id","winui17-quick"},{"mode","quick"},{"paths",Json::array()},{"intervalSeconds",interval},{"nextRun",next}});}policy["schedules"]=schedules;}return Call(Operation::ProtectionConfigure,policy);
    }
    if(method=="scan.start"){
        auto mode=p.at("mode").get<std::string>();Check(mode=="quick"||mode=="full"||mode=="custom","Invalid scan mode");Json request{{"schema",1},{"mode",mode}};if(mode=="custom")request["paths"]=Json::array({p.at("path")});auto result=Call(Operation::ProtectionSubmit,request);std::lock_guard lock(mutex_);scanId_=result.at("job").at("id");try{Persist(state_,L"last-scan.json",{{"id",scanId_}});}catch(const std::exception& e){result["persistenceWarning"]=e.what();}return result;
    }
    if(method=="scan.status"||method=="scan.cancel"){
        std::string id;{std::lock_guard lock(mutex_);id=scanId_;}
        if(method=="scan.cancel"){Check(!id.empty(),"No UI scan to cancel");return Call(Operation::ProtectionCancel,{{"schema",1},{"id",id}});}
        const auto s=Call(Operation::ProtectionStatus);if(id.empty())for(const auto& j:s.at("jobs"))if(j.value("origin",std::string())=="manual"){id=j.at("id");break;}return ScanStatus(s,id);
    }
    if(method=="history")return {{"items",Call(Operation::ProtectionStatus).at("jobs")}};
    if(method=="events")return Call(Operation::ProtectionEvents,{{"schema",1},{"after",p.value("after",uint64_t(0))}});
    if(method=="quarantine.list")return Call(Operation::QuarantineList);
    if(method=="quarantine.audit")return Call(Operation::QuarantineAudit,{{"schema",2},{"before",p.value("before",uint64_t(0))}});
    if(method=="quarantine.add")return Call(Operation::QuarantineIsolate,{{"schema",2},{"source",p.at("path")},{"threat","Manual"}});
    if(method=="quarantine.delete")return Call(Operation::QuarantineErase,{{"schema",2},{"id",p.at("id")}});
    if(method=="quarantine.restore"){const fs::path dest=Wide(p.at("destination").get<std::string>());return Call(Operation::QuarantineRestore,{{"schema",2},{"id",p.at("id")},{"directory",Text(dest.parent_path().wstring())},{"filename",Text(dest.filename().wstring())}});}
    if(method=="session.status"||method=="vm.list"||method=="behavior") {std::lock_guard lock(mutex_);return method=="vm.list"?Json{{"items",Json::array({session_})}}:session_;}
    if(method=="session.cancel"){cancel_=true;return {{"requested",true}};}
    if(method=="session.start"||method=="session.checkpoint"||method=="session.resume"||method=="session.bundle")return StartSession(method,p);
    throw std::runtime_error("Unsupported UI operation");
}
}
