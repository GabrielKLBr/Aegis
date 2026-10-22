// SignatureDatabase.h - loads and matches malware signatures.
#pragma once
#include "Common.h"

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
    size_t Count() const { return sigs_.size(); }

    // Tests a file against all signatures. Returns true if a threat is found,
    // filling 'out'. 'precomputedSha256' may be provided to avoid re-hashing.
    bool Match(const std::wstring& path, Detection& out,
               const std::string* precomputedSha256 = nullptr) const;

private:
    std::vector<Signature> sigs_;

    bool ContentMatch(const std::wstring& path, Detection& out) const;
};

} // namespace av
