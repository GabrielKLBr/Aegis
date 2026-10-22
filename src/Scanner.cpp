// Scanner.cpp
#include "Scanner.h"

namespace av {

static bool IsDirectory(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool Scanner::ScanFile(const std::wstring& path, ScanStats& stats) {
    Detection det;
    bool found = db_.Match(path, det);
    stats.filesScanned++;

    if (found) {
        stats.threatsFound++;
        PrintLine(L"  [THREAT] " + det.threat + L"  (" + det.reason + L")", Color::Red);
        PrintLine(L"           " + path, Color::Gray);

        if (autoQuarantine_ && quarantine_) {
            if (quarantine_->Isolate(det)) {
                stats.quarantined++;
                PrintLine(L"           -> quarantined", Color::Yellow);
            } else {
                stats.errors++;
                PrintLine(L"           -> FAILED to quarantine (locked / access denied)", Color::Yellow);
            }
        }
    }
    return found;
}

void Scanner::ScanPath(const std::wstring& path, ScanStats& stats) {
    if (!IsDirectory(path)) {
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            PrintLine(L"  [skip] cannot access: " + path, Color::Gray);
            stats.errors++;
            return;
        }
        ScanFile(path, stats);
        return;
    }

    std::wstring pattern = path + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring child = path + L"\\" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Skip reparse points to avoid symlink loops.
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            ScanPath(child, stats);
        } else {
            ScanFile(child, stats);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

} // namespace av
