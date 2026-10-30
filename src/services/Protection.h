#pragma once
#include "Services.h"
#include <memory>

namespace av::services {
// A borrowed token is used only while opening caller files, never vault/state files.
class UserAccess {
    bool active_ = false;

  public:
    explicit UserAccess(HANDLE token);
    ~UserAccess();
    UserAccess(const UserAccess &) = delete;
};
fs::path ProtectionRoot();
fs::path LocalPath(const fs::path &);
bool Beneath(const fs::path &path, const fs::path &root);
void OrdinaryPath(const fs::path &);
Json CallerJson(const fs::path &, HANDLE token);
HANDLE RestrictUserToken(HANDLE token);
Json RunRestricted(HANDLE token, Operation, const fs::path &, HANDLE cancellation,
                   const std::function<void(const Json &)> &progress = {}, uint32_t milliseconds = 15000);

class Protection {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Protection(const std::wstring &owner, HANDLE stopping);
    ~Protection();
    Json Dispatch(Operation, const fs::path &request, HANDLE user, bool administrator);
};
} // namespace av::services
