#include "Store.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#elif defined(GUARDIAN_SCANNER_CURL)
#include <curl/curl.h>
#endif
namespace av::scan
{
namespace
{
std::string Lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}
void Hash(const std::string &h)
{
    Require(h.size() == 64 && h.find_first_not_of("0123456789abcdef") == h.npos, "SHA256 lookup key invalid");
}
} // namespace
HttpRequest ProviderRequest(const std::string &id, const std::string &hash, const std::string &key)
{
    Hash(hash);
    Require(key.size() <= 512 && key.find_first_of("\r\n") == key.npos, "Invalid API credential");
    HttpRequest r;
    if (id == "virustotal")
    {
        Require(!key.empty(), "Missing provider credential");
        r.url = "https://www.virustotal.com/api/v3/files/" + hash;
        r.headerName = "x-apikey";
        r.headerValue = key;
    }
    else if (id == "malwarebazaar")
    {
        Require(!key.empty(), "Missing provider credential");
        r.url = "https://mb-api.abuse.ch/api/v1/";
        r.method = "POST";
        r.headerName = "Auth-Key";
        r.headerValue = key;
        r.body = "query=get_info&hash=" + hash;
    }
    else if (id == "circl")
        r.url = "https://hashlookup.circl.lu/lookup/sha256/" + hash;
    else
        throw std::runtime_error("Unknown provider");
    return r;
}
Json ProviderResponse(const std::string &id, const std::string &hash, const HttpResponse &response)
{
    Hash(hash);
    Require(response.body.size() <= 1ull << 20, "Provider response limit exceeded");
    Json out = {{"provider", id}, {"status", "unknown"}, {"classification", "unknown"}, {"sha256", hash}};
    if (response.status == 404 && id != "malwarebazaar")
        return out;
    Require(response.status == 200, "Provider HTTP failure");
    const auto j = Json::parse(response.body);
    Require(j.is_object(), "Provider object required");
    if (id == "virustotal")
    {
        const auto &d = j.at("data");
        Require(d.at("type") == "file" && Lower(d.at("id").get<std::string>()) == hash,
                "Provider hash/object mismatch");
        const auto &s = d.at("attributes").at("last_analysis_stats");
        Require(s.is_object() && !s.empty(), "Provider statistics absent");
        uint64_t bad = 0, total = 0;
        for (auto it = s.begin(); it != s.end(); ++it)
        {
            Require(it->is_number_integer() && it->get<int64_t>() >= 0 && it->get<int64_t>() <= 10000,
                    "Provider statistics invalid");
            const auto n = it->get<uint64_t>();
            total += n;
            if (it.key() == "malicious" || it.key() == "suspicious")
                bad += n;
        }
        out["status"] = "found";
        out["reportedDetections"] = bad;
        out["reportedEngines"] = total;
        if (bad)
            out["classification"] = "suspicious";
    }
    else if (id == "malwarebazaar")
    {
        const auto state = j.at("query_status").get<std::string>();
        if (state == "hash_not_found")
            return out;
        Require(state == "ok", "Provider query rejected");
        const auto &data = j.at("data");
        Require(data.is_array() && !data.empty() && data.size() <= 32, "Provider records invalid");
        bool match = false;
        for (const auto &record : data)
            if (Lower(record.at("sha256_hash").get<std::string>()) == hash)
                match = true;
        Require(match, "Provider hash mismatch");
        out["status"] = "found";
        out["classification"] = "suspicious";
        out["reason"] = "hash reported in MalwareBazaar; corroborate locally";
    }
    else if (id == "circl")
    {
        Require(Lower(j.at("SHA-256").get<std::string>()) == hash, "Provider hash mismatch");
        out["status"] = "found";
        out["reason"] = "known-file database presence is informational, not a safety verdict";
    }
    else
        throw std::runtime_error("Unknown provider");
    return out;
}
HttpResponse Https(const HttpRequest &request)
{
    // Only the three fixed hash endpoints can reach the network. No redirects,
    // sample uploads, credential-bearing diagnostics, or certificate bypass.
    const bool valid = (request.method == "GET" && request.body.empty() &&
                        ((request.url.starts_with("https://www.virustotal.com/api/v3/files/") &&
                          request.headerName == "x-apikey") ||
                         (request.url.starts_with("https://hashlookup.circl.lu/lookup/sha256/") &&
                          request.headerName.empty()))) ||
                       (request.method == "POST" && request.url == "https://mb-api.abuse.ch/api/v1/" &&
                        request.headerName == "Auth-Key" && request.body.starts_with("query=get_info&hash="));
    Require(valid, "Network request outside hash-only policy");
    const auto id = request.method == "POST"     ? std::string("malwarebazaar")
                    : request.headerName.empty() ? std::string("circl")
                                                 : std::string("virustotal");
    const auto hash = request.method == "POST"
                          ? request.body.substr(std::string("query=get_info&hash=").size())
                          : request.url.substr(request.url.find_last_of('/') + 1);
    const auto expected = ProviderRequest(id, hash, request.headerValue);
    Require(expected.url == request.url && expected.method == request.method &&
                expected.headerName == request.headerName && expected.body == request.body,
            "Noncanonical hash-only request rejected");
    HttpResponse result;
#ifdef _WIN32
    struct Handle
    {
        HINTERNET h = nullptr;
        ~Handle()
        {
            if (h)
                WinHttpCloseHandle(h);
        }
    } session, connection, operation;
    session.h = WinHttpOpen(L"Aegis/phase14", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    Require(session.h != nullptr, "HTTPS session failed");
    WinHttpSetTimeouts(session.h, 2000, 2000, 2000, 2000);
    const auto url = Utf8Path(request.url).wstring();
    URL_COMPONENTS c{};
    c.dwStructSize = sizeof(c);
    c.dwHostNameLength = DWORD(-1);
    c.dwUrlPathLength = DWORD(-1);
    Require(WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &c) && c.nScheme == INTERNET_SCHEME_HTTPS,
            "HTTPS URL rejected");
    const std::wstring host(c.lpszHostName, c.dwHostNameLength), path(c.lpszUrlPath, c.dwUrlPathLength);
    connection.h = WinHttpConnect(session.h, host.c_str(), c.nPort, 0);
    Require(connection.h != nullptr, "HTTPS connection failed");
    const auto method = Utf8Path(request.method).wstring();
    operation.h = WinHttpOpenRequest(connection.h, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    Require(operation.h != nullptr, "HTTPS request failed");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    Require(WinHttpSetOption(operation.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) !=
                FALSE,
            "HTTPS policy failed");
    std::wstring headers = L"Accept: application/json\r\n";
    if (!request.headerName.empty())
        headers += Utf8Path(request.headerName + ": " + request.headerValue + "\r\n").wstring();
    if (!request.body.empty())
        headers += L"Content-Type: application/x-www-form-urlencoded\r\n";
    Require(WinHttpSendRequest(operation.h, headers.c_str(), DWORD(headers.size()),
                               request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                                    : const_cast<char *>(request.body.data()),
                               DWORD(request.body.size()), DWORD(request.body.size()), 0) &&
                WinHttpReceiveResponse(operation.h, nullptr),
            "HTTPS request/response failed");
    DWORD status = 0, bytes = sizeof(status);
    Require(WinHttpQueryHeaders(operation.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes,
                                WINHTTP_NO_HEADER_INDEX) != FALSE,
            "HTTPS status unavailable");
    result.status = int(status);
    wchar_t retry[64]{};
    bytes = sizeof(retry);
    if (WinHttpQueryHeaders(operation.h, WINHTTP_QUERY_CUSTOM, L"Retry-After", retry, &bytes,
                            WINHTTP_NO_HEADER_INDEX))
        result.retryAfter = PathText(fs::path(retry));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    char buffer[16384];
    for (;;)
    {
        Require(std::chrono::steady_clock::now() < deadline, "HTTPS response deadline exceeded");
        DWORD n = 0;
        Require(WinHttpReadData(operation.h, buffer, sizeof(buffer), &n) != FALSE, "HTTPS read failed");
        if (!n)
            break;
        Require(n <= (1ull << 20) - result.body.size(), "HTTPS response size limit");
        result.body.append(buffer, n);
    }
#elif defined(GUARDIAN_SCANNER_CURL)
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    Require(initialized == CURLE_OK, "HTTPS initialization failed");
    auto *curl = curl_easy_init();
    Require(curl != nullptr, "HTTPS initialization failed");
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!request.headerName.empty())
        headers = curl_slist_append(headers, (request.headerName + ": " + request.headerValue).c_str());
    if (!request.body.empty())
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 7000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (request.method == "POST")
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(request.body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION, +[](char *p, size_t s, size_t n, void *user) -> size_t {
            auto &out = *static_cast<std::string *>(user);
            if (s && n > (1ull << 20) / s)
                return 0;
            const auto bytes = s * n;
            if (bytes > (1ull << 20) - out.size())
                return 0;
            out.append(p, bytes);
            return bytes;
        });
    const auto rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    Require(rc == CURLE_OK, "HTTPS request/response failed");
    result.status = int(status);
#else
    throw std::runtime_error("HTTPS transport not present in this build");
#endif
    return result;
}
Json Enrich(Store &store, const Config &config, const std::string &hash, int64_t now, Transport transport)
{
    Json rows = Json::array();
    for (const auto &provider : config.providers)
    {
        const auto id = provider.at("id").get<std::string>();
        Json row = {{"provider", id}, {"status", "disabled"}, {"classification", "unknown"}};
        if (!provider.value("enabled", false))
        {
            rows.push_back(row);
            continue;
        }
        const auto cache = store.Cached("intelligence:" + id, hash, now);
        if (!cache.is_null())
        {
            row = cache;
            row["cached"] = true;
            rows.push_back(row);
            continue;
        }
        if (config.offline)
        {
            row["status"] = "offline";
            rows.push_back(row);
            continue;
        }
        try
        {
            const auto env = provider.value("keyEnv", std::string());
            std::string secret;
#ifdef _WIN32
            if (!env.empty())
            {
                char value[1024]{};
                const auto n = GetEnvironmentVariableA(env.c_str(), value, DWORD(sizeof(value)));
                Require(n < sizeof(value), "Provider credential length rejected");
                if (n)
                    secret.assign(value, n);
            }
#else
            const auto *value = env.empty() ? nullptr : std::getenv(env.c_str());
            if (value)
                secret = value;
#endif
            const auto request = ProviderRequest(id, hash, secret);
            if (!store.Admit(id, now, provider.value("perMinute", 4u), provider.value("perDay", 500u)))
            {
                row["status"] = "rate-limited";
                rows.push_back(row);
                continue;
            }
            const auto response = transport(request);
            if (response.status == 429 || response.status == 503)
            {
                uint32_t seconds = 60;
                if (!response.retryAfter.empty() && response.retryAfter.size() <= 6 &&
                    response.retryAfter.find_first_not_of("0123456789") == response.retryAfter.npos)
                    seconds = std::clamp(uint32_t(std::stoul(response.retryAfter)), 60u, 86400u);
                store.Backoff(id, now + seconds);
                row["status"] = "rate-limited";
            }
            else
            {
                row = ProviderResponse(id, hash, response);
                store.Cache("intelligence:" + id, hash, row, now, provider.value("ttlSeconds", 3600u));
            }
        }
        catch (const std::exception &)
        {
            row["status"] = "unavailable";
            row["reason"] = "provider authentication, transport or response validation failed";
        }
        rows.push_back(row);
    }
    return rows;
}
} // namespace av::scan
