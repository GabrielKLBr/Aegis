#pragma once
#include "State.h"
#include <filesystem>
namespace av::microvm::state {
std::vector<uint8_t> ReadFile(const std::filesystem::path &, uint64_t limit = MaxStateBytes);
void AtomicWrite(const std::filesystem::path &, std::span<const uint8_t>);
void AtomicText(const std::filesystem::path &, const std::string &);
std::string Token();
bool Ordinary(const std::filesystem::path &);
class Lease {
  public:
    explicit Lease(const std::filesystem::path &, bool tryOnly = false);
    ~Lease();
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    bool Held() const { return held_; }

  private:
#ifdef _WIN32
    void *handle_ = reinterpret_cast<void *>(intptr_t(-1));
#else
    int handle_ = -1;
#endif
    bool held_ = false;
};
// Test fault injection: failure before the Nth durable-file transaction step.
void FailAfter(int64_t steps);
} // namespace av::microvm::state
namespace av::microvm::state {
void PublishDirectory(const std::filesystem::path &source, const std::filesystem::path &target);
}
