// Scanner.h - walks files/directories and applies the signature database.
#pragma once
#include "Common.h"
#include "SignatureDatabase.h"
#include "Quarantine.h"

namespace av {

struct ScanStats {
    uint64_t filesScanned = 0;
    uint64_t threatsFound = 0;
    uint64_t quarantined  = 0;
    uint64_t errors       = 0;
    uint64_t suspicious   = 0;
    uint64_t unknown      = 0;
};

class Scanner {
public:
    Scanner(const SignatureDatabase& db, Quarantine* quarantine, bool autoQuarantine)
        : db_(db), quarantine_(quarantine), autoQuarantine_(autoQuarantine) {}

    // Scans a single file. Returns true if a threat was detected.
    bool ScanFile(const std::wstring& path, ScanStats& stats);

    // Recursively scans a directory tree (or a single file if 'path' is a file).
    void ScanPath(const std::wstring& path, ScanStats& stats, uint32_t depth=0);

private:
    const SignatureDatabase& db_;
    Quarantine* quarantine_;
    bool autoQuarantine_;
};

} // namespace av
