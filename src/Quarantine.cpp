// Quarantine.cpp
#include "Quarantine.h"
#include "Sha256.h"

#include <fstream>
#include <filesystem>
#include <shlobj.h>

namespace av {

static const BYTE kXorKey = 0xA7; // simple neutralization so the file can't run

Quarantine::Quarantine(std::wstring dir) : dir_(std::move(dir)) {}

void Quarantine::EnsureDir() const {
    SHCreateDirectoryExW(nullptr, dir_.c_str(), nullptr);
}

bool Quarantine::Isolate(const Detection& det) {
    EnsureDir();

    // Read original bytes.
    HANDLE h = CreateFileW(det.file.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::vector<BYTE> data((size_t)size.QuadPart);
    DWORD read = 0;
    bool readOk = !data.empty()
                    ? (ReadFile(h, data.data(), (DWORD)data.size(), &read, nullptr) && read == data.size())
                    : true;
    CloseHandle(h);
    if (!readOk) return false;

    // Derive a unique quarantine name from the file's hash.
    std::string hash = Sha256Buffer(data.data(), data.size());
    if (hash.empty()) hash = "unknown";
    std::wstring qbase = dir_ + L"\\" + Utf8ToWide(hash);
    std::wstring qfile = qbase + L".quarantine";
    std::wstring qmeta = qbase + L".meta.txt";

    // Neutralize and write.
    for (auto& b : data) b ^= kXorKey;
    {
        std::ofstream out(std::filesystem::path{qfile}, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (!data.empty()) out.write((const char*)data.data(), (std::streamsize)data.size());
    }
    {
        std::ofstream meta(std::filesystem::path{qmeta}, std::ios::trunc);
        meta << "original_path: " << WideToUtf8(det.file) << "\n";
        meta << "threat: "        << WideToUtf8(det.threat) << "\n";
        meta << "reason: "        << WideToUtf8(det.reason) << "\n";
        meta << "sha256: "        << hash << "\n";
        meta << "xor_key: 0xA7\n";
    }

    // Remove the original threat.
    if (!DeleteFileW(det.file.c_str())) {
        // If we cannot delete (e.g. locked), at least try to strip execute by truncating.
        HANDLE t = CreateFileW(det.file.c_str(), GENERIC_WRITE, 0, nullptr,
                               TRUNCATE_EXISTING, 0, nullptr);
        if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
        return false;
    }
    return true;
}

void Quarantine::List() const {
    std::wstring pattern = dir_ + L"\\*.meta.txt";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLine(L"  (quarantine is empty)", Color::Gray);
        return;
    }
    int count = 0;
    do {
        std::wstring metaPath = dir_ + L"\\" + fd.cFileName;
        std::ifstream meta(std::filesystem::path{metaPath});
        std::string line;
        PrintLine(L"  - " + std::wstring(fd.cFileName), Color::Yellow);
        while (std::getline(meta, line))
            PrintLine(L"      " + Utf8ToWide(line), Color::Gray);
        ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (count == 0) PrintLine(L"  (quarantine is empty)", Color::Gray);
}

} // namespace av
