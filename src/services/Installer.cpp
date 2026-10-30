#include "Services.h"
#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <array>
#include <fstream>
#include <vector>

namespace av::services {
namespace {
struct ScHandle {
    SC_HANDLE value = nullptr;
    ~ScHandle() { if (value) CloseServiceHandle(value); }
};
void Ordinary(const fs::path& input) {
    const auto path = fs::absolute(input).lexically_normal();
    auto at = path.root_path();
    for (const auto& part : path.relative_path()) {
        at /= part;
        const auto attrs = GetFileAttributesW(at.c_str());
        Check(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "Installation reparse point or missing component");
    }
}
fs::path Root() {
    wchar_t* value = nullptr;
    Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &value)), "Program Files unavailable");
    const fs::path out = fs::path(value) / L"AegisEngineServices";
    CoTaskMemFree(value);
    return out;
}
void Protect(const fs::path& path) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)", SDDL_REVISION_1, &descriptor, nullptr)) WinError("Build installation ACL");
    PACL acl = nullptr; BOOL present = FALSE, inherited = FALSE;
    GetSecurityDescriptorDacl(descriptor, &present, &acl, &inherited);
    const auto error = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    LocalFree(descriptor);
    if (error) WinError("Protect installed engine files", error);
}
std::wstring Image(Kind kind, const fs::path& exe, const std::wstring& sid) {
    return L"\"" + exe.wstring() + L"\" --serve " + (kind == Kind::Antivirus ? L"antivirus" : L"sandbox") + L" --owner " + sid;
}
void VerifyOwned(const QUERY_SERVICE_CONFIGW& config, const fs::path& root, Kind kind) {
    int argc = 0;
    auto** argv = CommandLineToArgvW(config.lpBinaryPathName, &argc);
    bool valid = argv && argc == 5;
    if (valid) {
        const fs::path exe(argv[0]);
        valid = config.dwServiceType == SERVICE_WIN32_OWN_PROCESS && !_wcsicmp(config.lpServiceStartName, L"LocalSystem") &&
            !_wcsicmp(exe.filename().c_str(), L"guardian-engine-service.exe") &&
            !_wcsicmp(exe.parent_path().parent_path().c_str(), root.c_str()) &&
            std::wstring(argv[1]) == L"--serve" && std::wstring(argv[2]) == (kind == Kind::Antivirus ? L"antivirus" : L"sandbox") &&
            std::wstring(argv[3]) == L"--owner";
        if (valid) { try { Ordinary(exe); } catch (...) { valid = false; } }
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
void ReadPrevious(Previous& row, SC_HANDLE scm, Kind kind, const fs::path& root) {
    row.service.value = OpenServiceW(scm, Name(kind), SERVICE_ALL_ACCESS);
    if (!row.service.value) {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) return;
        WinError("Open existing Aegis service");
    }
    DWORD bytes = 0;
    QueryServiceConfigW(row.service.value, nullptr, 0, &bytes);
    Check(bytes && bytes <= 65536, "Existing service configuration limit");
    std::vector<uint8_t> data(bytes);
    auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data());
    if (!QueryServiceConfigW(row.service.value, config, bytes, &bytes)) WinError("Read existing service configuration");
    VerifyOwned(*config, root, kind);
    row.image = config->lpBinaryPathName; row.startup = config->dwStartType;
    row.running = Query(kind).Running();
    QueryServiceObjectSecurity(row.service.value, DACL_SECURITY_INFORMATION, nullptr, 0, &bytes);
    Check(bytes && bytes <= 65536, "Service security descriptor limit");
    row.security.resize(bytes);
    if (!QueryServiceObjectSecurity(row.service.value, DACL_SECURITY_INFORMATION,
        reinterpret_cast<PSECURITY_DESCRIPTOR>(row.security.data()), bytes, &bytes)) WinError("Read service permissions");
}
void Permissions(SC_HANDLE service, const std::wstring& owner) {
    // Owner can query/start/stop, never change ImagePath, account, or permissions.
    const auto text = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x200b5;;;" + owner + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) WinError("Build service permissions");
    const auto ok = SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor);
    const auto error = GetLastError(); LocalFree(descriptor);
    if (!ok) WinError("Apply service permissions", error);
}
}
int Install(const std::wstring& ownerArgument, bool uninstall) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(ownerArgument.c_str(), &sid)) WinError("Validate installation owner");
    const auto user = IsValidSid(sid) && !IsWellKnownSid(sid, WinLocalSystemSid) && !IsWellKnownSid(sid, WinLocalServiceSid) &&
        !IsWellKnownSid(sid, WinNetworkServiceSid) && !IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
    wchar_t* canonical = nullptr;
    if (!ConvertSidToStringSidW(sid, &canonical)) { LocalFree(sid); WinError("Canonicalize installation owner"); }
    const std::wstring owner(canonical); LocalFree(canonical);
    DWORD accountBytes = 0, domainBytes = 0; SID_NAME_USE accountType{};
    LookupAccountSidW(nullptr, sid, nullptr, &accountBytes, nullptr, &domainBytes, &accountType);
    std::vector<wchar_t> account(accountBytes), domain(domainBytes);
    const auto resolved = accountBytes && LookupAccountSidW(nullptr, sid, account.data(), &accountBytes, domain.data(), &domainBytes, &accountType);
    LocalFree(sid); Check(user && resolved && accountType == SidTypeUser, "A desktop user SID is required");
    const auto root = Root();
    ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE)};
    if (!scm.value) WinError("Administrator authorization is required to install services");
    const std::array kinds{Kind::Antivirus, Kind::Sandbox};
    std::array<Previous, 2> previous;
    for (size_t i = 0; i < kinds.size(); ++i) ReadPrevious(previous[i], scm.value, kinds[i], root);
    if (uninstall) {
        for (size_t i = 0; i < kinds.size(); ++i) if (previous[i].service.value) {
            SetRunning(kinds[i], false);
            if (!DeleteService(previous[i].service.value)) WinError("Remove Aegis service registration");
        }
        // Retain versioned files and reports for recovery; no recursive deletion.
        return 0;
    }
    Ordinary(root.parent_path());
    const bool fresh = fs::create_directory(root);
    Ordinary(root);
    const auto marker = root / L"aegis-engine-services.txt";
    if (!fresh) {
        Ordinary(marker);
        std::ifstream stream(marker); std::string line; std::getline(stream, line);
        Check(line == "Aegis Engine Services v1", "Installation directory belongs to another application");
    }
    Protect(root);
    if (fresh) { std::ofstream stream(marker); stream << "Aegis Engine Services v1\n"; Check(bool(stream), "Cannot write installation marker"); }
    const auto slot = root / (L"install-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(GetCurrentProcessId()));
    Check(fs::create_directory(slot), "A new installation slot is required");
    Ordinary(slot);
    const auto source = Executable().parent_path();
    for (const auto* name : PackageFiles) {
        const auto file = source / name; Ordinary(file);
        Check(fs::is_regular_file(file), "Required service package file missing");
        Check(fs::copy_file(file, slot / name, fs::copy_options::none), "Cannot copy service package");
    }
    // Optional app-local Microsoft redistributables, supplied by the package.
    for (const auto* name : RuntimeFiles)
        if (fs::exists(source / name)) { Ordinary(source / name); Check(fs::copy_file(source / name, slot / name), "Cannot copy C++ runtime"); }
    // Runtime assets are data. Helpers are installed exclusively from PackageFiles.
    if(fs::exists(source/L"runtime")){
        Ordinary(source/L"runtime");const auto indexPath=source/L"runtime"/L"index.json";Ordinary(indexPath);Check(fs::file_size(indexPath)<=2u<<20,"Runtime index quota");std::ifstream stream(indexPath);const auto index=Json::parse(stream);Check(index.value("schema",0)==1&&index.at("applications").is_object()&&index.at("applications").size()<=16,"Installed runtime index rejected");fs::create_directory(slot/L"runtime");Check(fs::copy_file(indexPath,slot/L"runtime"/L"index.json"),"Copy runtime index");uint64_t total=0;
        for(const auto& record:index.at("applications")){const auto& files=record.at("files");Check(files.is_object()&&files.size()<=12,"Installed runtime member quota");for(auto it=files.begin();it!=files.end();++it){auto part=fs::path(Wide(it.key()));Check(!part.empty()&&!part.is_absolute()&&!part.has_root_name()&&it.key().find(':')==std::string::npos,"Installed runtime path rejected");auto member=source/L"runtime";for(const auto& component:part){Check(component!=L".."&&component!=L".","Installed runtime traversal rejected");member/=component;Ordinary(member);}Check(fs::is_regular_file(member)&&fs::file_size(member)<=64u<<20,"Installed runtime member size rejected");total+=fs::file_size(member);Check(total<=512ull<<20,"Installed runtime total quota");const auto target=slot/L"runtime"/part;fs::create_directories(target.parent_path());if(!fs::exists(target))Check(fs::copy_file(member,target),"Copy runtime member");}}
    }
    const auto exe = slot / L"guardian-engine-service.exe";
    try {
        for (size_t i = 0; i < kinds.size(); ++i) if (previous[i].service.value) SetRunning(kinds[i], false);
        for (size_t i = 0; i < kinds.size(); ++i) {
            auto& row = previous[i]; const auto kind = kinds[i]; const auto image = Image(kind, exe, owner);
            if (row.service.value) {
                if (!ChangeServiceConfigW(row.service.value, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                    image.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, Label(kind))) WinError("Update Aegis service");
            } else {
                row.service.value = CreateServiceW(scm.value, Name(kind), Label(kind), SERVICE_ALL_ACCESS,
                    SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, image.c_str(),
                    nullptr, nullptr, nullptr, nullptr, nullptr);
                if (!row.service.value) WinError("Register Aegis service");
                row.created = true;
            }
            row.changed = true;
            Permissions(row.service.value, owner);
            wchar_t antivirusDescription[] = L"Análises do antivírus Aegis em processos restritos do usuário.";
            wchar_t sandboxDescription[] = L"Execuções MiniNT/WHP supervisionadas; interfaces na sessão do usuário.";
            SERVICE_DESCRIPTIONW description{kind == Kind::Antivirus ? antivirusDescription : sandboxDescription};
            if (!ChangeServiceConfig2W(row.service.value, SERVICE_CONFIG_DESCRIPTION, &description)) WinError("Describe Aegis service");
        }
        for (size_t i = 0; i < kinds.size(); ++i) if (previous[i].running) SetRunning(kinds[i], true);
    } catch (...) {
        for (size_t i = 0; i < kinds.size(); ++i)
            if (previous[i].changed) try { SetRunning(kinds[i], false); } catch (...) {}
        for (size_t i = 0; i < kinds.size(); ++i) {
            auto& row = previous[i];
            if (row.created) DeleteService(row.service.value);
            else if (row.changed) {
                ChangeServiceConfigW(row.service.value, SERVICE_NO_CHANGE, row.startup, SERVICE_NO_CHANGE,
                    row.image.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                SetServiceObjectSecurity(row.service.value, DACL_SECURITY_INFORMATION, row.security.data());
            }
            if (!row.created && row.running) try { SetRunning(kinds[i], true); } catch (...) {}
        }
        throw;
    }
    return 0;
}
} // namespace av::services
