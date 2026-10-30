#pragma once
#include "scanner/Scanner.h"
namespace av::quarantine {
inline constexpr uint64_t MaxFileBytes = 64ull << 20;
scan::fs::path DefaultRoot();
class Store {
  public:
    explicit Store(const scan::fs::path &root = DefaultRoot());
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    scan::Json Isolate(const scan::fs::path &, const std::string &expectedSha256 = {},
                       const std::string &threat = "Manual", const std::string &reason = {});
    scan::Json List();
    scan::Json Restore(const std::string &id, const scan::fs::path &authorizedDirectory,
                       const std::wstring &filename);
    scan::Json Erase(const std::string &id);
    scan::Json Audit(uint64_t beforeSequence = 0);
#ifdef AEGIS_QUARANTINE_TESTING
    void TestMaxPages(unsigned);
    void TestAuditEvents(unsigned);
#endif
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#ifdef AEGIS_QUARANTINE_TESTING
void TestFault(const std::string &point, bool terminate);
#endif
} // namespace av::quarantine
