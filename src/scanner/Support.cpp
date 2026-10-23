#include "Scanner.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <random>
#ifdef _WIN32
#include <Windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace av::scan
{
void Require(bool ok, const std::string &why)
{
    if (!ok)
        throw std::runtime_error(why);
}
std::string PathText(const fs::path &p)
{
    const auto u = p.u8string();
    return {u.begin(), u.end()};
}
fs::path Utf8Path(const std::string &s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}
namespace
{
uint64_t Number(const Json &j, const char *key, uint64_t fallback, uint64_t lo, uint64_t hi)
{
    if (!j.contains(key))
        return fallback;
    const auto &n = j.at(key);
    Require(n.is_number_unsigned() || (n.is_number_integer() && n.get<int64_t>() >= 0),
            std::string("Invalid number: ") + key);
    const auto value = n.get<uint64_t>();
    Require(value >= lo && value <= hi, std::string("Limit outside supported bounds: ") + key);
    return value;
}
} // namespace
Json Limits::ToJson() const
{
    return {{"fileBytes", fileBytes},
            {"expandedBytes", expandedBytes},
            {"entries", entries},
            {"depth", depth},
            {"milliseconds", milliseconds}};
}
Limits Limits::Parse(const Json &j)
{
    Require(j.is_object(), "Limits must be an object");
    Limits l;
    l.fileBytes = Number(j, "fileBytes", l.fileBytes, 1, 128ull << 20);
    l.expandedBytes = Number(j, "expandedBytes", l.expandedBytes, 1, 256ull << 20);
    l.entries = uint32_t(Number(j, "entries", l.entries, 1, 2048));
    l.depth = uint32_t(Number(j, "depth", l.depth, 0, 8));
    l.milliseconds = uint32_t(Number(j, "milliseconds", l.milliseconds, 100, 60000));
    return l;
}
Config Config::Parse(const Json &j, const fs::path &base)
{
    Require(j.is_object() && j.value("schema", 0) == 1, "Scanner configuration schema rejected");
    Config c;
    auto path = [&](const char *name) {
        const auto s = j.value(name, std::string());
        if (s.empty())
            return fs::path();
        const auto p = Utf8Path(s);
        return fs::absolute(p.is_absolute() ? p : base / p).lexically_normal();
    };
    c.state = path("state");
    c.signatures = path("signatures");
    c.rulePackage = path("rulePackage");
    c.offline = j.value("offline", true);
    c.queueUnknown = j.value("queueUnknown", false);
    c.cacheSeconds = uint32_t(Number(j, "cacheSeconds", c.cacheSeconds, 0, 86400));
    if (j.contains("limits"))
        c.limits = Limits::Parse(j.at("limits"));
    c.providers = j.value("providers", Json::array());
    Require(c.providers.is_array() && c.providers.size() <= 3, "At most three providers are supported");
    std::vector<std::string> seen;
    for (const auto &p : c.providers)
    {
        Require(p.is_object(), "Provider must be an object");
        const auto id = p.at("id").get<std::string>();
        Require(id == "virustotal" || id == "malwarebazaar" || id == "circl",
                "Unknown intelligence provider");
        Require(std::find(seen.begin(), seen.end(), id) == seen.end(), "Duplicate provider");
        seen.push_back(id);
        (void)Number(p, "perMinute", 4, 1, 60);
        (void)Number(p, "perDay", 500, 1, 100000);
        (void)Number(p, "ttlSeconds", 3600, 1, 86400);
        const auto key = p.value("keyEnv", std::string());
        Require(key.size() <= 64 &&
                    key.find_first_not_of(
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") == key.npos,
                "Invalid key environment name");
        if (p.value("enabled", false))
        {
            Require(p.value("termsAccepted", false), "Provider terms must be acknowledged in configuration");
            if (id == "virustotal")
                Require(
                    p.value("licenseTier", std::string()) == "premium",
                    "This hash-only product connector requires an appropriate VirusTotal Premium license");
            if (id != "circl")
                Require(!key.empty(), "Authenticated provider requires keyEnv");
        }
    }
    return c;
}
fs::path Executable()
{
#ifdef _WIN32
    std::vector<wchar_t> s(32768);
    const auto n = GetModuleFileNameW(nullptr, s.data(), DWORD(s.size()));
    Require(n && n < s.size(), "Cannot locate scanner executable");
    return fs::path(s.data(), s.data() + n);
#else
    std::array<char, 4096> s{};
    const auto n = ::readlink("/proc/self/exe", s.data(), s.size());
    Require(n > 0 && size_t(n) < s.size(), "Cannot locate scanner executable");
    return fs::path(std::string(s.data(), size_t(n)));
#endif
}
fs::path DefaultState()
{
#ifdef _WIN32
    wchar_t s[32768]{};
    const auto n = GetEnvironmentVariableW(L"LOCALAPPDATA", s, 32768);
    Require(n && n < 32768, "LOCALAPPDATA unavailable");
    return fs::path(s) / "Aegis" / "scanner-v1";
#else
    const auto *s = std::getenv("XDG_STATE_HOME");
    if (s && *s)
        return fs::path(s) / "aegis/scanner-v1";
    s = std::getenv("HOME");
    Require(s && *s, "User state directory unavailable");
    return fs::path(s) / ".local/state/aegis/scanner-v1";
#endif
}
Config UserConfig()
{
    const auto p = DefaultState().parent_path() / "scanner.json";
    return fs::exists(p) ? Config::Parse(ReadJson(p), p.parent_path()) : Config{};
}
void OrdinaryDirectory(const fs::path &p)
{
    const auto full = fs::absolute(p).lexically_normal();
    auto at = full.root_path();
    for (const auto &part : full.relative_path())
    {
        at /= part;
        const auto st = fs::symlink_status(at);
        Require(fs::is_directory(st) && !fs::is_symlink(st), "State directory must have ordinary ancestors");
#ifdef _WIN32
        const auto attrs = GetFileAttributesW(at.c_str());
        Require(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
                "State directory reparse point rejected");
#endif
    }
}
void PrivateState(const fs::path &p)
{
    Require(p.is_absolute() && p != p.root_path(), "A dedicated state directory is required");
    fs::create_directories(p);
    OrdinaryDirectory(p);
    const auto marker = p / ".aegis-scanner-state";
    if (!fs::exists(marker))
        Require(fs::is_empty(p), "Refusing a nonempty unrelated state directory");
#ifdef _WIN32
    HANDLE token = nullptr;
    Require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE,
            "Cannot identify state owner");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> info(size);
    const auto ok = GetTokenInformation(token, TokenUser, info.data(), size, &size);
    CloseHandle(token);
    Require(ok != FALSE, "Cannot identify state owner");
    wchar_t *sid = nullptr;
    Require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(info.data())->User.Sid, &sid) != FALSE,
            "Cannot identify state owner");
    const std::wstring acl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR sd = nullptr;
    Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &sd,
                                                                 nullptr) != FALSE,
            "Cannot build private state permissions");
    const auto applied =
        SetFileSecurityW(p.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, sd);
    LocalFree(sd);
    Require(applied != FALSE, "Cannot protect state directory");
