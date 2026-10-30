#include "DirectoryChanges.h"
#include <Windows.h>
#include <cstring>
namespace av::services {
DirectoryBatch DecodeDirectoryChanges(const std::filesystem::path &root, std::span<const uint8_t> bytes) {
    DirectoryBatch out;
    size_t at = 0;
    if (bytes.empty()) {
        out.rescanRequired = true;
        return out;
    }
    for (;;) {
        constexpr size_t header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        if (at > bytes.size() || bytes.size() - at < header) {
            out.rescanRequired = true;
            break;
        }
        DWORD next = 0, action = 0, size = 0;
        std::memcpy(&next, bytes.data() + at, 4);
        std::memcpy(&action, bytes.data() + at + 4, 4);
        std::memcpy(&size, bytes.data() + at + 8, 4);
        if (!size || size % 2 || size > bytes.size() - at - header) {
            out.rescanRequired = true;
            break;
        }
        std::wstring name(size / 2, L'\0');
        std::memcpy(name.data(), bytes.data() + at + header, size);
        std::filesystem::path path(name);
        bool valid = path.is_relative() && !path.has_root_path() && path == path.lexically_normal();
        for (const auto &part : path) {
            auto s = part.wstring();
            if (s.empty() || s == L"." || s == L".." || s.back() == L'.' || s.back() == L' ')
                valid = false;
        }
        for (const auto c : name)
            if (c < 32 || c == L':' || c == L'"' || c == L'*' || c == L'?' || c == L'<' || c == L'>' ||
                c == L'|')
                valid = false;
        if (!valid) {
            out.rescanRequired = true;
            break;
        }
        if (action == FILE_ACTION_ADDED || action == FILE_ACTION_MODIFIED ||
            action == FILE_ACTION_RENAMED_NEW_NAME)
            out.changed.push_back(root / path);
        if (!next)
            break;
        if (next % 4 || next < header + size || next > bytes.size() - at) {
            out.rescanRequired = true;
            break;
        }
        at += next;
    }
    if (out.rescanRequired)
        out.changed.clear();
    return out;
}
} // namespace av::services
