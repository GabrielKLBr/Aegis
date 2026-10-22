// EngineFacade.h - thin, UI-friendly wrapper around the scan engine.
// Decoupled from the console layer: progress is reported via callbacks so a GUI
// can marshal updates to its own thread.
#pragma once
#include "Common.h"
#include "SignatureDatabase.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <vector>

namespace av {

struct ScanState {
    std::atomic<bool>     running{false};
    std::atomic<bool>     cancel{false};
    std::atomic<uint64_t> filesScanned{0};
    std::atomic<uint64_t> threatsFound{0};

    std::mutex            mtx;          // guards the fields below
    std::wstring          currentFile;
    std::wstring          rootPath;
    std::vector<Detection> detections;
};

class Engine {
public:
    ~Engine() { Cancel(); Join(); }

    bool   LoadSignatures(const std::wstring& path) { return db_.Load(path); }
    size_t SignatureCount() const { return db_.Count(); }

    // Starts a background scan. onUpdate is invoked (on the worker thread,
    // throttled) as progress changes; onDone once at the end. The UI is
    // expected to PostMessage from these callbacks. No-op if already running.
    void StartScan(const std::wstring& root,
                   std::function<void()> onUpdate,
                   std::function<void()> onDone);

    void Cancel() { state_.cancel = true; }
    void Join()   { if (worker_.joinable()) worker_.join(); }

    bool IsRunning() const { return state_.running.load(); }
    ScanState& State() { return state_; }

private:
    void Walk(const std::wstring& dir, const std::function<void()>& onUpdate);

    SignatureDatabase db_;
    ScanState         state_;
    std::thread       worker_;
};

} // namespace av
