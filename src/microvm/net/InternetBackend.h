#pragma once
#include "guardian/Internet.h"
#include "../state/Files.h"
#include <memory>
namespace av::microvm::internet_session {
using guardian::internet::Policy;
Policy ReadPolicy(const std::filesystem::path&);
void Validate(const Policy&);
class Backend {
public:
    explicit Backend(const Policy&);
    ~Backend();
    Backend(const Backend&)=delete;
    Backend& operator=(const Backend&)=delete;
    bool Exchange(guardian::internet::Request&);
    std::string Json() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
