// VirtualFs.cpp - implementation of the in-memory virtual file system.
#include "VirtualFs.h"

#include <cctype>

namespace av {
namespace microvm {

static std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

void VirtualFs::Seed(const std::string& path, std::vector<uint8_t> content) {
    files_[path] = std::move(content);
}

void VirtualFs::Block(const std::string& needle) {
    blocked_.push_back(Lower(needle));
}

bool VirtualFs::IsBlocked(const std::string& path) const {
    std::string low = Lower(path);
    for (const auto& n : blocked_) if (low.find(n) != std::string::npos) return true;
    return false;
}

VirtualFs::OpenResult VirtualFs::Open(const std::string& path, bool createIfMissing) {
    // A blocked VM artifact never exists, no matter the disposition.
    if (IsBlocked(path)) return {0, false, false};
    bool exists = files_.find(path) != files_.end();
    if (!exists && !createIfMissing) return {0, false, false};  // pure open of absent file
    if (!exists) files_[path];                 // create empty
    uint64_t h = nextHandle_;
    nextHandle_ += 4;
    handles_[h] = OpenFile{path, 0};
    return {h, !exists, true};
}

bool VirtualFs::Write(uint64_t handle, const uint8_t* data, size_t len) {
    auto it = handles_.find(handle);
    if (it == handles_.end()) return false;
    auto& file = files_[it->second.path];
    uint64_t pos = it->second.pos;
    if (pos + len > file.size()) file.resize(pos + len);
    for (size_t i = 0; i < len; ++i) file[pos + i] = data[i];
    it->second.pos = pos + len;
    return true;
}

size_t VirtualFs::Read(uint64_t handle, uint8_t* out, size_t len) {
    auto it = handles_.find(handle);
    if (it == handles_.end()) return 0;
    auto& file = files_[it->second.path];
    uint64_t pos = it->second.pos;
    size_t n = 0;
    for (; n < len && pos + n < file.size(); ++n) out[n] = file[pos + n];
    it->second.pos = pos + n;
    return n;
}

bool VirtualFs::Close(uint64_t handle) {
    return handles_.erase(handle) != 0;
}

std::string VirtualFs::PathOf(uint64_t h) const {
    auto it = handles_.find(h);
    return it == handles_.end() ? std::string("<invalid handle>") : it->second.path;
}

} // namespace microvm
} // namespace av
