// Scanner.cpp
#include "Scanner.h"

namespace av {

static bool IsDirectory(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool Scanner::ScanFile(const std::wstring& path, ScanStats& stats) {
    Detection det;
    const auto result = db_.Analyze(path);
    const bool found = result.at("classification") == "detected";
    if (!result.value("complete", false)) {
        ++stats.errors;
        PrintLine(L"  [INCONCLUSIVE] " + path, Color::Yellow);
    }
    if (result.at("classification") == "suspicious") {
        ++stats.suspicious;
        PrintLine(L"  [SUSPICIOUS] " + path, Color::Yellow);
    } else if (result.at("classification") == "unknown") ++stats.unknown;
    if (found) for (const auto& f : result.at("findings")) {
        if (f.at("classification") != "detected") continue;
        det = {path, Utf8ToWide(f.at("rule").get<std::string>()), Utf8ToWide(f.at("kind").get<std::string>())}; break;
    }
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

void Scanner::ScanPath(const std::wstring& path, ScanStats& stats, uint32_t depth) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) { ++stats.errors; return; }
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

    if (depth >= 64) { ++stats.errors; return; }
    std::wstring pattern = path + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { ++stats.errors; return; }

    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring child = path + L"\\" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Skip reparse points to avoid symlink loops.
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++stats.errors; continue; }
            ScanPath(child, stats, depth+1);
        } else {
            ScanFile(child, stats);
        }
    } while (FindNextFileW(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES) ++stats.errors;
    FindClose(h);
}

} // namespace av
