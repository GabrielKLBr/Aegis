#pragma once
#include "Scanner.h"
#include <functional>
struct sqlite3;
namespace av::scan
{
class Store
{
  public:
    explicit Store(const fs::path &);
    ~Store();
    Json Cached(const std::string &category, const std::string &key, int64_t now);
    void Cache(const std::string &category, const std::string &key, const Json &, int64_t now, uint32_t ttl);
    bool Admit(const std::string &provider, int64_t now, uint32_t minute, uint32_t daily);
    void Backoff(const std::string &provider, int64_t until);
    Json Enqueue(const std::string &hash, const std::string &name, const fs::path &payload, int64_t now);
    Json Jobs();
    Json Claim();
    void Finish(const std::string &id, const Json &);

  private:
    sqlite3 *db_ = nullptr;
    void Exec(const char *);
};
struct HttpRequest
{
    std::string url, method = "GET", headerName, headerValue, body;
};
struct HttpResponse
{
    int status = 0;
    std::string body, retryAfter;
};
using Transport = std::function<HttpResponse(const HttpRequest &)>;
HttpRequest ProviderRequest(const std::string &, const std::string &hash, const std::string &key);
Json ProviderResponse(const std::string &, const std::string &hash, const HttpResponse &);
HttpResponse Https(const HttpRequest &);
Json Enrich(Store &, const Config &, const std::string &hash, int64_t now, Transport transport = Https);
} // namespace av::scan
