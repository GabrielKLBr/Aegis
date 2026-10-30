// SignatureDatabase.h - loads and matches malware signatures.
#pragma once
#include "Common.h"
#include "scanner/Scanner.h"

namespace av {

enum class SigType { Sha256, Substr };

struct Signature {
    SigType     type;
    std::string value;   // sha256 hex (lowercase) OR ascii substring to search for
    std::wstring name;   // threat name reported on a match
};

struct Detection {
    std::wstring file;
    std::wstring threat;
    std::wstring reason; // e.g. "sha256 match" or "content signature"
    std::string sha256;
};

class SignatureDatabase {
public:
    // Loads signatures from a text file. Format, one per line:
    //   sha256:<64-hex>:<ThreatName>
    //   substr:<ascii-bytes>:<ThreatName>
    // Lines beginning with '#' and blank lines are ignored.
    // Returns false if the file cannot be opened (built-in EICAR rule still active).
    bool Load(const std::wstring& path);

    // Number of loaded signatures (excluding the built-in EICAR content rule).
    size_t Count() const { return engine_ ? engine_->SignatureCount() : 0; }
    scan::Json Analyze(const std::wstring&, const std::atomic<bool>* cancel = nullptr) const;

    // Compatibility wrapper for confirmed local detections. Prefer Analyze:
    // false cannot express incomplete coverage. Caller hashes are deliberately
    // ignored: the worker verifies the exact bytes of its own bounded snapshot.
    bool Match(const std::wstring& path, Detection& out,
               const std::string* precomputedSha256 = nullptr) const;

private:
    mutable std::shared_ptr<scan::Engine> engine_;
    std::string loadError_;
};

} // namespace av
