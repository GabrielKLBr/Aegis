#include "Services.h"
#include "distribution/Distribution.h"
#include <aclapi.h>
#include <array>
#include <fstream>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>

namespace av::services {
namespace {
void Checkpoint(const char* name){
#ifdef AEGIS_DEPLOYMENT_FAULT_TEST
    wchar_t requested[128]{};GetEnvironmentVariableW(L"AEGIS_DEPLOYMENT_FAIL_AT",requested,128);if(Wide(name)==requested)TerminateProcess(GetCurrentProcess(),77);
#else
    (void)name;
#endif
}
struct RestorePrivilege {
    Handle token;
    TOKEN_PRIVILEGES previous{};
    DWORD bytes = sizeof(previous);
    RestorePrivilege() {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token.value))
            WinError("Open installer token");
        TOKEN_PRIVILEGES requested{};
        requested.PrivilegeCount = 1;
        if (!LookupPrivilegeValueW(nullptr, L"SeRestorePrivilege", &requested.Privileges[0].Luid))
            WinError("Locate ownership privilege");
        requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        if (!AdjustTokenPrivileges(token.value, FALSE, &requested, sizeof(previous), &previous, &bytes) ||
            GetLastError() == ERROR_NOT_ALL_ASSIGNED)
            WinError("Authorize protected installation ownership");
    }
    ~RestorePrivilege() {
        if (token.value)
            AdjustTokenPrivileges(token.value, FALSE, &previous, 0, nullptr, nullptr);
    }
};
struct ScHandle {
    SC_HANDLE value = nullptr;
    ~ScHandle() {
        if (value)
            CloseServiceHandle(value);
    }
};
void Ordinary(const fs::path &input) {
    const auto path = fs::absolute(input).lexically_normal();
    auto at = path.root_path();
    for (const auto &part : path.relative_path()) {
        at /= part;
        const auto attrs = GetFileAttributesW(at.c_str());
        Check(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
              "Installation reparse point or missing component");
    }
}
fs::path Root() {
    wchar_t *value = nullptr;
    Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &value)),
          "Program Files unavailable");
    const fs::path out = fs::path(value) / L"AegisEngineServices";
    CoTaskMemFree(value);
    return out;
}
void Protect(const fs::path &path) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            InstallationSecurity, SDDL_REVISION_1, &descriptor,
            nullptr))
        WinError("Build installation ACL");
    PSID ownerSid = nullptr;
    BOOL ownerDefault = FALSE;
    GetSecurityDescriptorOwner(descriptor, &ownerSid, &ownerDefault);
    PACL acl = nullptr;
    BOOL present = FALSE, inherited = FALSE;
    GetSecurityDescriptorDacl(descriptor, &present, &acl, &inherited);
    const auto error = SetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT,
                                             OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                                                 PROTECTED_DACL_SECURITY_INFORMATION,
                                             ownerSid, nullptr, acl, nullptr);
    LocalFree(descriptor);
    if (error)
        WinError("Protect installed engine files", error);
}
std::wstring Image(Kind kind, const fs::path &exe, const std::wstring &sid) {
    return L"\"" + exe.wstring() + L"\" --serve " + (kind == Kind::Antivirus ? L"antivirus" : L"sandbox") +
           L" --owner " + sid;
}
void VerifyOwned(const QUERY_SERVICE_CONFIGW &config, const fs::path &root, Kind kind) {
    int argc = 0;
    auto **argv = CommandLineToArgvW(config.lpBinaryPathName, &argc);
    bool valid = argv && argc == 5;
    if (valid) {
        const fs::path exe(argv[0]);
        valid = config.dwServiceType == SERVICE_WIN32_OWN_PROCESS &&
                !_wcsicmp(config.lpServiceStartName, L"LocalSystem") &&
                !_wcsicmp(exe.filename().c_str(), L"guardian-engine-service.exe") &&
                !_wcsicmp(exe.parent_path().parent_path().c_str(), root.c_str()) &&
                std::wstring(argv[1]) == L"--serve" &&
                std::wstring(argv[2]) == (kind == Kind::Antivirus ? L"antivirus" : L"sandbox") &&
                std::wstring(argv[3]) == L"--owner";
        if (valid) {
            try {
                Ordinary(exe);
            } catch (...) {
                valid = false;
            }
        }
    }
    LocalFree(argv);
    Check(valid, "An unrelated service already uses an Aegis service name; installation left it unchanged");
}
struct Previous {
    ScHandle service;
    std::wstring image;
    DWORD startup = SERVICE_DEMAND_START;
    bool running = false, created = false, changed = false;
    std::vector<uint8_t> security;
};
void ReadPrevious(Previous &row, SC_HANDLE scm, Kind kind, const fs::path &root) {
    row.service.value = OpenServiceW(scm, Name(kind), SERVICE_ALL_ACCESS);
    if (!row.service.value) {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
            return;
        WinError("Open existing Aegis service");
    }
    DWORD bytes = 0;
    QueryServiceConfigW(row.service.value, nullptr, 0, &bytes);
    Check(bytes && bytes <= 65536, "Existing service configuration limit");
    std::vector<uint8_t> data(bytes);
    auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(data.data());
    if (!QueryServiceConfigW(row.service.value, config, bytes, &bytes))
        WinError("Read existing service configuration");
    VerifyOwned(*config, root, kind);
    row.image = config->lpBinaryPathName;
    row.startup = config->dwStartType;
    row.running = Query(kind).Running();
    QueryServiceObjectSecurity(row.service.value, DACL_SECURITY_INFORMATION, nullptr, 0, &bytes);
    Check(bytes && bytes <= 65536, "Service security descriptor limit");
    row.security.resize(bytes);
    if (!QueryServiceObjectSecurity(row.service.value, DACL_SECURITY_INFORMATION,
                                    reinterpret_cast<PSECURITY_DESCRIPTOR>(row.security.data()), bytes,
                                    &bytes))
        WinError("Read service permissions");
}
void Permissions(SC_HANDLE service, const std::wstring &owner, Kind kind) {
    // Disabling the antivirus requires an elevated administrator.
    const auto text = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;" +
                      std::wstring(kind == Kind::Antivirus ? L"0x20095" : L"0x200b5") + L";;;" + owner + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        WinError("Build service permissions");
    const auto ok = SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor);
    const auto error = GetLastError();
    LocalFree(descriptor);
    if (!ok)
        WinError("Apply service permissions", error);
}
Json Snapshot(std::array<Previous,2>& previous){
    Json rows=Json::array();for(size_t i=0;i<previous.size();++i){const auto& row=previous[i];Json value{{"kind",i+1},{"installed",row.service.value!=nullptr},{"running",row.running},{"startup",row.startup},{"image",Text(row.image)}};
        if(row.service.value){wchar_t* sddl=nullptr;Check(ConvertSecurityDescriptorToStringSecurityDescriptorW(reinterpret_cast<PSECURITY_DESCRIPTOR>(const_cast<uint8_t*>(row.security.data())),SDDL_REVISION_1,DACL_SECURITY_INFORMATION,&sddl,nullptr)!=FALSE,"Serialize service permissions");value["security"]=Text(sddl);LocalFree(sddl);}rows.push_back(value);
    }return rows;
}
void RestoreSnapshot(const fs::path& root,SC_HANDLE scm,const Json& rows){
    Check(rows.is_array()&&rows.size()==2,"Deployment recovery snapshot rejected");
    for(size_t i=0;i<2;++i){const auto kind=i==0?Kind::Antivirus:Kind::Sandbox;const auto& row=rows.at(i);Check(row.at("kind")==i+1,"Deployment recovery service mismatch");Previous current;ReadPrevious(current,scm,kind,root);
        if(current.service.value)SetRunning(kind,false);
        if(!row.at("installed").get<bool>()){if(current.service.value)Check(DeleteService(current.service.value)!=FALSE,"Recover fresh service registration");continue;}
        const auto image=Wide(row.at("image").get<std::string>());QUERY_SERVICE_CONFIGW config{};config.dwServiceType=SERVICE_WIN32_OWN_PROCESS;config.lpServiceStartName=const_cast<wchar_t*>(L"LocalSystem");config.lpBinaryPathName=const_cast<wchar_t*>(image.c_str());VerifyOwned(config,root,kind);
        if(current.service.value)Check(ChangeServiceConfigW(current.service.value,SERVICE_NO_CHANGE,row.at("startup").get<DWORD>(),SERVICE_NO_CHANGE,image.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)!=FALSE,"Restore service configuration");
        else {current.service.value=CreateServiceW(scm,Name(kind),Label(kind),SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,row.at("startup").get<DWORD>(),SERVICE_ERROR_NORMAL,image.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr);Check(current.service.value!=nullptr,"Recover missing service registration");}
        PSECURITY_DESCRIPTOR descriptor=nullptr;Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(Wide(row.at("security").get<std::string>()).c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=FALSE,"Recover service permission descriptor");const auto applied=SetServiceObjectSecurity(current.service.value,DACL_SECURITY_INFORMATION,descriptor);LocalFree(descriptor);Check(applied!=FALSE,"Recover service permissions");
    }
    for(size_t i=0;i<2;++i)if(rows.at(i).at("installed").get<bool>()&&rows.at(i).at("running").get<bool>())SetRunning(i==0?Kind::Antivirus:Kind::Sandbox,true);
}
Json CurrentRecord(const fs::path& root,const Json& rows){
    namespace d=aegis::distribution;Json result={{"slot",""},{"sequence",uint64_t(0)},{"configuration","Release"},{"hashes",Json::object()}};
    for(const auto& row:rows)if(row.at("installed").get<bool>()){
        int count=0;auto** args=CommandLineToArgvW(Wide(row.at("image").get<std::string>()).c_str(),&count);Check(args&&count==5,"Record existing deployment");const fs::path slot=fs::path(args[0]).parent_path();LocalFree(args);result["slot"]=Text(slot.filename().wstring());
        if(fs::exists(slot/d::ManifestName)){auto trusted=d::Verify(slot,"engines",true);result["sequence"]=trusted.manifest.at("sequence");result["configuration"]=trusted.manifest.at("configuration");}
        for(const auto& file:fs::directory_iterator(slot))if(file.is_regular_file()){Ordinary(file.path());result["hashes"][Text(file.path().filename().wstring())]=d::HashFile(file.path());}break;
    }(void)root;return result;
}
void VerifyRecord(const fs::path& root,const Json& record){
    namespace d=aegis::distribution;const auto name=record.at("slot").get<std::string>();d::RelativeName(name);Check(name.starts_with("install-")&&name.find('/')==name.npos,"Rollback slot rejected");const auto slot=root/Wide(name);Ordinary(slot);
    Check(record.at("hashes").is_object()&&!record.at("hashes").empty()&&record.at("hashes").size()<=64,"Rollback inventory rejected");
    for(auto it=record.at("hashes").begin();it!=record.at("hashes").end();++it){d::RelativeName(it.key());Check(it.key().find('/')==std::string::npos&&d::HashFile(slot/Wide(it.key()))==it.value().get<std::string>(),"Previous service slot changed; rollback refused");}
    if(fs::exists(slot/d::ManifestName)){auto verified=d::Verify(slot,"engines",true);(void)verified;}
}
void RecoverPending(const fs::path& root,SC_HANDLE scm){
    namespace d=aegis::distribution;const auto pending=root/L"deployment.pending.json";if(!fs::exists(pending))return;Ordinary(pending);const auto journal=d::ReadJson(pending);Check(journal.at("schema")==1&&journal.at("kind")=="service-deployment","Service recovery journal rejected");
    const auto statePath=root/L"deployment.state.json";auto state=fs::exists(statePath)?d::ReadJson(statePath):Json::object();
    if(state.value("transaction",std::string())!=journal.at("transaction").get<std::string>()){
        if(journal.at("before").at("current").at("slot")!="")VerifyRecord(root,journal.at("before").at("current"));
        RestoreSnapshot(root,scm,journal.at("services"));d::AtomicJson(statePath,journal.at("before"));
    }
    Check(DeleteFileW(pending.c_str())!=FALSE,"Retire service recovery journal");
}
} // namespace
void ProtectDeploymentTree(const fs::path& path){RestorePrivilege privilege;Ordinary(path);Protect(path);if(fs::is_directory(path))for(const auto& entry:fs::recursive_directory_iterator(path)){Ordinary(entry.path());Protect(entry.path());}}
int Install(const std::wstring &ownerArgument, bool uninstall, bool rollback) {
    namespace d=aegis::distribution;
    auto source = Executable().parent_path();
#ifdef AEGIS_DEPLOYMENT_FAULT_TEST
    wchar_t overridePath[32768]{};Check(GetEnvironmentVariableW(L"AEGIS_DEPLOYMENT_TEST_SOURCE",overridePath,32768)>0,"Explicit test package required");source=overridePath;
#endif
    // Authenticate before stopping or changing any installed service.
    std::unique_ptr<d::VerifiedTree> trusted;
    if(!uninstall&&!rollback)trusted=std::make_unique<d::VerifiedTree>(d::Verify(source,"engines"));
    if(trusted){auto bundled=d::Verify(source/L"definitions","definitions");Check(bundled.manifest.at("files").contains("rules.json")&&bundled.manifest.at("files").contains("signatures.txt"),"Bundled definitions missing");}
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(ownerArgument.c_str(), &sid))
        WinError("Validate installation owner");
    const auto user = IsValidSid(sid) && !IsWellKnownSid(sid, WinLocalSystemSid) &&
                      !IsWellKnownSid(sid, WinLocalServiceSid) &&
                      !IsWellKnownSid(sid, WinNetworkServiceSid) &&
                      !IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
    wchar_t *canonical = nullptr;
    if (!ConvertSidToStringSidW(sid, &canonical)) {
        LocalFree(sid);
        WinError("Canonicalize installation owner");
    }
    const std::wstring owner(canonical);
    LocalFree(canonical);
    DWORD accountBytes = 0, domainBytes = 0;
    SID_NAME_USE accountType{};
    LookupAccountSidW(nullptr, sid, nullptr, &accountBytes, nullptr, &domainBytes, &accountType);
    std::vector<wchar_t> account(accountBytes), domain(domainBytes);
    const auto resolved = accountBytes && LookupAccountSidW(nullptr, sid, account.data(), &accountBytes,
                                                            domain.data(), &domainBytes, &accountType);
    LocalFree(sid);
    Check(user && resolved && accountType == SidTypeUser, "A desktop user SID is required");
    const auto root = Root();
    ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE)};
    if (!scm.value)
        WinError("Administrator authorization is required to install services");
    const std::array kinds{Kind::Antivirus, Kind::Sandbox};
    RestorePrivilege ownership;
    if(!fs::exists(root)){Check(!uninstall&&!rollback,"No Aegis installation exists");d::CreateDirectorySecure(root,true);Protect(root);d::AtomicWrite(root/L"aegis-engine-services.txt","Aegis Engine Services v1\n");}
    Ordinary(root);Ordinary(root/L"aegis-engine-services.txt");{std::ifstream marker(root/L"aegis-engine-services.txt");std::string text;std::getline(marker,text);Check(text=="Aegis Engine Services v1","Unrelated installation root rejected");}Protect(root);
    Handle deploymentLock(CreateFileW((root/L"deployment.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(deploymentLock.value!=INVALID_HANDLE_VALUE,"Another service deployment is running");Ordinary(root/L"deployment.lock");Protect(root/L"deployment.lock");RecoverPending(root,scm.value);
    std::array<Previous, 2> previous;
    for (size_t i = 0; i < kinds.size(); ++i)
        ReadPrevious(previous[i], scm.value, kinds[i], root);
    const auto beforeRows=Snapshot(previous);auto before=fs::exists(root/L"deployment.state.json")?d::ReadJson(root/L"deployment.state.json"):Json{{"schema",1},{"current",CurrentRecord(root,beforeRows)},{"previous",Json::object()},{"previousServices",Json::array()},{"highestSequence",uint64_t(0)},{"transaction",""}};
    if(rollback){Check(before.at("previous").is_object()&&before.at("previous").contains("slot"),"No previous service deployment");VerifyRecord(root,before.at("previous"));auto next=before;next["current"]=before.at("previous");next["previous"]=CurrentRecord(root,beforeRows);auto targetRows=before.at("previousServices");next["previousServices"]=beforeRows;const auto transaction=std::to_string(GetTickCount64())+"-rollback";next["transaction"]=transaction;d::AtomicJson(root/L"deployment.pending.json",{{"schema",1},{"kind","service-deployment"},{"transaction",transaction},{"before",before},{"services",beforeRows}});RestoreSnapshot(root,scm.value,targetRows);d::AtomicJson(root/L"deployment.state.json",next);RecoverPending(root,scm.value);return 0;}
    if (uninstall) {
        const auto transaction=std::to_string(GetTickCount64())+"-uninstall";
        d::AtomicJson(root/L"deployment.pending.json",{{"schema",1},{"kind","service-deployment"},{"transaction",transaction},{"before",before},{"services",beforeRows}});
        try{for (size_t i = 0; i < kinds.size(); ++i)if(previous[i].service.value){SetRunning(kinds[i],false);Check(DeleteService(previous[i].service.value)!=FALSE,"Remove Aegis service registration");CloseServiceHandle(previous[i].service.value);previous[i].service.value=nullptr;}
            auto next=before;next["transaction"]=transaction;d::AtomicJson(root/L"deployment.state.json",next);RecoverPending(root,scm.value);
        }catch(...){for(auto& row:previous)if(row.service.value){CloseServiceHandle(row.service.value);row.service.value=nullptr;}RecoverPending(root,scm.value);throw;}
        // Retain versioned files and reports for recovery; no recursive deletion.
        return 0;
    }
    Ordinary(root.parent_path());
    const bool fresh = fs::create_directory(root);
    Ordinary(root);
    const auto marker = root / L"aegis-engine-services.txt";
    if (!fresh) {
        Ordinary(marker);
        std::ifstream stream(marker);
        std::string line;
        std::getline(stream, line);
        Check(line == "Aegis Engine Services v1", "Installation directory belongs to another application");
    }
    Protect(root);
    if (fresh) {
        std::ofstream stream(marker);
        stream << "Aegis Engine Services v1\n";
        Check(bool(stream), "Cannot write installation marker");
    }
    const auto slot = root / (L"install-" + std::to_wstring(GetTickCount64()) + L"-" +
                              std::to_wstring(GetCurrentProcessId()));
    Check(!fs::exists(slot), "A new installation slot is required");d::CreateDirectorySecure(slot);
    Ordinary(slot);
    Check(trusted->manifest.at("sequence").get<uint64_t>()>=before.at("highestSequence").get<uint64_t>(),"Older service package refused; use explicit rollback");
    if(before.at("current").at("slot")!="")Check(trusted->manifest.at("configuration")==before.at("current").at("configuration"),"Service build configuration mismatch");
    for (const auto *name : PackageFiles) {
        const auto file = source / name;
        Ordinary(file);
        Check(fs::is_regular_file(file), "Required service package file missing");
        d::CopyMember(file,slot/name);
    }
    // Optional app-local Microsoft redistributables, supplied by the package.
    for (const auto *name : RuntimeFiles)
        if (fs::exists(source / name)) {
            Ordinary(source / name);
            d::CopyMember(source/name,slot/name);
        }
    for(auto it=trusted->manifest.at("files").begin();it!=trusted->manifest.at("files").end();++it){const auto target=slot/d::Wide(it.key());if(!fs::exists(target))d::CopyMember(source/d::Wide(it.key()),target);}
    for(const auto* name:{d::ManifestName,d::SignatureName})d::CopyMember(source/name,slot/name);
    // Keep local, licensed guest references across an update. Portable archives
    // never redistribute those references. The runtime still verifies its index
    // and helper hashes before using a prepared application.
    auto assets=source;
    const auto& retainedImage=previous[1].image.empty()?previous[0].image:previous[1].image;
    if(!fs::exists(assets/L"runtime")&&!retainedImage.empty()){
        int count=0;auto** arguments=CommandLineToArgvW(retainedImage.c_str(),&count);
        if(arguments&&count==5)assets=fs::path(arguments[0]).parent_path();LocalFree(arguments);
    }
    const auto catalogSlot=root/L"runtime-slot.txt";
    if(!fs::exists(assets/L"runtime")&&fs::exists(catalogSlot)){
        Ordinary(catalogSlot);Check(fs::file_size(catalogSlot)<=256,"Retained runtime locator quota");
        std::ifstream saved(catalogSlot);std::string name;std::getline(saved,name);
        Check(name.starts_with("install-")&&name.size()>8&&name.find_first_not_of("install-0123456789")==name.npos,"Retained runtime locator rejected");
        assets=root/Wide(name);Ordinary(assets);
    }
    // Runtime assets are data. Helpers are installed exclusively from PackageFiles.
    if (fs::exists(assets / L"runtime")) {
        Ordinary(assets / L"runtime");
        const auto indexPath = assets / L"runtime" / L"index.json";
        Ordinary(indexPath);
        Check(fs::file_size(indexPath) <= 2u << 20, "Runtime index quota");
        std::ifstream stream(indexPath);
        const auto index = Json::parse(stream);
        Check(index.value("schema", 0) == 1 && index.at("applications").is_object() &&
                  index.at("applications").size() <= 16,
              "Installed runtime index rejected");
        d::CreateDirectorySecure(slot / L"runtime");d::CopyMember(indexPath,slot/L"runtime"/L"index.json");
        uint64_t total = 0;
        for (const auto &record : index.at("applications")) {
            const auto &files = record.at("files");
            Check(files.is_object() && files.size() <= 12, "Installed runtime member quota");
            for (auto it = files.begin(); it != files.end(); ++it) {
                auto part = fs::path(Wide(it.key()));
                Check(!part.empty() && !part.is_absolute() && !part.has_root_name() &&
                          it.key().find(':') == std::string::npos,
                      "Installed runtime path rejected");
                auto member = assets / L"runtime";
                for (const auto &component : part) {
                    Check(component != L".." && component != L".", "Installed runtime traversal rejected");
                    member /= component;
                    Ordinary(member);
                }
                Check(fs::is_regular_file(member) && fs::file_size(member) <= 64u << 20,
                      "Installed runtime member size rejected");
                total += fs::file_size(member);
                Check(total <= 512ull << 20, "Installed runtime total quota");
                const auto target = slot / L"runtime" / part;
                d::CreateDirectorySecure(target.parent_path());
                if (!fs::exists(target))
                    d::CopyMember(member,target);
            }
        }
    }
    // Install an immutable code tree for standard users, including the owner.
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        Ordinary(entry.path());
        Protect(entry.path());
    }
    Protect(slot);
    auto installed=d::Verify(slot,"engines",true);
    const auto exe = slot / L"guardian-engine-service.exe";
    const auto transaction=std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentProcessId());
    d::AtomicJson(root/L"deployment.pending.json",{{"schema",1},{"kind","service-deployment"},{"transaction",transaction},{"before",before},{"services",beforeRows}});
    Checkpoint("service.prepared");
    try {
        for (size_t i = 0; i < kinds.size(); ++i)
            if (previous[i].service.value)
                SetRunning(kinds[i], false);
        for (size_t i = 0; i < kinds.size(); ++i) {
            auto &row = previous[i];
            const auto kind = kinds[i];
            const auto image = Image(kind, exe, owner);
            if (row.service.value) {
                if (!ChangeServiceConfigW(row.service.value, SERVICE_NO_CHANGE,
                                          kind == Kind::Antivirus ? SERVICE_AUTO_START : SERVICE_NO_CHANGE,
                                          SERVICE_NO_CHANGE, image.c_str(), nullptr, nullptr, nullptr,
                                          nullptr, nullptr, Label(kind)))
                    WinError("Update Aegis service");
            } else {
                row.service.value = CreateServiceW(
                    scm.value, Name(kind), Label(kind), SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                    kind == Kind::Antivirus ? SERVICE_AUTO_START : SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                    image.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
                if (!row.service.value)
                    WinError("Register Aegis service");
                row.created = true;
            }
            row.changed = true;
            if(i==0)Checkpoint("service.firstConfigured");
            Permissions(row.service.value, owner, kind);
            if (kind == Kind::Antivirus) {
                SC_ACTION actions[] = {
                    {SC_ACTION_RESTART, 1000}, {SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 15000}};
                SERVICE_FAILURE_ACTIONSW recovery{86400, nullptr, nullptr, 3, actions};
                if (!ChangeServiceConfig2W(row.service.value, SERVICE_CONFIG_FAILURE_ACTIONS, &recovery))
                    WinError("Configure antivirus recovery");
                SERVICE_FAILURE_ACTIONS_FLAG flag{TRUE};
                if (!ChangeServiceConfig2W(row.service.value, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag))
                    WinError("Configure failure recovery");
                SERVICE_DELAYED_AUTO_START_INFO delayed{TRUE};
                if (!ChangeServiceConfig2W(row.service.value, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                                           &delayed))
                    WinError("Configure automatic protection startup");
            }
            wchar_t required[] = L"SeChangeNotifyPrivilege\0SeImpersonatePrivilege\0SeAssignPrimaryTokenPrivi"
                                 L"lege\0SeIncreaseQuotaPrivilege\0SeTcbPrivilege\0";
            SERVICE_REQUIRED_PRIVILEGES_INFOW privileges{required};
            if (!ChangeServiceConfig2W(row.service.value, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO,
                                       &privileges))
                WinError("Limit service privileges");
            wchar_t antivirusDescription[] =
                L"Analysis of Aegis antivirus in restricted user processes.";
            wchar_t sandboxDescription[] =
                L"Supervised MiniNT/WHP executions; user session interfaces.";
            SERVICE_DESCRIPTIONW description{kind == Kind::Antivirus ? antivirusDescription
                                                                     : sandboxDescription};
            if (!ChangeServiceConfig2W(row.service.value, SERVICE_CONFIG_DESCRIPTION, &description))
                WinError("Describe Aegis service");
        }
        for (size_t i = 0; i < kinds.size(); ++i)
            if (previous[i].running || (previous[i].created && kinds[i]==Kind::Antivirus))
                SetRunning(kinds[i], true);
        if(Query(Kind::Antivirus).Running()){
            const auto readyUntil=GetTickCount64()+5000;bool ready=false;
            while(!ready&&GetTickCount64()<readyUntil){try{auto status=Call(Kind::Antivirus,Operation::ProtectionStatus,Executable(),{},nullptr,1500);ready=status.value("complete",false);}catch(const std::exception&){Sleep(100);}}
            Check(ready,"Updated antivirus did not pass its IPC health check");
        }
        Checkpoint("service.started");
        if(fs::exists(slot/L"runtime")){
            d::AtomicWrite(catalogSlot,Text(slot.filename().wstring())+"\n");
        }
        auto next=before;next["schema"]=1;next["previous"]=CurrentRecord(root,beforeRows);next["previousServices"]=beforeRows;next["current"]={{"slot",Text(slot.filename().wstring())},{"sequence",trusted->manifest.at("sequence")},{"configuration",trusted->manifest.at("configuration")},{"hashes",Json::object()}};
        for(const auto& file:fs::directory_iterator(slot))if(file.is_regular_file())next["current"]["hashes"][Text(file.path().filename().wstring())]=d::HashFile(file.path());
        next["highestSequence"]=std::max(before.at("highestSequence").get<uint64_t>(),trusted->manifest.at("sequence").get<uint64_t>());next["transaction"]=transaction;d::AtomicJson(root/L"deployment.state.json",next);RecoverPending(root,scm.value);
    } catch (...) {
        for (size_t i = 0; i < kinds.size(); ++i)
            if (previous[i].changed)
                try {
                    SetRunning(kinds[i], false);
                } catch (...) {
                }
        for (size_t i = 0; i < kinds.size(); ++i) {
            auto &row = previous[i];
            if (row.created)
                DeleteService(row.service.value);
            else if (row.changed) {
                ChangeServiceConfigW(row.service.value, SERVICE_NO_CHANGE, row.startup, SERVICE_NO_CHANGE,
                                     row.image.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                SetServiceObjectSecurity(row.service.value, DACL_SECURITY_INFORMATION, row.security.data());
            }
            if (!row.created && row.running)
                try {
                    SetRunning(kinds[i], true);
                } catch (...) {
                }
        }
        RecoverPending(root,scm.value);
        throw;
    }
    return 0;
}
} // namespace av::services
