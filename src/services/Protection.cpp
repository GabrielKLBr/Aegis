#include "Protection.h"
#include "DirectoryChanges.h"
#include "quarantine/Store.h"
#include "scanner/Scanner.h"
#include <aclapi.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <sddl.h>
#include <set>
#include <shlobj.h>
#include <sqlite3.h>
#include <thread>
#include <vector>
#include <wtsapi32.h>

namespace av::services {
UserAccess::UserAccess(HANDLE token) {
    Check(token != nullptr, "Caller token required");
    if (!ImpersonateLoggedOnUser(token))
        WinError("Authorize caller file access");
    active_ = true;
}
UserAccess::~UserAccess() {
    if (active_ && !RevertToSelf())
        TerminateProcess(GetCurrentProcess(), ERROR_CANNOT_IMPERSONATE);
}
fs::path LocalPath(const fs::path &path) {
    const auto s = path.wstring();
    Check(s.size() >= 3 && s.size() < 240 && iswalpha(s[0]) && s[1] == L':' && s[2] == L'\\' &&
              s.find(L'\0') == s.npos,
          "A local absolute path shorter than 240 characters is required");
    Check(path == path.lexically_normal(), "Path normalization/traversal rejected");
    for (size_t i = 2; i < s.size(); ++i)
        Check(s[i] >= 32 && s[i] != L':' && s[i] != L'*' && s[i] != L'?' && s[i] != L'"' && s[i] != L'|' &&
                  s[i] != L'<' && s[i] != L'>',
              "Path character rejected");
    for (const auto &part : path.relative_path()) {
        auto n = part.wstring();
        Check(!n.empty() && n != L"." && n != L".." && n.back() != L'.' && n.back() != L' ',
              "Path component rejected");
    }
    const auto type = GetDriveTypeW(path.root_path().c_str());
    Check(type == DRIVE_FIXED || type == DRIVE_REMOVABLE, "Network/device paths are not allowed");
    return path;
}
bool Beneath(const fs::path &path, const fs::path &root) {
    auto p = path.wstring(), r = root.wstring();
    if (r.back() != L'\\')
        r += L'\\';
    return CompareStringOrdinal(path.c_str(), -1, root.c_str(), -1, TRUE) == CSTR_EQUAL ||
           (p.size() > r.size() &&
            CompareStringOrdinal(p.c_str(), int(r.size()), r.c_str(), int(r.size()), TRUE) == CSTR_EQUAL);
}
void OrdinaryPath(const fs::path &input) {
    auto path = LocalPath(input);
    auto at = path.root_path();
    for (const auto &part : path.relative_path()) {
        at /= part;
        const auto a = GetFileAttributesW(at.c_str());
        Check(a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_REPARSE_POINT),
              "Missing path or reparse point rejected");
    }
}
Json CallerJson(const fs::path &path, HANDLE token) {
    UserAccess access(token);
    OrdinaryPath(path);
    auto j = scan::ReadJson(path, 65536);
    Check(j.is_object() && j.value("schema", 0) == 1, "Protection request schema rejected");
    return j;
}
fs::path ProtectionRoot() {
    PWSTR p = nullptr;
    Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &p)), "ProgramData unavailable");
    fs::path out = fs::path(p) / L"AegisProtection16";
    CoTaskMemFree(p);
    return out;
}
namespace {
int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
void SecureDirectory(const fs::path &path) {
    fs::create_directory(path);
    OrdinaryPath(path);
    PSECURITY_DESCRIPTOR existing = nullptr;
    PSID owner = nullptr;
    const auto read =
        GetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                              &owner, nullptr, nullptr, nullptr, &existing);
    Check(read == ERROR_SUCCESS, "Read protected state ownership");
    BYTE system[SECURITY_MAX_SID_SIZE]{};
    DWORD bytes = sizeof(system);
    CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &bytes);
    BYTE admins[SECURITY_MAX_SID_SIZE]{};
    DWORD adminBytes = sizeof(admins);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &adminBytes);
    const bool safe = owner && (EqualSid(owner, system) || EqualSid(owner, admins));
    LocalFree(existing);
    Check(safe, "Protection state must be owned by SYSTEM; existing directory was not trusted");
    PSECURITY_DESCRIPTOR sd = nullptr;
    Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",
                                                               SDDL_REVISION_1, &sd, nullptr) != FALSE,
          "Build protected state ACL");
    BOOL present = FALSE, def = FALSE;
    PACL acl = nullptr;
    GetSecurityDescriptorDacl(sd, &present, &acl, &def);
    const auto error = SetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT,
                                             OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                                                 PROTECTED_DACL_SECURITY_INFORMATION,
                                             system, nullptr, acl, nullptr);
    LocalFree(sd);
    if (error)
        WinError("Protect service state", error);
}
struct Statement {
    sqlite3_stmt *p = nullptr;
    Statement(sqlite3 *db, const char *sql) {
        Check(sqlite3_prepare_v2(db, sql, -1, &p, nullptr) == SQLITE_OK, "Prepare protection state");
    }
    ~Statement() { sqlite3_finalize(p); }
    void Text(int n, const std::string &s) {
        Check(sqlite3_bind_text(p, n, s.data(), int(s.size()), SQLITE_TRANSIENT) == SQLITE_OK,
              "Bind protection state");
    }
    void Number(int n, int64_t v) {
        Check(sqlite3_bind_int64(p, n, v) == SQLITE_OK, "Bind protection number");
    }
    int Step() {
        const auto n = sqlite3_step(p);
        Check(n == SQLITE_ROW || n == SQLITE_DONE, "Protection storage unavailable/full");
        return n;
    }
    std::string String(int n) {
        auto s = sqlite3_column_text(p, n);
        return s ? reinterpret_cast<const char *>(s) : "";
    }
};
void Sql(sqlite3 *db, const char *sql) {
    Check(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK,
          "Protection transaction unavailable/full");
}
struct Transaction {
    sqlite3 *db;
    bool committed = false;
    explicit Transaction(sqlite3 *p) : db(p) { Sql(db, "BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (!committed)
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void Commit() {
        Sql(db, "COMMIT");
        committed = true;
    }
};
std::vector<fs::path> Paths(const Json &array) {
    Check(array.is_array() && array.size() <= 16, "At most sixteen paths are supported");
    std::vector<fs::path> out;
    for (const auto &p : array) {
        Check(p.is_string(), "Path must be text");
        auto path = LocalPath(scan::Utf8Path(p.get<std::string>()));
        if (std::find(out.begin(), out.end(), path) == out.end())
            out.push_back(path);
    }
    return out;
}
bool Excluded(const fs::path &p, const std::vector<fs::path> &exclusions) {
    for (const auto &e : exclusions)
        if (Beneath(p, e))
            return true;
    return false;
}
Json Defaults() {
    return {{"schema", 1},
            {"enabled", false},
            {"roots", Json::array()},
            {"quickRoots", Json::array()},
            {"exclusions", Json::array()},
            {"autoQuarantine", false},
            {"maxFiles", 10000},
            {"maxSeconds", 1800},
            {"schedules", Json::array()}};
}
Json Policy(const Json &input, HANDLE token) {
    auto value = Defaults();
    for (auto it = input.begin(); it != input.end(); ++it) {
        if (it.key() == "owner") {
            Check(it.value() == Text(UserSid(token)), "Policy owner mismatch");
            continue;
        }
        Check(value.contains(it.key()), "Unknown protection policy field");
        value[it.key()] = it.value();
    }
    Check(value.at("enabled").is_boolean() && value.at("autoQuarantine").is_boolean(),
          "Protection flags must be booleans");
    auto roots = Paths(value.at("roots"));
    Paths(value.at("quickRoots"));
    Paths(value.at("exclusions"));
    Check(!value.at("enabled").get<bool>() || !roots.empty(), "Enabled protection needs at least one root");
    Check(value.at("maxFiles").is_number_integer() && value.at("maxFiles").get<int64_t>() >= 1 &&
              value.at("maxFiles").get<int64_t>() <= 100000,
          "Scan file quota rejected");
    Check(value.at("maxSeconds").is_number_integer() && value.at("maxSeconds").get<int64_t>() >= 10 &&
              value.at("maxSeconds").get<int64_t>() <= 86400,
          "Scan time quota rejected");
    {
        UserAccess access(token);
        for (const auto &r : roots) {
            OrdinaryPath(r);
            Check(fs::is_directory(r), "A watched root must be a directory");
        }
    }
    auto &schedules = value.at("schedules");
    Check(schedules.is_array() && schedules.size() <= 16, "Schedule quota exceeded");
    std::set<std::string> ids;
    for (auto &s : schedules) {
        Check(s.is_object(), "Schedule must be an object");
        const auto id = s.at("id").get<std::string>();
        Check(!id.empty() && id.size() <= 64 && ids.insert(id).second, "Schedule identity rejected");
        auto mode = s.at("mode").get<std::string>();
        Check(mode == "quick" || mode == "full" || mode == "custom", "Schedule scan mode rejected");
        auto interval = s.at("intervalSeconds").get<int64_t>();
        Check(interval >= 60 && interval <= 604800, "Schedule interval must be 60 seconds to seven days");
        Paths(s.value("paths", Json::array()));
        if (mode == "custom")
            Check(!s.at("paths").empty(), "Custom schedule needs paths");
        Check(s.value("nextRun", Now() + interval) >= 0, "Schedule time rejected");
        s["nextRun"] = s.value("nextRun", Now() + interval);
    }
    return value;
}
HANDLE OwnerToken(const std::wstring &owner) {
    PWTS_SESSION_INFOW list = nullptr;
    DWORD count = 0;
    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &list, &count))
        return nullptr;
    HANDLE found = nullptr;
    for (DWORD i = 0; i < count && !found; ++i) {
        if (list[i].SessionId == 0)
            continue;
        Handle token;
        if (WTSQueryUserToken(list[i].SessionId, &token.value)) {
            try {
                if (UserSid(token.value) == owner)
                    found = RestrictUserToken(token.value);
            } catch (...) {
            }
        }
    }
    WTSFreeMemory(list);
    return found;
}
struct Watch {
    Handle directory, event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED io{};
    std::vector<uint8_t> buffer = std::vector<uint8_t>(65536);
    fs::path root;
    BY_HANDLE_FILE_INFORMATION identity{};
    Watch(const fs::path &p, HANDLE token) : root(p) {
        UserAccess access(token);
        OrdinaryPath(p);
        directory.value = CreateFileW(
            p.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (directory.value == INVALID_HANDLE_VALUE)
            WinError("Open watched root");
        Check(event.value != nullptr, "Watch event unavailable");
        Check(GetFileInformationByHandle(directory.value, &identity) != FALSE &&
                  !(identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
              "Watch directory identity rejected");
        try {
            Arm();
        } catch (...) {
            CancelIoEx(directory.value, &io);
            DWORD ignored = 0;
            GetOverlappedResult(directory.value, &io, &ignored, TRUE);
            throw;
        }
    }
    bool Current(HANDLE token) {
        try {
            UserAccess access(token);
            OrdinaryPath(root);
            Handle current(CreateFileW(
                root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (current.value == INVALID_HANDLE_VALUE)
                return false;
            BY_HANDLE_FILE_INFORMATION actual{};
            return GetFileInformationByHandle(current.value, &actual) &&
                   !(actual.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                   actual.dwVolumeSerialNumber == identity.dwVolumeSerialNumber &&
                   actual.nFileIndexHigh == identity.nFileIndexHigh &&
                   actual.nFileIndexLow == identity.nFileIndexLow;
        } catch (...) {
            return false;
        }
    }
    void Arm() {
        ResetEvent(event.value);
        io = {};
        io.hEvent = event.value;
        if (!ReadDirectoryChangesW(directory.value, buffer.data(), DWORD(buffer.size()), TRUE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                       FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                   nullptr, &io, nullptr))
            WinError("Arm directory notifications");
    }
    ~Watch() {
        if (directory.value && directory.value != INVALID_HANDLE_VALUE) {
            CancelIoEx(directory.value, &io);
            DWORD ignored = 0;
            GetOverlappedResult(directory.value, &io, &ignored, TRUE);
        }
    }
};
} // namespace
struct Protection::Impl {
    std::wstring owner;
    HANDLE stop;
    fs::path root, vault;
    sqlite3 *db = nullptr;
    std::mutex mutex, vaultMutex;
    std::thread thread, executor, manualExecutor;
    std::atomic<bool> busy{false}, manualBusy{false};
    Handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)},
        manualCancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> stopping{false};
    bool sessionAvailable = false;
    size_t watchCount = 0;
    std::string active, manualActive, lastError;
    uint64_t generation = 0;
    Json policy;
    std::map<std::wstring, int64_t> dirty;
    std::map<std::wstring, bool> pendingRescan;
    explicit Impl(const std::wstring &who, HANDLE event)
        : owner(who), stop(event), root(ProtectionRoot()), vault(root / L"quarantine") {
        Check(UserSid() == L"S-1-5-18", "Protection supervisor must run as SYSTEM");
        Check(cancel.value != nullptr && manualCancel.value != nullptr,
              "Protection cancellation unavailable");
        SecureDirectory(root);
        SecureDirectory(vault);
        const auto path = root / L"state.sqlite";
        const bool databaseExisted = fs::exists(path);
        const auto marker = root / L"state-initialized.v1";
        Check(databaseExisted || !fs::exists(marker), "Protection state missing; automatic reset refused");
        Check(!databaseExisted || fs::file_size(path) > 0, "Protection state empty; automatic reset refused");
        if (databaseExisted) {
            OrdinaryPath(path);
            Handle file(CreateFileW(path.c_str(), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            Check(file.value != INVALID_HANDLE_VALUE, "Open existing protection state");
            BY_HANDLE_FILE_INFORMATION info{};
            Check(GetFileInformationByHandle(file.value, &info) && info.nNumberOfLinks == 1 &&
                      !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)),
                  "Protection state links rejected");
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            PSID fileOwner = nullptr;
            Check(GetSecurityInfo(file.value, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &fileOwner, nullptr,
                                  nullptr, nullptr, &descriptor) == ERROR_SUCCESS,
                  "Inspect protection state owner");
            BYTE sys[SECURITY_MAX_SID_SIZE]{}, admins[SECURITY_MAX_SID_SIZE]{};
            DWORD n = sizeof(sys), m = sizeof(admins);
            CreateWellKnownSid(WinLocalSystemSid, nullptr, sys, &n);
            CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &m);
            const bool trusted = fileOwner && (EqualSid(fileOwner, sys) || EqualSid(fileOwner, admins));
            LocalFree(descriptor);
            Check(trusted, "Existing protection state is not owned by SYSTEM/Administrators");
        }
        Check(sqlite3_open_v2(scan::PathText(path).c_str(), &db,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                              nullptr) == SQLITE_OK,
              "Open protected protection state");
        sqlite3_busy_timeout(db, 2000);
        sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
        Sql(db, "PRAGMA trusted_schema=OFF;PRAGMA journal_mode=DELETE;PRAGMA synchronous=FULL;PRAGMA "
                "max_page_count=32768;CREATE TABLE IF NOT EXISTS settings(id INTEGER PRIMARY KEY "
                "CHECK(id=1),value TEXT NOT NULL);CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY,value "
                "TEXT NOT NULL);CREATE TABLE IF NOT EXISTS events(seq INTEGER PRIMARY KEY "
                "AUTOINCREMENT,value TEXT NOT NULL);");
        {
            Statement check(db, "PRAGMA quick_check");
            Check(check.Step() == SQLITE_ROW && check.String(0) == "ok",
                  "Protection database integrity failure");
        }
        Statement settings(db, "SELECT value FROM settings WHERE id=1");
        if (settings.Step() == SQLITE_ROW) {
            policy = Json::parse(settings.String(0));
            Check(policy.value("owner", std::string()) == Text(owner),
                  "Protection installation owner mismatch");
        } else {
            Check(!databaseExisted, "Protection settings missing; automatic reset refused");
            policy = Defaults();
            policy["owner"] = Text(owner);
            SavePolicy();
        }
        {
            Transaction tx(db);
            Statement rows(db, "SELECT id,value FROM jobs");
            std::vector<Json> recovered;
            while (rows.Step() == SQLITE_ROW) {
                auto j = Json::parse(rows.String(1));
                if (j.at("state") == "running") {
                    j["state"] = "queued";
                    j["recovered"] = true;
                    j["attempts"] = j.value("attempts", 0) + 1;
                    recovered.push_back(j);
                }
            }
            for (const auto &j : recovered)
                SaveJob(j);
            Event("service-started",
                  {{"recoveredJobs", recovered.size()}, {"rescanOnStart", policy.at("enabled")}});
            tx.Commit();
        }
        if (!fs::exists(marker)) {
            Handle initialized(CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            Check(initialized.value != INVALID_HANDLE_VALUE, "Create protection state marker");
            const char text[] = "Aegis protection state v1";
            DWORD written = 0;
            Check(WriteFile(initialized.value, text, sizeof(text), &written, nullptr) &&
                      written == sizeof(text) && FlushFileBuffers(initialized.value),
                  "Flush protection state marker");
        }
        thread = std::thread([this] { Loop(); });
    }
    ~Impl() {
        stopping = true;
        SetEvent(cancel.value);
        SetEvent(manualCancel.value);
        if (thread.joinable())
            thread.join();
        if (db)
            sqlite3_close_v2(db);
    }
    void SavePolicy() {
        Statement s(db, "INSERT OR REPLACE INTO settings VALUES(1,?)");
        s.Text(1, policy.dump());
        s.Step();
    }
    void SaveJob(const Json &j) {
        Statement s(db, "INSERT OR REPLACE INTO jobs VALUES(?,?)");
        s.Text(1, j.at("id").get<std::string>());
        s.Text(2, j.dump());
        s.Step();
    }
    void Event(const std::string &action, Json fields = Json::object()) {
        fields["action"] = action;
        fields["time"] = Now();
        if (action == "threat-detected" || action == "coverage-gap" || action == "protection-error") {
            const auto source = RegisterEventSourceW(nullptr, L"AegisProtection16");
            bool sent = false;
            if (source) {
                Json notice = {
                    {"phase", "16"}, {"action", action}, {"job", fields.value("job", std::string())}};
                if (fields.contains("path"))
                    notice["file"] =
                        scan::PathText(scan::Utf8Path(fields.at("path").get<std::string>()).filename());
                auto text = Wide(notice.dump());
                const wchar_t *strings[] = {text.c_str()};
                sent = ReportEventW(source, EVENTLOG_WARNING_TYPE, 0, 1, nullptr, 1, 0, strings, nullptr) !=
                       FALSE;
                DeregisterEventSource(source);
            }
            fields["windowsEventPublished"] = sent;
        }
        Statement s(db, "INSERT INTO events(value) VALUES(?)");
        s.Text(1, fields.dump());
        s.Step();
        Sql(db, "DELETE FROM events WHERE seq<=(SELECT COALESCE(max(seq),0)-4096 FROM events)");
    }
    Json Job(const std::string &id) {
        Statement s(db, "SELECT value FROM jobs WHERE id=?");
        s.Text(1, id);
        Check(s.Step() == SQLITE_ROW, "Protection job not found");
        return Json::parse(s.String(0));
    }
    Json Enqueue(Json request, const std::string &origin) {
        const auto mode = request.value("mode", std::string("custom"));
        Check(mode == "quick" || mode == "full" || mode == "custom", "Scan mode rejected");
        const auto paths = Paths(request.value("paths", Json::array()));
        if (mode == "custom")
            Check(!paths.empty(), "Custom scan needs at least one path");
        Statement rows(db, "SELECT value FROM jobs");
        size_t pending = 0, manual = 0;
        std::vector<std::string> finished;
        while (rows.Step() == SQLITE_ROW) {
            auto j = Json::parse(rows.String(0));
            if (j.at("state") == "queued" || j.at("state") == "running") {
                ++pending;
                if (j.at("origin") == "manual")
                    ++manual;
                if (origin != "manual" && j.at("origin") == origin &&
                    j.at("paths") == request.value("paths", Json::array()) && j.at("state") == "queued")
                    return j;
            } else
                finished.push_back(j.at("id"));
        }
        Check(pending < 128 && (origin != "manual" || manual < 16),
              "Protection queue full; automatic rescans remain required");
        if (finished.size() > 512)
            for (size_t i = 0; i < finished.size() - 512; ++i) {
                Statement d(db, "DELETE FROM jobs WHERE id=?");
                d.Text(1, finished[i]);
                d.Step();
            }
        Json j = {{"id", scan::Token()},
                  {"mode", mode},
                  {"paths", request.value("paths", Json::array())},
                  {"origin", origin},
                  {"state", "queued"},
                  {"created", Now()},
                  {"filesScanned", 0},
                  {"threatsFound", 0},
                  {"inconclusive", 0},
                  {"excluded", 0},
                  {"quarantined", 0},
                  {"attempts", 0},
                  {"complete", false},
                  {"cancelRequested", false}};
        SaveJob(j);
        Event("scan-queued", {{"job", j.at("id")}, {"origin", origin}});
        return j;
    }
    Json Status(bool admin) {
        Json jobs = Json::array();
        Statement s(db, "SELECT value FROM jobs ORDER BY rowid DESC LIMIT 64");
        while (s.Step() == SQLITE_ROW)
            jobs.push_back(Json::parse(s.String(0)));
        return {{"schema", 1},
                {"type", "protection"},
                {"complete", true},
                {"enabled", policy.at("enabled")},
                {"sessionAvailable", sessionAvailable},
                {"watchingRoots", watchCount},
                {"monitoringActive", policy.at("enabled").get<bool>() && sessionAvailable &&
                                         watchCount == policy.at("roots").size() && lastError.empty()},
                {"activeJob", manualActive.empty() ? active : manualActive},
                {"activeAutomaticJob", active},
                {"activeManualJob", manualActive},
                {"lastError", lastError},
                {"policy", policy},
                {"jobs", jobs},
                {"administrator", admin},
                {"protectedVault", true},
                {"model", "post-change-user-session"},
                {"rescanPending", !pendingRescan.empty()},
                {"servicePid", GetCurrentProcessId()}};
    }
    Json Events(uint64_t after) {
        Json result = {
            {"schema", 1}, {"type", "protection-events"}, {"complete", true}, {"events", Json::array()}};
        Statement first(db, "SELECT COALESCE(min(seq),0),COALESCE(max(seq),0) FROM events");
        first.Step();
        const auto oldest = uint64_t(sqlite3_column_int64(first.p, 0)),
                   last = uint64_t(sqlite3_column_int64(first.p, 1));
        result["cursorGap"] = after && oldest > after + 1;
        result["oldestSequence"] = oldest;
        result["lastSequence"] = last;
        uint64_t next = after;
        Statement rows(db, "SELECT seq,value FROM events WHERE seq>? ORDER BY seq LIMIT 128");
        rows.Number(1, int64_t((std::min)(after, uint64_t(INT64_MAX))));
        while (rows.Step() == SQLITE_ROW) {
            next = uint64_t(sqlite3_column_int64(rows.p, 0));
            auto e = Json::parse(rows.String(1));
            e["seq"] = next;
            result["events"].push_back(e);
        }
        result["next"] = next;
        return result;
    }
    void MarkGap(const fs::path &path, const std::string &reason) {
        pendingRescan[path.wstring()] = true;
        Event("coverage-gap", {{"path", scan::PathText(path)}, {"reason", reason}, {"rescanRequired", true}});
    }
    void RunJob(Json j, HANDLE token, HANDLE jobCancellation) {
        const bool manual = j.at("origin") == "manual";
        Json current;
        {
            std::lock_guard lock(mutex);
            current = policy;
            j = Job(j.at("id"));
            if (j.at("state") == "cancelled" ||
                (j.at("origin") != "manual" && !policy.at("enabled").get<bool>())) {
                j["state"] = "cancelled";
                SaveJob(j);
                return;
            }
            j["state"] = "running";
            j["started"] = Now();
            j["filesScanned"] = 0;
            j["threatsFound"] = 0;
            j["inconclusive"] = 0;
            j["excluded"] = 0;
            j["quarantined"] = 0;
            (manual ? manualActive : active) = j.at("id");
            ResetEvent(jobCancellation);
            SaveJob(j);
            Event("scan-started", {{"job", j.at("id")}});
        }
        auto exclusions = Paths(current.at("exclusions"));
        exclusions.push_back(root);
        exclusions.push_back(Executable().parent_path());
        std::vector<fs::path> roots;
        if (j.at("mode") == "full") {
            wchar_t drives[512]{};
            const auto n = GetLogicalDriveStringsW(512, drives);
            Check(n && n < 512, "Enumerate local volumes");
            for (auto *p = drives; *p; p += wcslen(p) + 1)
                if (GetDriveTypeW(p) == DRIVE_FIXED)
                    roots.emplace_back(p);
        } else if (j.at("mode") == "quick") {
            roots = Paths(current.at("quickRoots"));
            if (roots.empty())
                roots = Paths(current.at("roots"));
            if (roots.empty()) {
                UserAccess access(token);
                for (const auto &folder : {FOLDERID_Downloads, FOLDERID_Desktop}) {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(SHGetKnownFolderPath(folder, 0, token, &p))) {
                        roots.emplace_back(p);
                        CoTaskMemFree(p);
                    }
                }
            }
        } else
            roots = Paths(j.at("paths"));
        const auto deadline = GetTickCount64() + current.at("maxSeconds").get<uint64_t>() * 1000;
        const auto maxFiles = current.at("maxFiles").get<uint64_t>();
        bool limited = false;
        auto interrupted = [&] {
            return stopping || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 ||
                   WaitForSingleObject(jobCancellation, 0) == WAIT_OBJECT_0;
        };
        auto inspect = [&](const fs::path &path) {
            if (interrupted())
                return false;
            if (GetTickCount64() >= deadline || j.at("filesScanned").get<uint64_t>() >= maxFiles) {
                limited = true;
                return false;
            }
            if (Excluded(path, exclusions)) {
                j["excluded"] = j.at("excluded").get<uint64_t>() + 1;
                return true;
            }
            try {
                auto result =
                    RunRestricted(token, manual ? Operation::ProtectionManualFile : Operation::ProtectionFile,
                                  path, jobCancellation);
                j["filesScanned"] = j.at("filesScanned").get<uint64_t>() + 1;
                const auto classification = result.value("classification", std::string("inconclusive"));
                if (!result.value("complete", false))
                    j["inconclusive"] = j.at("inconclusive").get<uint64_t>() + 1;
                if (classification == "detected") {
                    j["threatsFound"] = j.at("threatsFound").get<uint64_t>() + 1;
                    Json detected = {{"job", j.at("id")},
                                     {"path", scan::PathText(path)},
                                     {"sha256", result.value("sha256", std::string())},
                                     {"classification", classification}};
                    if (current.at("autoQuarantine").get<bool>() && result.value("complete", false) &&
                        !interrupted()) {
                        try {
                            std::lock_guard vaultLock(vaultMutex);
                            quarantine::Store store(vault, token);
                            auto q = store.Isolate(path, result.at("sha256").get<std::string>(),
                                                   result.value("threat", std::string("Detected")),
                                                   "Proteção em tempo real/varredura do serviço");
                            detected["quarantine"] = q;
                            if (q.value("complete", false))
                                j["quarantined"] = j.at("quarantined").get<uint64_t>() + 1;
                        } catch (const std::exception &e) {
                            detected["quarantineError"] = e.what();
                            j["inconclusive"] = j.at("inconclusive").get<uint64_t>() + 1;
                        }
                    }
                    std::lock_guard lock(mutex);
                    Event("threat-detected", detected);
                }
                {
                    std::lock_guard lock(mutex);
                    j["currentFile"] = scan::PathText(path);
                    auto persisted = Job(j.at("id"));
                    j["cancelRequested"] = persisted.value("cancelRequested", false);
                    SaveJob(j);
                }
            } catch (const std::exception &e) {
                j["inconclusive"] = j.at("inconclusive").get<uint64_t>() + 1;
                j["lastError"] = e.what();
            }
            return !interrupted();
        };
        try {
            for (const auto &path : roots) {
                if (interrupted() || limited)
                    break;
                if (Excluded(path, exclusions)) {
                    j["excluded"] = j.at("excluded").get<uint64_t>() + 1;
                    continue;
                }
                std::vector<fs::path> files;
                {
                    UserAccess access(token);
                    OrdinaryPath(path);
                    std::error_code error;
                    if (fs::is_regular_file(path, error))
                        files.push_back(path);
                    else {
                        fs::recursive_directory_iterator it(path, fs::directory_options::none, error), end;
                        if (error)
                            j["inconclusive"] = j.at("inconclusive").get<uint64_t>() + 1;
                        while (it != end && !interrupted()) {
                            if (files.size() >= maxFiles || GetTickCount64() >= deadline) {
                                limited = true;
                                break;
                            }
                            auto file = it->path();
                            const auto attributes = GetFileAttributesW(file.c_str());
                            if (attributes == INVALID_FILE_ATTRIBUTES ||
                                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) || Excluded(file, exclusions) ||
                                it.depth() >= 64) {
                                if (it->is_directory(error))
                                    it.disable_recursion_pending();
                                j["excluded"] = j.at("excluded").get<uint64_t>() + 1;
                            } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
                                files.push_back(file);
                            it.increment(error);
                            if (error) {
                                j["inconclusive"] = j.at("inconclusive").get<uint64_t>() + 1;
                                error.clear();
                            }
                        }
                    }
                }
                for (const auto &file : files)
                    if (!inspect(file))
                        break;
            }
        } catch (const std::exception &e) {
            j["lastError"] = e.what();
            j["inconclusive"] = j.at("inconclusive").get<uint64_t>() + 1;
        }
        {
            std::lock_guard lock(mutex);
            const bool cancelled = interrupted();
            j["state"] = cancelled ? (stopping || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 ? "queued"
                                                                                                : "cancelled")
                                   : "completed";
            j["limited"] = limited;
            j["complete"] = !cancelled && !limited && j.at("inconclusive") == 0;
            j["finished"] = Now();
            if (j.at("state") == "queued")
                j["recovered"] = true;
            SaveJob(j);
            Event(cancelled ? "scan-interrupted" : "scan-finished", {{"job", j.at("id")},
                                                                     {"complete", j.at("complete")},
                                                                     {"limited", limited},
                                                                     {"state", j.at("state")}});
            (manual ? manualActive : active).clear();
        }
    }
    void Loop() {
        std::vector<std::unique_ptr<Watch>> watches;
        uint64_t applied = UINT64_MAX;
        Handle token;
        int64_t nextSession = 0, nextIdentityCheck = 0;
        while (!stopping && WaitForSingleObject(stop, 100) != WAIT_OBJECT_0) {
            try {
                if (Now() >= nextSession) {
                    Handle candidate(OwnerToken(owner));
                    nextSession = Now() + 5;
                    std::lock_guard lock(mutex);
                    sessionAvailable = candidate.value != nullptr;
                    if (!busy && !manualBusy) {
                        if (token.value)
                            CloseHandle(token.release());
                        token.value = candidate.release();
                        if (!token.value) {
                            watches.clear();
                            watchCount = 0;
                            applied = UINT64_MAX;
                        }
                    }
                }
                Json work, manualWork;
                bool run = false, runManual = false;
                {
                    std::lock_guard lock(mutex);
                    if (token.value && Now() >= nextIdentityCheck) {
                        nextIdentityCheck = Now() + 2;
                        for (const auto &watch : watches)
                            if (!watch->Current(token.value)) {
                                MarkGap(watch->root, "watch-root-moved-or-replaced");
                                applied = UINT64_MAX;
                            }
                    }
                    if (applied != generation && token.value) {
                        watches.clear();
                        dirty.clear();
                        lastError.clear();
                        applied = generation;
                        if (policy.at("enabled").get<bool>())
                            for (const auto &path : Paths(policy.at("roots"))) {
                                try {
                                    watches.push_back(std::make_unique<Watch>(path, token.value));
                                    MarkGap(path, "startup-or-policy-change");
                                } catch (const std::exception &e) {
                                    lastError = e.what();
                                    MarkGap(path, "watch-unavailable");
                                }
                            }
                    }
                    watchCount = watches.size();
                    for (auto &watch : watches)
                        if (WaitForSingleObject(watch->event.value, 0) == WAIT_OBJECT_0) {
                            DWORD count = 0;
                            const bool ok = GetOverlappedResult(watch->directory.value, &watch->io, &count,
                                                                FALSE) != FALSE;
                            if (!ok || !count)
                                MarkGap(watch->root, "notification-buffer-overflow-or-error");
                            else {
                                auto decoded = DecodeDirectoryChanges(watch->root,
                                                                      std::span(watch->buffer.data(), count));
                                if (decoded.rescanRequired)
                                    MarkGap(watch->root, "notification-record-invalid");
                                for (const auto &path : decoded.changed) {
                                    if (dirty.size() < 1024)
                                        dirty[path.wstring()] = Now() + 1;
                                    else
                                        MarkGap(watch->root, "debounce-queue-overflow");
                                }
                            }
                            try {
                                watch->Arm();
                            } catch (const std::exception &e) {
                                lastError = e.what();
                                MarkGap(watch->root, "watch-rearm-failed");
                                applied = UINT64_MAX;
                            }
                        }
                    for (auto it = dirty.begin(); it != dirty.end();) {
                        if (it->second > Now()) {
                            ++it;
                            continue;
                        }
                        try {
                            auto path = fs::path(it->first);
                            {
                                UserAccess access(token.value);
                                const auto a = GetFileAttributesW(path.c_str());
                                if (a == INVALID_FILE_ATTRIBUTES) {
                                    it = dirty.erase(it);
                                    continue;
                                }
                            }
                            Enqueue({{"mode", "custom"}, {"paths", Json::array({scan::PathText(path)})}},
                                    "realtime");
                            it = dirty.erase(it);
                        } catch (...) {
                            for (const auto &r : Paths(policy.at("roots")))
                                pendingRescan[r.wstring()] = true;
                            break;
                        }
                    }
                    if (token.value && policy.at("enabled").get<bool>())
                        for (auto it = pendingRescan.begin(); it != pendingRescan.end();) {
                            try {
                                Enqueue({{"mode", "custom"},
                                         {"paths", Json::array({scan::PathText(fs::path(it->first))})}},
                                        "recovery");
                                it = pendingRescan.erase(it);
                            } catch (...) {
                                break;
                            }
                        }
                    bool changed = false;
                    if (token.value && policy.at("enabled").get<bool>())
                        for (auto &s : policy.at("schedules"))
                            if (s.at("nextRun").get<int64_t>() <= Now()) {
                                Transaction tx(db);
                                Enqueue(s, "schedule:" + s.at("id").get<std::string>());
                                s["nextRun"] = Now() + s.at("intervalSeconds").get<int64_t>();
                                SavePolicy();
                                tx.Commit();
                                changed = true;
                            }
                    (void)changed;
                    if (token.value && sessionAvailable) {
                        Statement rows(db, "SELECT value FROM jobs ORDER BY rowid");
                        while (rows.Step() == SQLITE_ROW) {
                            auto j = Json::parse(rows.String(0));
                            if (j.at("state") != "queued")
                                continue;
                            if (j.at("origin") == "manual") {
                                if (!manualBusy && !runManual) {
                                    manualWork = j;
                                    runManual = true;
                                }
                            } else if (policy.at("enabled").get<bool>() && !busy && !run) {
                                work = j;
                                run = true;
                            }
                        }
                    }
                }
                auto launch = [this, t = token.value](Json work, bool manual) {
                    auto &worker = manual ? manualExecutor : executor;
                    auto &occupied = manual ? manualBusy : busy;
                    if (worker.joinable())
                        worker.join();
                    occupied = true;
                    worker = std::thread([this, work, t, manual] {
                        try {
                            RunJob(work, t, manual ? manualCancel.value : cancel.value);
                        } catch (const std::exception &e) {
                            std::lock_guard lock(mutex);
                            lastError = e.what();
                            try {
                                auto j = Job(work.at("id"));
                                j["state"] = "failed";
                                j["lastError"] = e.what();
                                SaveJob(j);
                                Event("scan-failed", {{"job", j.at("id")}, {"error", e.what()}});
                            } catch (...) {
                            }
                            (manual ? manualActive : active).clear();
                        }
                        (manual ? manualBusy : busy) = false;
                    });
                };
                if (run)
                    launch(work, false);
                if (runManual)
                    launch(manualWork, true);

            } catch (const std::exception &e) {
                std::lock_guard lock(mutex);
                lastError = e.what();
                try {
                    Event("protection-error", {{"error", lastError}});
                } catch (...) {
                }
                WaitForSingleObject(stop, 500);
            }
        }
        SetEvent(cancel.value);
        SetEvent(manualCancel.value);
        if (executor.joinable())
            executor.join();
        if (manualExecutor.joinable())
            manualExecutor.join();
    }
};
Protection::Protection(const std::wstring &owner, HANDLE stop) : impl_(std::make_unique<Impl>(owner, stop)) {}
Protection::~Protection() = default;
Json Protection::Dispatch(Operation operation, const fs::path &path, HANDLE token, bool admin) {
    auto &v = *impl_;
    if (operation >= Operation::QuarantineList && operation <= Operation::QuarantineAudit) {
        if ((operation == Operation::QuarantineRestore || operation == Operation::QuarantineErase) && !admin)
            WinError("Administrator authorization required to release/delete quarantined content",
                     ERROR_ACCESS_DENIED);
        Json input;
        if (operation != Operation::QuarantineList &&
            (operation != Operation::QuarantineAudit || path.extension() == L".json")) {
            UserAccess access(token);
            OrdinaryPath(path);
            input = scan::ReadJson(path, 65536);
            Check(input.is_object() && input.value("schema", 0) == 2, "Quarantine request schema rejected");
        }
        std::lock_guard lock(v.vaultMutex);
        quarantine::Store store(v.vault, token);
        Json reply;
        if (operation == Operation::QuarantineList)
            reply = store.List();
        else if (operation == Operation::QuarantineAudit)
            reply = store.Audit(input.is_null() ? 0 : input.value("before", uint64_t(0)));
        else if (operation == Operation::QuarantineIsolate)
            reply = store.Isolate(
                scan::Utf8Path(input.at("source").get<std::string>()), input.value("sha256", std::string()),
                input.value("threat", std::string("Manual")), input.value("reason", std::string()));
        else if (operation == Operation::QuarantineRestore)
            reply = store.Restore(input.at("id").get<std::string>(),
                                  scan::Utf8Path(input.at("directory").get<std::string>()),
                                  scan::Utf8Path(input.at("filename").get<std::string>()).wstring());
        else
            reply = store.Erase(input.at("id").get<std::string>());
        reply["protectedVault"] = true;
        return reply;
    }
    Json input;
    if (operation != Operation::ProtectionStatus)
        input = CallerJson(path, token);
    std::lock_guard lock(v.mutex);
    if (operation == Operation::ProtectionStatus)
        return v.Status(admin);
    if (operation == Operation::ProtectionEvents)
        return v.Events(input.value("after", uint64_t(0)));
    if (operation == Operation::ProtectionConfigure) {
        if (!admin)
            WinError("Administrator authorization required to change protection policy", ERROR_ACCESS_DENIED);
        auto next = Policy(input, token);
        next["owner"] = Text(v.owner);
        Transaction tx(v.db);
        auto previous = v.policy;
        v.policy = next;
        try {
            v.SavePolicy();
            v.Event("policy-updated", {{"enabled", next.at("enabled")}, {"actor", Text(v.owner)}});
            Statement pending(v.db, "SELECT value FROM jobs");
            std::vector<Json> stale;
            while (pending.Step() == SQLITE_ROW) {
                auto j = Json::parse(pending.String(0));
                if (j.at("origin") != "manual" && j.at("state") == "queued") {
                    j["state"] = "cancelled";
                    j["policyChanged"] = true;
                    stale.push_back(j);
                }
            }
            for (const auto &j : stale)
                v.SaveJob(j);
            tx.Commit();
        } catch (...) {
            v.policy = previous;
            throw;
        }
        ++v.generation;
        SetEvent(v.cancel.value);
        SetEvent(v.manualCancel.value);
        return v.Status(admin);
    }
    if (operation == Operation::ProtectionSubmit) {
        Transaction tx(v.db);
        auto j = v.Enqueue(input, "manual");
        tx.Commit();
        return {{"schema", 1}, {"type", "protection-job"}, {"complete", true}, {"job", j}};
    }
    Check(operation == Operation::ProtectionCancel, "Protection operation rejected");
    auto j = v.Job(input.at("id").get<std::string>());
    if (j.at("origin") != "manual" && !admin)
        WinError("Only an administrator can cancel automatic protection", ERROR_ACCESS_DENIED);
    if (j.at("state") == "queued") {
        j["state"] = "cancelled";
        j["finished"] = Now();
    } else if (j.at("state") == "running") {
        j["cancelRequested"] = true;
        SetEvent(j.at("origin") == "manual" ? v.manualCancel.value : v.cancel.value);
    }
    v.SaveJob(j);
    v.Event("scan-cancel-requested", {{"job", j.at("id")}});
    return {{"schema", 1}, {"type", "protection-job"}, {"complete", true}, {"job", j}};
}
} // namespace av::services
