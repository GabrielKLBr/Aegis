// EngineFacade.cpp
#include "EngineFacade.h"

namespace av {

void Engine::StartScan(const std::wstring& root,
                       std::function<void()> onUpdate,
                       std::function<void()> onDone) {
    if (state_.running.load()) return;
    if (worker_.joinable()) worker_.join();

    state_.running = true;
    state_.cancel  = false;
    state_.filesScanned = 0;
    state_.threatsFound = 0;
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        state_.detections.clear();
        state_.currentFile.clear();
        state_.rootPath = root;
    }

    worker_ = std::thread([this, root, onUpdate, onDone]() {
        Walk(root, onUpdate);
        state_.running = false;
        if (onUpdate) onUpdate();
        if (onDone)   onDone();
    });
}

void Engine::Walk(const std::wstring& dir, const std::function<void()>& onUpdate) {
    if (state_.cancel.load()) return;

    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (state_.cancel.load()) break;
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = dir + L"\\" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            Walk(full, onUpdate);
        } else {
            {
                std::lock_guard<std::mutex> lk(state_.mtx);
                state_.currentFile = full;
            }
            Detection det;
            if (db_.Match(full, det)) {
                state_.threatsFound++;
                std::lock_guard<std::mutex> lk(state_.mtx);
                state_.detections.push_back(det);
            }
            uint64_t n = ++state_.filesScanned;
            if ((n & 0x0F) == 0 && onUpdate) onUpdate(); // throttle UI updates
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    if (onUpdate) onUpdate();
}

} // namespace av
