// RealTimeMonitor.cpp
#include "RealTimeMonitor.h"

namespace av {

void RealTimeMonitor::Run() {
    HANDLE hDir = CreateFileW(dir_.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hDir == INVALID_HANDLE_VALUE) {
        PrintLine(L"Cannot open directory for monitoring: " + dir_, Color::Red);
        return;
    }

    PrintLine(L"Real-time protection active on: " + dir_, Color::Green);
    PrintLine(L"Watching for new/modified files. Press Ctrl+C to stop.", Color::Gray);
    PrintLine(L"", Color::Default);

    std::vector<BYTE> buffer(64 * 1024);
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                         FILE_NOTIFY_CHANGE_LAST_WRITE |
                         FILE_NOTIFY_CHANGE_SIZE |
                         FILE_NOTIFY_CHANGE_CREATION;

    for (;;) {
        DWORD bytesReturned = 0;
        BOOL ok = ReadDirectoryChangesW(hDir, buffer.data(), (DWORD)buffer.size(),
                                        TRUE /*subtree*/, filter,
                                        &bytesReturned, nullptr, nullptr);
        if (!ok || bytesReturned == 0) {
            PrintLine(L"Monitor stopped.", Color::Yellow);
            break;
        }

        BYTE* p = buffer.data();
        for (;;) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            if (info->Action == FILE_ACTION_ADDED ||
                info->Action == FILE_ACTION_MODIFIED ||
                info->Action == FILE_ACTION_RENAMED_NEW_NAME) {

                std::wstring rel(info->FileName, info->FileNameLength / sizeof(WCHAR));
                std::wstring full = dir_ + L"\\" + rel;

                DWORD attr = GetFileAttributesW(full.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    PrintLine(L"[event] " + rel, Color::Cyan);
                    ScanStats s;
                    scanner_.ScanFile(full, s);
                }
            }
            if (info->NextEntryOffset == 0) break;
            p += info->NextEntryOffset;
        }
    }
    CloseHandle(hDir);
}

} // namespace av
