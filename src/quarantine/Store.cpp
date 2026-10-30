#include "Store.h"
#include <Windows.h>
#include <aclapi.h>
#include <array>
#include <bcrypt.h>
#include <cstring>
#include <cwctype>
#include <exception>
#include <functional>
#include <map>
#include <sddl.h>
#include <set>
#include <shlobj.h>
#include <sqlite3.h>
#include <wincrypt.h>
namespace av::quarantine {
using namespace scan;
namespace {
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE value) : h(value) {}
    ~Handle() { Close(); }
    void Close() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : h(other.h) { other.h = INVALID_HANDLE_VALUE; }
    Handle &operator=(Handle &&other) noexcept {
        if (this != &other) {
            Close();
            h = other.h;
            other.h = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
};
void Win(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(std::string(what) + " (Windows " + std::to_string(GetLastError()) + ")");
}
struct Descriptor {
    PSECURITY_DESCRIPTOR p = nullptr;
    explicit Descriptor(const std::wstring &s) {
        Win(ConvertStringSecurityDescriptorToSecurityDescriptorW(s.c_str(), SDDL_REVISION_1, &p, nullptr) !=
                FALSE,
            "Security descriptor");
    }
    ~Descriptor() { LocalFree(p); }
};
std::wstring Sid() {
    Handle token;
    Win(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h) != FALSE, "User token");
    DWORD size = 0;
    GetTokenInformation(token.h, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> bytes(size);
    Win(GetTokenInformation(token.h, TokenUser, bytes.data(), size, &size) != FALSE, "User SID");
    LPWSTR text = nullptr;
    Win(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid, &text) != FALSE,
        "User SID text");
    std::wstring result = text;
    LocalFree(text);
    return result;
}
std::wstring PrivateAcl(bool temp = false) {
    return L"D:P" + std::wstring(temp ? L"(D;;0x20;;;WD)" : L"") + L"(A;OICI;FA;;;SY)(A;OICI;FA;;;" + Sid() +
           L")";
}
void Dacl(HANDLE file, const std::wstring &text) {
    Descriptor sd(text);
    BOOL present = FALSE, def = FALSE;
    PACL acl = nullptr;
    Win(GetSecurityDescriptorDacl(sd.p, &present, &acl, &def) != FALSE && present && acl, "Read DACL");
    const auto error =
        SetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, acl, nullptr);
    if (error)
        throw std::runtime_error("Set DACL (Windows " + std::to_string(error) + ")");
}
std::wstring SavedDacl(HANDLE file) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    PSID owner = nullptr;
    PACL acl = nullptr;
    auto e = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                             &owner, nullptr, &acl, nullptr, &sd);
    if (e)
        throw std::runtime_error("Read file permissions");
    LPWSTR ownerText = nullptr, aclText = nullptr;
    bool ok = owner && ConvertSidToStringSidW(owner, &ownerText) && acl &&
              ConvertSecurityDescriptorToStringSecurityDescriptorW(
                  sd, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &aclText, nullptr);
    std::wstring result = ok ? aclText : L"", actual = ownerText ? ownerText : L"";
    LocalFree(ownerText);
    LocalFree(aclText);
    LocalFree(sd);
    Require(ok && actual == Sid(), "Only files owned by the current user with a concrete DACL are supported");
    return result;
}
bool Id(const std::string &s) { return s.size() == 32 && s.find_first_not_of("0123456789abcdef") == s.npos; }
fs::path Path(const fs::path &input) {
    const auto s = input.wstring();
    Require(s.size() > 3 && s.size() < 240 && std::iswalpha(s[0]) && s[1] == L':' &&
                (s[2] == L'\\' || s[2] == L'/') && s.find(L'\0') == s.npos,
            "Use a local absolute path below 240 characters");
    for (size_t i = 2; i < s.size(); ++i)
        Require(s[i] >= 32 && s[i] != L':' && s[i] != L'*' && s[i] != L'?' && s[i] != L'"' && s[i] != L'<' &&
                    s[i] != L'>' && s[i] != L'|',
                "Path characters rejected");
    const auto p = fs::path(s).lexically_normal();
    Require(p == fs::path(s), "Path normalization/traversal rejected");
    for (const auto &part : p.relative_path()) {
        auto n = part.wstring();
        Require(!n.empty() && n != L"." && n != L".." && n.back() != L'.' && n.back() != L' ',
                "Path component rejected");
        auto base = n.substr(0, n.find(L'.'));
        for (auto &c : base)
            c = wchar_t(std::towupper(c));
        Require(base != L"CON" && base != L"PRN" && base != L"AUX" && base != L"NUL" && base != L"CONIN$" &&
                    base != L"CONOUT$" &&
                    !(base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
                      base[3] >= L'1' && base[3] <= L'9'),
                "Device names rejected");
    }
    auto drive = p.root_path().wstring();
    auto type = GetDriveTypeW(drive.c_str());
    Require(type == DRIVE_FIXED || type == DRIVE_REMOVABLE, "Network/device paths rejected");
    return p;
}
struct Pins {
    std::vector<Handle> handles;
    explicit Pins(const fs::path &directory) {
        auto p = Path(directory);
        auto at = p.root_path();
        auto open = [&](const fs::path &path) {
            Handle h(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            Win(h.h != INVALID_HANDLE_VALUE, "Pin directory");
            FILE_ATTRIBUTE_TAG_INFO info{};
            Win(GetFileInformationByHandleEx(h.h, FileAttributeTagInfo, &info, sizeof(info)) != FALSE,
                "Directory attributes");
            Require((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        !(info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
                    "Directory link/reparse point rejected");
            handles.push_back(std::move(h));
        };
        open(at);
        for (const auto &part : p.relative_path()) {
            at /= part;
            open(at);
        }
    }
};
Handle Open(const fs::path &p, DWORD access, DWORD sharing = 0, DWORD disposition = OPEN_EXISTING,
            const std::wstring &acl = {}) {
    std::unique_ptr<Descriptor> descriptor;
    if (!acl.empty())
        descriptor = std::make_unique<Descriptor>(acl);
    SECURITY_ATTRIBUTES sa{sizeof(sa), descriptor ? descriptor->p : nullptr, FALSE};
    Handle h(CreateFileW(p.c_str(), access, sharing, descriptor ? &sa : nullptr, disposition,
                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Win(h.h != INVALID_HANDLE_VALUE, "Open file");
    BY_HANDLE_FILE_INFORMATION info{};
    Win(GetFileInformationByHandle(h.h, &info) != FALSE, "File identity");
    Require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                info.nNumberOfLinks == 1 && GetFileType(h.h) == FILE_TYPE_DISK,
            "Regular single-link file required");
    return h;
}
std::vector<uint8_t> ReadHandle(HANDLE h, uint64_t limit) {
    LARGE_INTEGER size{}, zero{};
    Win(GetFileSizeEx(h, &size) != FALSE && SetFilePointerEx(h, zero, nullptr, FILE_BEGIN) != FALSE,
        "File size/position");
    Require(size.QuadPart >= 0 && uint64_t(size.QuadPart) <= limit, "Quarantine file size limit");
    std::vector<uint8_t> data(size_t(size.QuadPart));
    size_t at = 0;
    while (at < data.size()) {
        DWORD got = 0;
        Win(ReadFile(h, data.data() + at, DWORD((std::min)(data.size() - at, size_t(1 << 20))), &got,
                     nullptr) != FALSE &&
                got,
            "Read file");
        at += got;
    }
    return data;
}
void WriteHandle(HANDLE h, std::span<const uint8_t> data) {
    size_t at = 0;
    while (at < data.size()) {
        DWORD done = 0;
        Win(WriteFile(h, data.data() + at, DWORD((std::min)(data.size() - at, size_t(1 << 20))), &done,
                      nullptr) != FALSE &&
                done,
            "Write file");
        at += done;
    }
    Win(FlushFileBuffers(h) != FALSE, "Durable file flush");
}
std::string FileId(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION i{};
    Win(GetFileInformationByHandle(h, &i) != FALSE, "File identity");
    return std::to_string(i.dwVolumeSerialNumber) + ":" + std::to_string(i.nFileIndexHigh) + ":" +
           std::to_string(i.nFileIndexLow);
}
void Remove(HANDLE h) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    Win(SetFileInformationByHandle(h, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE,
        "Remove original by identity");
}
uint64_t Clock() {
    FILETIME t{};
    GetSystemTimeAsFileTime(&t);
    return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
#ifdef AEGIS_QUARANTINE_TESTING
thread_local std::string faultPoint;
thread_local bool faultTerminates = false;
void Point(const char *point) {
    if (faultPoint == point) {
        if (faultTerminates)
            TerminateProcess(GetCurrentProcess(), 77);
        throw std::runtime_error(std::string("Injected failure: ") + point);
    }
}
#else
void Point(const char *) {}
#endif
struct FailureGuard {
    std::function<void()> action;
    int count = std::uncaught_exceptions();
    ~FailureGuard() {
        if (std::uncaught_exceptions() > count)
            try {
                action();
            } catch (...) {
            }
    }
};
struct Aes {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<uint8_t> object;
    explicit Aes(std::span<const uint8_t> secret) {
        try {
            Require(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0, "AES provider");
            Require(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                                      reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                                      sizeof(BCRYPT_CHAIN_MODE_GCM), 0) >= 0,
                    "AES-GCM mode");
            DWORD size = 0, got = 0;
            Require(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                                      sizeof(size), &got, 0) >= 0,
                    "AES key size");
            object.resize(size);
            Require(BCryptGenerateSymmetricKey(alg, &key, object.data(), size,
                                               const_cast<PUCHAR>(secret.data()), ULONG(secret.size()),
                                               0) >= 0,
                    "AES key");
        } catch (...) {
            if (key)
                BCryptDestroyKey(key);
            if (alg)
                BCryptCloseAlgorithmProvider(alg, 0);
            throw;
        }
    }
    ~Aes() {
        if (key)
            BCryptDestroyKey(key);
        if (alg)
            BCryptCloseAlgorithmProvider(alg, 0);
        if (!object.empty())
            SecureZeroMemory(object.data(), object.size());
    }
    std::vector<uint8_t> Crypt(bool encrypt, std::span<const uint8_t> input, const std::string &domain) {
        Require(input.size() <= MaxFileBytes + 65536, "Ciphertext size quota");
        std::vector<uint8_t> out(encrypt ? input.size() + 32 : input.size() >= 32 ? input.size() - 32 : 0);
        Require(encrypt || input.size() >= 32, "Truncated authenticated record");
        std::array<uint8_t, 32> prefix{};
        if (encrypt) {
            prefix[0] = 1;
            Require(BCryptGenRandom(nullptr, prefix.data() + 4, 12, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
                    "Nonce generation");
        } else {
            std::memcpy(prefix.data(), input.data(), 32);
            Require(prefix[0] == 1 && !prefix[1] && !prefix[2] && !prefix[3], "Cipher format rejected");
        }
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = prefix.data() + 4;
        info.cbNonce = 12;
        info.pbTag = prefix.data() + 16;
        info.cbTag = 16;
        info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(domain.data()));
        info.cbAuthData = ULONG(domain.size());
        ULONG bytes = 0;
        uint8_t dummy = 0;
        auto body = encrypt ? input : input.subspan(32);
        auto status =
            encrypt ? BCryptEncrypt(key, const_cast<PUCHAR>(body.data()), ULONG(body.size()), &info, nullptr,
                                    0, out.data() + 32, ULONG(input.size()), &bytes, 0)
                    : BCryptDecrypt(key, const_cast<PUCHAR>(body.data()), ULONG(body.size()), &info, nullptr,
                                    0, out.empty() ? &dummy : out.data(), ULONG(out.size()), &bytes, 0);
        if (status < 0 && !out.empty())
            SecureZeroMemory(out.data(), out.size());
        Require(status >= 0, "Authenticated quarantine record rejected");
        if (encrypt) {
            Require(bytes == input.size(), "Encrypted length mismatch");
            std::memcpy(out.data(), prefix.data(), 32);
        } else
            Require(bytes == out.size(), "Decrypted length mismatch");
        return out;
    }
};
struct Statement {
    sqlite3_stmt *p = nullptr;
    Statement(sqlite3 *db, const char *sql) {
        Require(sqlite3_prepare_v2(db, sql, -1, &p, nullptr) == SQLITE_OK, "Prepare quarantine transaction");
    }
    ~Statement() { sqlite3_finalize(p); }
    void Text(int i, const std::string &s) {
        Require(sqlite3_bind_text(p, i, s.data(), int(s.size()), SQLITE_TRANSIENT) == SQLITE_OK,
                "Bind quarantine text");
    }
    void Blob(int i, std::span<const uint8_t> b) {
        Require(b.size() <= INT_MAX &&
                    sqlite3_bind_blob(p, i, b.data(), int(b.size()), SQLITE_TRANSIENT) == SQLITE_OK,
                "Bind quarantine bytes");
    }
    int Step() { return sqlite3_step(p); }
    std::vector<uint8_t> Bytes(int i) {
        auto n = sqlite3_column_bytes(p, i);
        Require(n >= 0 && uint64_t(n) <= MaxFileBytes + 65536, "Stored record quota");
        auto *b = static_cast<const uint8_t *>(sqlite3_column_blob(p, i));
        return n ? std::vector<uint8_t>(b, b + n) : std::vector<uint8_t>();
    }
    std::string String(int i) {
        const auto *ptext = sqlite3_column_text(p, i);
        Require(ptext != nullptr, "Stored text missing");
        return {reinterpret_cast<const char *>(ptext), size_t(sqlite3_column_bytes(p, i))};
    }
};
void Sql(sqlite3 *db, const char *text) {
    auto status = sqlite3_exec(db, text, nullptr, nullptr, nullptr);
    Require(status == SQLITE_OK, "Quarantine transaction failed (SQLite " + std::to_string(status) + ")");
}
struct Transaction {
    sqlite3 *db;
    bool committed = false;
    explicit Transaction(sqlite3 *d) : db(d) { Sql(db, "BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (!committed)
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void Commit() {
        Sql(db, "COMMIT");
        committed = true;
    }
};
std::vector<uint8_t> Bytes(const Json &j) {
    auto text = j.dump();
    return {text.begin(), text.end()};
}
Json Reply(const std::string &action) {
    return {{"schema", 2}, {"type", "quarantine"}, {"action", action}, {"complete", true}};
}
} // namespace
struct Store::Impl {
    fs::path root;
    std::unique_ptr<Pins> pins;
    Handle lock, keyFile, databaseFile;
    std::array<uint8_t, 32> secret{};
    std::unique_ptr<Aes> aes;
    sqlite3 *db = nullptr;
    explicit Impl(const fs::path &path) : root(Path(path)) {
        try {
            fs::create_directories(root);
            pins = std::make_unique<Pins>(root);
            Handle directory(CreateFileW(root.c_str(), READ_CONTROL | WRITE_DAC,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            Win(directory.h != INVALID_HANDLE_VALUE, "Open private vault directory");
            SavedDacl(directory.h);
            Dacl(directory.h, PrivateAcl());
            lock = Open(root / L"vault.lock", GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS, PrivateAcl());
            const auto keyPath = root / L"vault.key";
            const bool existing = fs::exists(keyPath);
            const bool databaseExisted = fs::exists(root / L"vault.sqlite");
            Require(existing == databaseExisted,
                    "Vault key/database pair missing; existing files left untouched");
            Require(!databaseExisted || fs::file_size(root / L"vault.sqlite") > 0,
                    "Existing vault database is empty; refusing reinitialization");
            keyFile = Open(keyPath, existing ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                           existing ? OPEN_EXISTING : CREATE_NEW, PrivateAcl());
            if (!existing) {
                Require(BCryptGenRandom(nullptr, secret.data(), DWORD(secret.size()),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
                        "Vault key generation");
                DATA_BLOB plain{DWORD(secret.size()), secret.data()}, sealed{};
                Win(CryptProtectData(&plain, L"Aegis quarantine v2", nullptr, nullptr, nullptr,
                                     CRYPTPROTECT_UI_FORBIDDEN, &sealed) != FALSE,
                    "Protect vault key");
                try {
                    WriteHandle(keyFile.h, {sealed.pbData, sealed.cbData});
                } catch (...) {
                    LocalFree(sealed.pbData);
                    throw;
                }
                LocalFree(sealed.pbData);
            } else {
                auto raw = ReadHandle(keyFile.h, 16384);
                DATA_BLOB sealed{DWORD(raw.size()), raw.data()}, plain{};
                Win(CryptUnprotectData(&sealed, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                                       &plain) != FALSE,
                    "Unprotect vault key");
                bool valid = plain.cbData == secret.size();
                if (valid)
                    std::memcpy(secret.data(), plain.pbData, secret.size());
                SecureZeroMemory(plain.pbData, plain.cbData);
                LocalFree(plain.pbData);
                Require(valid, "Vault key length rejected");
            }
            aes = std::make_unique<Aes>(secret);
            databaseFile = Open(root / L"vault.sqlite", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS, PrivateAcl());
            auto name = PathText(root / L"vault.sqlite");
            Require(sqlite3_open_v2(name.c_str(), &db,
                                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                    nullptr) == SQLITE_OK,
                    "Open quarantine database");
            sqlite3_busy_timeout(db, 2000);
            Require(sqlite3_compileoption_used("OMIT_LOAD_EXTENSION") != 0,
                    "SQLite extension loading must be compiled out");
            sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
            Sql(db, "PRAGMA trusted_schema=OFF; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA "
                    "secure_delete=ON; PRAGMA max_page_count=262144; CREATE TABLE IF NOT EXISTS entries(id "
                    "TEXT PRIMARY KEY, meta BLOB NOT NULL, data BLOB NOT NULL); CREATE TABLE IF NOT EXISTS "
                    "audit(seq INTEGER PRIMARY KEY, value BLOB NOT NULL); CREATE TABLE IF NOT EXISTS info(id "
                    "INTEGER PRIMARY KEY CHECK(id=1), value BLOB NOT NULL);");
            {
                Statement page(db, "PRAGMA page_size");
                Require(page.Step() == SQLITE_ROW && sqlite3_column_int(page.p, 0) == 4096,
                        "Vault page size rejected");
                Statement check(db, "PRAGMA quick_check");
                Require(check.Step() == SQLITE_ROW && check.String(0) == "ok",
                        "Quarantine database integrity failure");
            }
            Statement head(db, "SELECT value FROM info WHERE id=1");
            if (head.Step() == SQLITE_DONE) {
                Require(!databaseExisted, "Existing vault audit head missing; refusing reinitialization");
                Statement count(db, "SELECT (SELECT count(*) FROM audit)+(SELECT count(*) FROM entries)");
                Require(count.Step() == SQLITE_ROW && sqlite3_column_int(count.p, 0) == 0,
                        "Audit head missing");
                Transaction tx(db);
                auto blob = Seal("vault/head", {{"seq", 0}, {"hash", std::string(64, '0')}});
                Statement insert(db, "INSERT INTO info VALUES(1,?)");
                insert.Blob(1, blob);
                Require(insert.Step() == SQLITE_DONE, "Initialize audit head");
                tx.Commit();
            }
            CheckAudit();
        } catch (...) {
            if (db) {
                sqlite3_close_v2(db);
                db = nullptr;
            }
            aes.reset();
            SecureZeroMemory(secret.data(), secret.size());
            throw;
        }
    }
    ~Impl() {
        if (db)
            sqlite3_close_v2(db);
        aes.reset();
        SecureZeroMemory(secret.data(), secret.size());
    }
    std::vector<uint8_t> Seal(const std::string &domain, const Json &value) {
        auto plain = Bytes(value);
        Require(plain.size() <= 65536, "Metadata quota");
        return aes->Crypt(true, plain, "Aegis15/" + domain);
    }
    Json Unseal(const std::string &domain, std::span<const uint8_t> bytes) {
        Require(bytes.size() <= 65568, "Metadata quota");
        return Json::parse(aes->Crypt(false, bytes, "Aegis15/" + domain));
    }
    Json Head() {
        Statement s(db, "SELECT value FROM info WHERE id=1");
        Require(s.Step() == SQLITE_ROW, "Audit head missing");
        return Unseal("vault/head", s.Bytes(0));
    }
    void CheckAudit() {
        auto head = Head();
        Statement rows(db, "SELECT seq,value FROM audit ORDER BY seq");
        uint64_t sequence = 0;
        std::string previous(64, '0');
        std::map<std::string, std::string> expected;
        int status = SQLITE_OK;
        while ((status = rows.Step()) == SQLITE_ROW) {
            Require(++sequence <= 8192 && sqlite3_column_int64(rows.p, 0) == int64_t(sequence),
                    "Audit sequence rejected");
            auto raw = rows.Bytes(1);
            auto event = Unseal("audit/" + std::to_string(sequence), raw);
            Require(event.at("seq") == sequence && event.at("previous") == previous, "Audit chain rejected");
            const auto id = event.at("entry").get<std::string>();
            if (!id.empty()) {
                Require(Id(id), "Audit entry ID rejected");
                expected[id] = event.at("action").get<std::string>();
            }
            previous = Digest(raw);
        }
        Require(status == SQLITE_DONE, "Audit read failed");
        Require(head.at("seq") == sequence && head.at("hash") == previous, "Audit truncation/head mismatch");
        Statement entries(db, "SELECT id FROM entries");
        std::set<std::string> found;
        while ((status = entries.Step()) == SQLITE_ROW) {
            auto id = entries.String(0);
            Require(Id(id) && found.size() < 256 && expected.contains(id) && expected.at(id) != "erased",
                    "Entry missing from audit");
            found.insert(id);
        }
        Require(status == SQLITE_DONE, "Entry inventory failed");
        for (const auto &[id, action] : expected)
            Require((action == "erased") == !found.contains(id), "Vault entry missing or resurrected");
    }
    void Append(Json event) {
        auto head = Head();
        auto n = head.at("seq").get<uint64_t>() + 1;
        Require(n <= 8192, "Audit capacity reached");
        event["seq"] = n;
        event["previous"] = head["hash"];
        event["timeFiletime"] = Clock();
        event["actor"] = PathText(Sid());
        auto raw = Seal("audit/" + std::to_string(n), event);
        Statement s(db, "INSERT INTO audit VALUES(?,?)");
        sqlite3_bind_int64(s.p, 1, sqlite3_int64(n));
        s.Blob(2, raw);
        Require(s.Step() == SQLITE_DONE, "Append audit event");
        auto next = Seal("vault/head", {{"seq", n}, {"hash", Digest(raw)}});
        Statement update(db, "UPDATE info SET value=? WHERE id=1");
        update.Blob(1, next);
        Require(update.Step() == SQLITE_DONE, "Update audit head");
    }
    void Event(const std::string &id, const std::string &action) {
        auto details = Meta(id);
        Append({{"entry", id},
                {"action", action},
                {"source", details["source"]},
                {"sha256", details["sha256"]},
                {"threat", details["threat"]},
                {"destination", details.value("restoreDestination", std::string())}});
    }
    void Failure(const char *action, const std::string &id = {}) {
        Transaction tx(db);
        if (Id(id)) {
            try {
                Event(id, std::string(action) + "-failed");
                tx.Commit();
                return;
            } catch (...) {
            }
        }
        Append({{"entry", ""}, {"action", std::string(action) + "-failed"}});
        tx.Commit();
    }

    Json Meta(const std::string &id) {
        Require(Id(id), "Entry ID rejected");
        Statement s(db, "SELECT meta FROM entries WHERE id=?");
        s.Text(1, id);
        Require(s.Step() == SQLITE_ROW, "Quarantine entry not found");
        auto m = Unseal(id + "/meta", s.Bytes(0));
        Require(m.at("id") == id && m.at("schema") == 2, "Entry metadata mismatch");
        return m;
    }
    std::vector<uint8_t> Data(const std::string &id, const Json &meta) {
        Statement s(db, "SELECT data FROM entries WHERE id=?");
        s.Text(1, id);
        Require(s.Step() == SQLITE_ROW, "Quarantine payload missing");
        auto plain = aes->Crypt(false, s.Bytes(0), "Aegis15/" + id + "/data");
        Require(plain.size() == meta.at("bytes").get<uint64_t>() &&
                    Digest(plain) == meta.at("sha256").get<std::string>(),
                "Payload integrity mismatch");
        return plain;
    }
    void State(const std::string &id, Json &m, const std::string &state) {
        m["state"] = state;
        auto raw = Seal(id + "/meta", m);
        Transaction tx(db);
        Statement s(db, "UPDATE entries SET meta=? WHERE id=?");
        s.Blob(1, raw);
        s.Text(2, id);
        Require(s.Step() == SQLITE_DONE && sqlite3_changes(db) == 1, "Update quarantine state");
        Event(id, state);
        tx.Commit();
    }
};
fs::path DefaultRoot() {
    PWSTR text = nullptr;
    Win(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &text) == S_OK,
        "Local user data folder");
    fs::path path = text;
    CoTaskMemFree(text);
    return path / L"Aegis" / L"quarantine-v2";
}
Store::Store(const fs::path &root) : impl_(std::make_unique<Impl>(root)) {}
Store::~Store() = default;
Json Store::Isolate(const fs::path &input, const std::string &expected, const std::string &threat,
                    const std::string &reason) {
    auto &v = *impl_;
    FailureGuard guard{[&] { v.Failure("isolate"); }};
    auto source = Path(input);
    auto sourceText = source.wstring(), vaultText = v.root.wstring();
    Require(_wcsnicmp(sourceText.c_str(), vaultText.c_str(), vaultText.size()) != 0,
            "Cannot quarantine vault files");
    Require(threat.size() <= 2048 && reason.size() <= 4096, "Detection metadata quota");
    Pins parents(source.parent_path());
    auto file = Open(source, GENERIC_READ | READ_CONTROL | DELETE, FILE_SHARE_READ);
    BY_HANDLE_FILE_INFORMATION info{};
    Win(GetFileInformationByHandle(file.h, &info) != FALSE, "Source identity");
    Require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_COMPRESSED |
                                       FILE_ATTRIBUTE_SPARSE_FILE | FILE_ATTRIBUTE_OFFLINE)),
            "EFS/compressed/sparse/offline files are unsupported");
    std::vector<uint8_t> streams(65536);
    Win(GetFileInformationByHandleEx(file.h, FileStreamInfo, streams.data(), DWORD(streams.size())) != FALSE,
        "Inspect alternate streams");
    auto *stream = reinterpret_cast<FILE_STREAM_INFO *>(streams.data());
    Require(!stream->NextEntryOffset &&
                std::wstring(stream->StreamName, stream->StreamNameLength / 2) == L"::$DATA",
            "Alternate data streams are unsupported");
    auto permissions = SavedDacl(file.h);
    auto data = ReadHandle(file.h, MaxFileBytes);
    auto sha = Digest(data);
    Require(expected.empty() || expected == sha, "Source changed since detection; original left untouched");
    Point("isolate-read");
    auto id = Token();
    Require(Id(id), "Entry random ID");
    Json m = {
        {"schema", 2},
        {"id", id},
        {"source", PathText(source)},
        {"sourceIdentity", FileId(file.h)},
        {"owner", PathText(Sid())},
        {"dacl", PathText(permissions)},
        {"attributes", info.dwFileAttributes},
        {"created", (uint64_t(info.ftCreationTime.dwHighDateTime) << 32) | info.ftCreationTime.dwLowDateTime},
        {"accessed",
         (uint64_t(info.ftLastAccessTime.dwHighDateTime) << 32) | info.ftLastAccessTime.dwLowDateTime},
        {"modified",
         (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime},
        {"sha256", sha},
        {"bytes", data.size()},
        {"threat", threat},
        {"reason", reason},
        {"state", "prepared"},
        {"storedFiletime", Clock()}};
    auto cipher = v.aes->Crypt(true, data, "Aegis15/" + id + "/data");
    auto meta = v.Seal(id + "/meta", m);
    {
        Transaction tx(v.db);
        Statement count(v.db, "SELECT count(*) FROM entries");
        Require(count.Step() == SQLITE_ROW && sqlite3_column_int(count.p, 0) < 256,
                "Vault entry capacity reached");
        Statement s(v.db, "INSERT INTO entries VALUES(?,?,?)");
        s.Text(1, id);
        s.Blob(2, meta);
        s.Blob(3, cipher);
        Require(s.Step() == SQLITE_DONE, "Persist quarantine copy");
        v.Event(id, "prepared");
        tx.Commit();
    }
    Point("isolate-prepared");
    {
        auto verified = v.Data(id, v.Meta(id));
        Require(verified == data, "Persisted copy verification failed");
        SecureZeroMemory(verified.data(), verified.size());
    }
    Point("isolate-verified");
    auto result = Reply("isolate");
    result["id"] = id;
    result["sha256"] = sha;
    bool removed = false;
    try {
        Remove(file.h);
        file.Close();
        removed = true;
        Point("isolate-removed");
        v.State(id, m, "isolated");
        Point("isolate-final");
        result["state"] = "isolated";
        result["originalRemoved"] = true;
    } catch (const std::exception &e) {
        result["complete"] = false;
        result["state"] = "recovery-required";
        result["error"] = e.what();
        result["recoverableCopy"] = true;
        result["originalRemoved"] = removed;
        try {
            m["lastError"] = e.what();
            v.State(id, m, removed ? "recovery-required" : "source-retained");
        } catch (...) {
        }
    }
    SecureZeroMemory(data.data(), data.size());
    return result;
}
Json Store::List() {
    auto &v = *impl_;
    auto result = Reply("list");
    result["entries"] = Json::array();
    Statement s(v.db, "SELECT id FROM entries ORDER BY id");
    int status = SQLITE_OK;
    while ((status = s.Step()) == SQLITE_ROW) {
        Require(result["entries"].size() < 256, "Entry count quota");
        auto id = s.String(0);
        try {
            auto m = v.Meta(id);
            m.erase("dacl");
            m.erase("owner");
            for (const auto *field : {"reason", "threat", "lastError"})
                if (m.contains(field)) {
                    auto text = m.at(field).get<std::string>();
                    if (text.size() > 256) {
                        size_t end = 256;
                        while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
                            --end;
                        m[field] = text.substr(0, end) + "…";
                        m["previewTruncated"] = true;
                    }
                }
            m["metadataVerified"] = true;
            m["payloadVerified"] = false;
            m["recoveryRequired"] = m.at("state") == "prepared" || m.at("state") == "restore-pending";
            result["entries"].push_back(m);
        } catch (const std::exception &e) {
            result["entries"].push_back(
                {{"id", id}, {"state", "corrupt"}, {"error", e.what()}, {"metadataVerified", false}});
            result["complete"] = false;
        }
    }
    Require(status == SQLITE_DONE, "Entry listing read failed");
    return result;
}
Json Store::Restore(const std::string &id, const fs::path &authorizedDirectory,
                    const std::wstring &filename) {
    auto &v = *impl_;
    FailureGuard guard{[&] { v.Failure("restore", id); }};
    auto parent = Path(authorizedDirectory);
    Require(fs::path(filename).filename() == fs::path(filename) && !filename.empty(),
            "Restore filename must be a leaf");
    auto destination = Path(parent / filename);
    auto text = destination.wstring(), root = v.root.wstring();
    Require(_wcsnicmp(text.c_str(), root.c_str(), root.size()) != 0, "Cannot restore into vault");
    Pins pins(parent);
    Require(GetFileAttributesW(destination.c_str()) == INVALID_FILE_ATTRIBUTES &&
                GetLastError() == ERROR_FILE_NOT_FOUND,
            "Restore destination exists or is unavailable; no overwrite");
    auto m = v.Meta(id);
    auto plain = v.Data(id, m);
    m["restoreDestination"] = PathText(destination);
    bool journaled = true;
    std::string historyError;
    try {
        Point("restore-journal");
        v.State(id, m, "restore-pending");
    } catch (const std::exception &e) {
        journaled = false;
        historyError = e.what();
    }
    Point("restore-intent");
    const auto temp = parent / (L".aegis-" + Utf8Path(Token()).wstring() + L".tmp");
    auto file = Open(temp, GENERIC_READ | GENERIC_WRITE | WRITE_DAC | FILE_WRITE_ATTRIBUTES | DELETE, 0,
                     CREATE_NEW, PrivateAcl(true));
    bool published = false;
    try {
        WriteHandle(file.h, plain);
        auto check = ReadHandle(file.h, MaxFileBytes);
        Require(check == plain, "Restored bytes differ");
        SecureZeroMemory(check.data(), check.size());
        auto ft = [](uint64_t n) { return FILETIME{DWORD(n), DWORD(n >> 32)}; };
        auto created = ft(m.at("created").get<uint64_t>()), accessed = ft(m.at("accessed").get<uint64_t>()),
             modified = ft(m.at("modified").get<uint64_t>());
        Win(SetFileTime(file.h, &created, &accessed, &modified) != FALSE, "Restore timestamps");
        Point("restore-flushed");
        FILE_BASIC_INFO basic{};
        basic.FileAttributes = m.at("attributes").get<DWORD>() &
                               (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
                                FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        if (!basic.FileAttributes)
            basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        const auto name = destination.wstring();
        std::vector<uint8_t> rename(offsetof(FILE_RENAME_INFO, FileName) +
                                    (name.size() + 1) * sizeof(wchar_t));
        auto *info = reinterpret_cast<FILE_RENAME_INFO *>(rename.data());
        info->ReplaceIfExists = FALSE;
        info->RootDirectory = nullptr;
        info->FileNameLength = DWORD(name.size() * sizeof(wchar_t));
        std::memcpy(info->FileName, name.c_str(), (name.size() + 1) * sizeof(wchar_t));
        Win(SetFileInformationByHandle(file.h, FileRenameInfo, info, DWORD(rename.size())) != FALSE,
            "Publish restore without overwrite");
        published = true;
        Dacl(file.h, Utf8Path(m.at("dacl").get<std::string>()).wstring());
        Win(SetFileInformationByHandle(file.h, FileBasicInfo, &basic, sizeof(basic)) != FALSE,
            "Restore attributes");
        Win(FlushFileBuffers(file.h) != FALSE, "Flush restored destination");
        Point("restore-published");
        if (journaled) {
            try {
                v.State(id, m, "restored-copy");
            } catch (const std::exception &e) {
                journaled = false;
                historyError = e.what();
            }
        }
        Point("restore-final");
    } catch (...) {
        if (!published) {
            try {
                Remove(file.h);
            } catch (...) {
            }
        }
        SecureZeroMemory(plain.data(), plain.size());
        throw;
    }
    SecureZeroMemory(plain.data(), plain.size());
    auto result = Reply("restore");
    result["id"] = id;
    result["destination"] = PathText(destination);
    result["sha256"] = m["sha256"];
    result["vaultCopyRetained"] = true;
    result["copyRestored"] = true;
    result["historyUpdated"] = journaled;
    if (!journaled) {
        result["complete"] = false;
        result["error"] =
            "Cópia restaurada e verificada; histórico não atualizado. Cópia do cofre mantida. " +
            historyError;
    }
    return result;
}
Json Store::Erase(const std::string &id) {
    auto &v = *impl_;
    FailureGuard guard{[&] { v.Failure("erase", id); }};
    v.Meta(id);
    Transaction tx(v.db);
    v.Event(id, "erased");
    Statement s(v.db, "DELETE FROM entries WHERE id=?");
    s.Text(1, id);
    Require(s.Step() == SQLITE_DONE && sqlite3_changes(v.db) == 1, "Erase quarantine entry");
    Point("erase-uncommitted");
    tx.Commit();
    Point("erase-final");
    auto result = Reply("erase");
    result["id"] = id;
    result["physicalSecureEraseGuaranteed"] = false;
    return result;
}
Json Store::Audit(uint64_t beforeSequence) {
    auto &v = *impl_;
    auto result = Reply("audit");
    result["events"] = Json::array();
    auto total = v.Head().at("seq").get<uint64_t>();
    Require(beforeSequence <= 8193, "Audit cursor rejected");
    Statement rows(v.db, "SELECT seq,value FROM audit WHERE seq < ? ORDER BY seq DESC LIMIT 128");
    sqlite3_bind_int64(rows.p, 1, sqlite3_int64(beforeSequence ? beforeSequence : total + 1));
    int status = SQLITE_OK;
    uint64_t first = 0;
    while ((status = rows.Step()) == SQLITE_ROW) {
        auto n = sqlite3_column_int64(rows.p, 0);
        first = uint64_t(n);
        result["events"].push_back(v.Unseal("audit/" + std::to_string(n), rows.Bytes(1)));
    }
    Require(status == SQLITE_DONE, "Audit page read failed");
    std::reverse(result["events"].begin(), result["events"].end());
    result["totalEvents"] = total;
    result["nextBefore"] = first > 1 ? first : 0;
    return result;
}
#ifdef AEGIS_QUARANTINE_TESTING
void TestFault(const std::string &point, bool terminate) {
    faultPoint = point;
    faultTerminates = terminate;
}
void Store::TestAuditEvents(unsigned count) {
    Require(count <= 8192, "Test audit limit");
    Transaction tx(impl_->db);
    for (unsigned i = 0; i < count; ++i)
        impl_->Append({{"entry", ""}, {"action", "test-event"}});
    tx.Commit();
}
void Store::TestMaxPages(unsigned pages) {
    Sql(impl_->db, ("PRAGMA max_page_count=" + std::to_string(pages)).c_str());
}
#endif
} // namespace av::quarantine
