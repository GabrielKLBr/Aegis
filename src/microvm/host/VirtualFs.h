// VirtualFs.h - an in-memory virtual file system for the NT personality.
//
// File operations a guest PE performs (CreateFile/WriteFile/ReadFile/Close) are
// serviced here instead of touching the host disk: every path the sample writes
// to lives only in this map. Combined with ReportBuilder it answers "what files
// did the sample create / write?" without any real file ever existing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace av {
namespace microvm {

class VirtualFs {
public:
    struct OpenResult { uint64_t handle; bool created; bool found; };

    // Open a file. `createIfMissing` distinguishes NtOpenFile / FILE_OPEN (a pure
    // open that must fail if the file does not exist) from create dispositions.
    // `found` is false when the path does not exist (or is a blocked VM artifact);
    // the caller then returns STATUS_OBJECT_NAME_NOT_FOUND.
    OpenResult Open(const std::string& path, bool createIfMissing = true);
    bool   Write(uint64_t handle, const uint8_t* data, size_t len);
    size_t Read(uint64_t handle, uint8_t* out, size_t len);
    bool   Close(uint64_t handle);

    // Anti-VM: pre-seed a normal file as already present (a lived-in disk), and
    // blocklist a VM/sandbox artifact so any open of a path containing `needle`
    // (case-insensitive) reports "not found" -- the VM drivers simply aren't here.
    void Seed(const std::string& path, std::vector<uint8_t> content = {});
    void Block(const std::string& needle);
    bool IsBlocked(const std::string& path) const;

    bool        IsValidHandle(uint64_t h) const { return handles_.count(h) != 0; }
    size_t      OpenHandleCount() const { return handles_.size(); }
    std::string PathOf(uint64_t h) const;
    const std::map<std::string, std::vector<uint8_t>>& Files() const { return files_; }

private:
    struct OpenFile { std::string path; uint64_t pos; };
    std::map<std::string, std::vector<uint8_t>> files_;  // path -> content
    std::map<uint64_t, OpenFile> handles_;
    std::vector<std::string> blocked_;             // lowercased VM-artifact needles
    uint64_t nextHandle_ = 0x100;  // looks like a small Win32 HANDLE
};

} // namespace microvm
} // namespace av
