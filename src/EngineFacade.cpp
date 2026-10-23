// EngineFacade.cpp
#include "EngineFacade.h"

namespace av
{

void Engine::StartScan(const std::wstring &root, std::function<void()> onUpdate, std::function<void()> onDone)
{
    if (state_.running.load())
        return;
    if (worker_.joinable())
        worker_.join();

    state_.running = true;
    state_.cancel = false;
    state_.filesScanned = 0;
    state_.threatsFound = 0;
    state_.suspicious = 0;
    state_.inconclusive = 0;
    state_.unknown = 0;
    {
        std::lock_guard<std::mutex> lk(state_.mtx);
        state_.detections.clear();
        state_.currentFile.clear();
        state_.rootPath = root;
    }

    worker_ = std::thread([this, root, onUpdate, onDone]() {
        Walk(root, onUpdate);
        state_.running = false;
        if (onUpdate)
            onUpdate();
        if (onDone)
            onDone();
    });
}

void Engine::Walk(const std::wstring &dir, const std::function<void()> &onUpdate, uint32_t depth)
{
    if (state_.cancel.load())
        return;
    const auto attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        ++state_.inconclusive;
        return;
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        {
            std::lock_guard lock(state_.mtx);
            state_.currentFile = dir;
        }
        const auto r = db_.Analyze(dir, &state_.cancel);
        if (!r.value("complete", false))
            ++state_.inconclusive;
        const auto c = r.at("classification").get<std::string>();
        if (c == "detected" || c == "suspicious")
        {
            if (c == "detected")
                ++state_.threatsFound;
            else
                ++state_.suspicious;
            if (!r.at("findings").empty())
            {
                const auto &f = r.at("findings").front();
                std::lock_guard lock(state_.mtx);
                state_.detections.push_back({dir, Utf8ToWide(f.at("rule").get<std::string>()),
                                             Utf8ToWide(f.at("classification").get<std::string>())});
            }
        }
        else if (c == "unknown")
            ++state_.unknown;
        ++state_.filesScanned;
        if (onUpdate)
            onUpdate();
        return;
    }
    if (depth >= 64)
    {
        ++state_.inconclusive;
        return;
    }
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        ++state_.inconclusive;
        return;
    }
    do
    {
        if (state_.cancel.load())
            break;
        const std::wstring name = fd.cFileName;
        if (name != L"." && name != L"..")
            Walk(dir + L"\\" + name, onUpdate, depth + 1);
    } while (FindNextFileW(h, &fd));
    if (!state_.cancel.load() && GetLastError() != ERROR_NO_MORE_FILES)
        ++state_.inconclusive;
    FindClose(h);
    if (onUpdate)
        onUpdate();
}
} // namespace av
