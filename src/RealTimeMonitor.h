// RealTimeMonitor.h - watches a directory and scans files as they change.
#pragma once
#include "Common.h"
#include "Scanner.h"

namespace av {

class RealTimeMonitor {
public:
    RealTimeMonitor(Scanner& scanner, std::wstring dir)
        : scanner_(scanner), dir_(std::move(dir)) {}

    // Blocks, watching the directory tree with ReadDirectoryChangesW.
    // Press Ctrl+C to stop. Returns when the watch handle closes/errors.
    void Run();

private:
    Scanner& scanner_;
    std::wstring dir_;
};

} // namespace av
