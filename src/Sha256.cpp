// Sha256.cpp - SHA-256 via BCrypt.
#include "Sha256.h"
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace av {

static std::string ToHex(const std::vector<UCHAR>& bytes) {
    static const char* k = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (UCHAR b : bytes) {
        out.push_back(k[b >> 4]);
        out.push_back(k[b & 0x0F]);
    }
    return out;
}

// Internal helper that hashes a stream of chunks fed by a callback.
template <typename Feed>
static std::string HashWith(Feed feed, bool& ok) {
    ok = false;
    BCRYPT_ALG_HANDLE  hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::string result;

    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return result;

    DWORD cbObject = 0, cbHash = 0, cbData = 0;
    if (!NT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
            (PUCHAR)&cbObject, sizeof(DWORD), &cbData, 0))) goto cleanup;
    if (!NT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
            (PUCHAR)&cbHash, sizeof(DWORD), &cbData, 0))) goto cleanup;

    {
        std::vector<UCHAR> obj(cbObject);
        std::vector<UCHAR> digest(cbHash);

        if (!NT_SUCCESS(BCryptCreateHash(hAlg, &hHash, obj.data(), cbObject, nullptr, 0, 0)))
            goto cleanup;

        if (!feed([&](const void* p, size_t n) -> bool {
                return NT_SUCCESS(BCryptHashData(hHash, (PUCHAR)p, (ULONG)n, 0));
            }))
            goto cleanup;

        if (!NT_SUCCESS(BCryptFinishHash(hHash, digest.data(), cbHash, 0)))
            goto cleanup;

        result = ToHex(digest);
        ok = true;
    }

cleanup:
    if (hHash) BCryptDestroyHash(hHash);
    if (hAlg)  BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}

std::string Sha256File(const std::wstring& path, bool& ok) {
    ok = false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};

    std::vector<BYTE> buf(1 << 16); // 64 KiB
    std::string result = HashWith([&](auto sink) -> bool {
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr)) return false;
            if (read == 0) break;
            if (!sink(buf.data(), read)) return false;
        }
        return true;
    }, ok);

    CloseHandle(h);
    return result;
}

std::string Sha256Buffer(const void* data, size_t len) {
    bool ok = false;
    return HashWith([&](auto sink) -> bool {
        return sink(data, len);
    }, ok);
}

} // namespace av
