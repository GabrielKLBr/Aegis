// SignatureDatabase.cpp
#include "SignatureDatabase.h"
#include "Sha256.h"

#include <fstream>
#include <filesystem>
#include <string>

namespace av {

// The EICAR test pattern is the industry-standard, harmless way to verify an
// antivirus reacts. We assemble the trigger fragment at runtime so this source
// file itself is not flagged by real scanners.
static std::string EicarFragment() {
    // "EICAR-STANDARD-ANTIVIRUS-TEST-FILE"
    return std::string("EICAR-STANDARD-") + "ANTIVIRUS-" + "TEST-FILE";
}

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool SignatureDatabase::Load(const std::wstring& path) {
    // std::filesystem::path makes the wide path portable across MSVC and MinGW
    // (plain std::ifstream(std::wstring) is an MSVC-only extension).
    std::ifstream f(std::filesystem::path{path});
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t p1 = line.find(':');
        if (p1 == std::string::npos) continue;
        size_t p2 = line.find(':', p1 + 1);
        if (p2 == std::string::npos) continue;

        std::string type = line.substr(0, p1);
        std::string value = line.substr(p1 + 1, p2 - p1 - 1);
        std::string name  = line.substr(p2 + 1);

        Signature sig;
        sig.value = value;
        sig.name  = Utf8ToWide(name);
        if (type == "sha256") {
            sig.type = SigType::Sha256;
            sig.value = WideToUtf8(ToLower(Utf8ToWide(value)));
        } else if (type == "substr") {
            sig.type = SigType::Substr;
        } else {
            continue;
        }
        sigs_.push_back(std::move(sig));
    }
    return true;
}

bool SignatureDatabase::ContentMatch(const std::wstring& path, Detection& out) const {
    // Read up to a bounded amount of the file and search for substring rules
    // plus the built-in EICAR fragment.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const DWORD kMax = 4 * 1024 * 1024; // cap content scan at 4 MiB
    std::string data;
    data.reserve(1 << 16);
    std::vector<char> buf(1 << 16);
    DWORD total = 0;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr) || read == 0) break;
        data.append(buf.data(), read);
        total += read;
        if (total >= kMax) break;
    }
    CloseHandle(h);

    const std::string eicar = EicarFragment();
    if (data.find(eicar) != std::string::npos) {
        out.file = path;
        out.threat = L"EICAR-Test-File";
        out.reason = L"content signature (EICAR)";
        return true;
    }

    for (const auto& sig : sigs_) {
        if (sig.type != SigType::Substr) continue;
        if (!sig.value.empty() && data.find(sig.value) != std::string::npos) {
            out.file = path;
            out.threat = sig.name;
            out.reason = L"content signature";
            return true;
        }
    }
    return false;
}

bool SignatureDatabase::Match(const std::wstring& path, Detection& out,
                              const std::string* precomputedSha256) const {
    // 1) Hash-based detection.
    bool hashOk = false;
    std::string hash;
    if (precomputedSha256) {
        hash = *precomputedSha256;
        hashOk = !hash.empty();
    } else {
        hash = Sha256File(path, hashOk);
    }
    if (hashOk) {
        for (const auto& sig : sigs_) {
            if (sig.type == SigType::Sha256 && sig.value == hash) {
                out.file = path;
                out.threat = sig.name;
                out.reason = L"sha256 match";
                return true;
            }
        }
    }

    // 2) Content/substring detection (catches EICAR regardless of hash).
    return ContentMatch(path, out);
}

} // namespace av