#else
    struct stat st{};
    Require(::stat(p.c_str(), &st) == 0 && st.st_uid == ::geteuid(), "State directory owner mismatch");
    Require(::chmod(p.c_str(), 0700) == 0, "Cannot protect state directory");
#endif
    if (!fs::exists(marker))
    {
        const std::string s = "aegis-scanner-state-v1\n";
        Write(marker, {reinterpret_cast<const uint8_t *>(s.data()), s.size()});
    }
    else
        (void)Read(marker, 64);
}
std::string Token()
{
    std::array<uint8_t, 16> bytes{};
#ifdef _WIN32
    Require(BCryptGenRandom(nullptr, bytes.data(), ULONG(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
            "Cannot create scanner job identity");
#else
    std::random_device r;
    for (auto &b : bytes)
        b = uint8_t(r());
#endif
    const char *h = "0123456789abcdef";
    std::string s;
    for (auto b : bytes)
    {
        s += h[b >> 4];
        s += h[b & 15];
    }
    return s;
}
std::string Digest(std::span<const uint8_t> data, const std::string &algorithm)
{
    std::vector<uint8_t> digest;
#ifdef _WIN32
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    const wchar_t *name = algorithm == "sha256" ? BCRYPT_SHA256_ALGORITHM
                          : algorithm == "sha1" ? BCRYPT_SHA1_ALGORITHM
                          : algorithm == "md5"  ? BCRYPT_MD5_ALGORITHM
                                                : nullptr;
    Require(name != nullptr, "Unsupported digest algorithm");
    Require(BCryptOpenAlgorithmProvider(&provider, name, nullptr, 0) >= 0, "Cannot initialize digest");
    try
    {
        DWORD obj = 0, bytes = 0, n = 0;
        Require(BCryptGetProperty(provider, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj), sizeof(obj),
                                  &n, 0) >= 0 &&
                    BCryptGetProperty(provider, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&bytes),
                                      sizeof(bytes), &n, 0) >= 0,
                "Cannot query digest");
        std::vector<uint8_t> state(obj);
        digest.resize(bytes);
        Require(BCryptCreateHash(provider, &hash, state.data(), obj, nullptr, 0, 0) >= 0,
                "Cannot create digest");
        for (size_t i = 0; i < data.size(); i += 65536)
        {
            const auto length = ULONG(std::min<size_t>(65536, data.size() - i));
            Require(BCryptHashData(hash, const_cast<PUCHAR>(data.data() + i), length, 0) >= 0,
                    "Digest update failed");
        }
        Require(BCryptFinishHash(hash, digest.data(), bytes, 0) >= 0, "Digest finalization failed");
        BCryptDestroyHash(hash);
        hash = nullptr;
        BCryptCloseAlgorithmProvider(provider, 0);
    }
    catch (...)
    {
        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(provider, 0);
        throw;
    }
#else
    const auto *md = algorithm == "sha256" ? EVP_sha256()
                     : algorithm == "sha1" ? EVP_sha1()
                     : algorithm == "md5"  ? EVP_md5()
                                           : nullptr;
    Require(md != nullptr, "Unsupported digest algorithm");
    auto *c = EVP_MD_CTX_new();
    Require(c != nullptr, "Cannot create digest");
    digest.resize(EVP_MAX_MD_SIZE);
    unsigned n = 0;
    const auto ok = EVP_DigestInit_ex(c, md, nullptr) == 1 &&
                    EVP_DigestUpdate(c, data.data(), data.size()) == 1 &&
                    EVP_DigestFinal_ex(c, digest.data(), &n) == 1;
    EVP_MD_CTX_free(c);
    Require(ok, "Digest failed");
    digest.resize(n);
#endif
    const char *hex = "0123456789abcdef";
    std::string out;
    for (auto b : digest)
    {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}
std::vector<uint8_t> Read(const fs::path &path, uint64_t limit)
{
    Require(limit <= 256ull << 20, "Read limit rejected");
    std::vector<uint8_t> out;
#ifdef _WIN32
    Require(!path.native().starts_with(L"\\\\"), "Network/device paths are not supported by the scanner");
    const auto h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    Require(h != INVALID_HANDLE_VALUE, "Cannot open ordinary scan input");
    try
    {
        BY_HANDLE_FILE_INFORMATION a{}, b{};
        LARGE_INTEGER size{};
        Require(GetFileInformationByHandle(h, &a) &&
                    !(a.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                    GetFileType(h) == FILE_TYPE_DISK && GetFileSizeEx(h, &size) && size.QuadPart >= 0 &&
                    uint64_t(size.QuadPart) <= limit,
                "Input type or size limit rejected");
        out.resize(size_t(size.QuadPart));
        size_t at = 0;
        while (at < out.size())
        {
            DWORD n = 0;
            Require(
                ReadFile(h, out.data() + at, DWORD(std::min<size_t>(65536, out.size() - at)), &n, nullptr) &&
                    n,
                "Input read failed");
            at += n;
        }
        uint8_t extra = 0;
        DWORD n = 0;
        Require(ReadFile(h, &extra, 1, &n, nullptr) && !n && GetFileInformationByHandle(h, &b) &&
                    a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
                    CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0,
                "Input changed while reading");
        CloseHandle(h);
    }
    catch (...)
    {
        CloseHandle(h);
        throw;
    }
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    Require(fd >= 0, "Cannot open ordinary scan input");
    try
    {
        struct stat a{}, b{};
        Require(::fstat(fd, &a) == 0 && S_ISREG(a.st_mode) && a.st_size >= 0 && uint64_t(a.st_size) <= limit,
                "Input type or size limit rejected");
        out.resize(size_t(a.st_size));
        size_t at = 0;
        while (at < out.size())
        {
            const auto n = ::read(fd, out.data() + at, std::min<size_t>(65536, out.size() - at));
            if (n < 0 && errno == EINTR)
                continue;
            Require(n > 0, "Input read failed");
            at += size_t(n);
        }
        uint8_t extra = 0;
        Require(::read(fd, &extra, 1) == 0 && ::fstat(fd, &b) == 0 && a.st_size == b.st_size &&
                    a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
                    a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec,
                "Input changed while reading");
        ::close(fd);
    }
    catch (...)
    {
        ::close(fd);
        throw;
    }
#endif
    return out;
}
void Write(const fs::path &p, std::span<const uint8_t> data)
{
    OrdinaryDirectory(p.parent_path());
    Require(data.size() <= 128ull << 20, "Output limit rejected");
#ifdef _WIN32
    const auto h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    Require(h != INVALID_HANDLE_VALUE, "Cannot create private scanner output");
    bool ok = true;
    for (size_t at = 0; at < data.size();)
    {
        DWORD n = 0;
        if (!WriteFile(h, data.data() + at, DWORD(std::min<size_t>(65536, data.size() - at)), &n, nullptr) ||
            !n)
        {
            ok = false;
            break;
        }
        at += n;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    Require(ok, "Cannot persist scanner output");
#else
    const auto fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    Require(fd >= 0, "Cannot create private scanner output");
    bool ok = true;
    for (size_t at = 0; at < data.size();)
    {
        const auto n = ::write(fd, data.data() + at, data.size() - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            ok = false;
            break;
        }
        at += size_t(n);
    }
    ok = ok && ::fsync(fd) == 0;
    ::close(fd);
    Require(ok, "Cannot persist scanner output");
#endif
}
void WriteJson(const fs::path &p, const Json &j)
{
    const auto s = j.dump();
    Require(s.size() <= 2ull << 20, "JSON output limit exceeded");
    Write(p, std::span(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
}
Json ReadJson(const fs::path &p, uint64_t limit)
{
    const auto s = Read(p, limit);
    return Json::parse(s.begin(), s.end());
}
} // namespace av::scan
