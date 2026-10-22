// Sha256.h - SHA-256 hashing using the Windows CNG (BCrypt) API (no external deps).
#pragma once
#include "Common.h"

namespace av {

// Computes the lowercase hex SHA-256 of a file's contents.
// On success returns the 64-char digest and sets ok=true.
// On failure (file unreadable, etc.) returns "" and sets ok=false.
std::string Sha256File(const std::wstring& path, bool& ok);

// Computes the lowercase hex SHA-256 of an in-memory buffer.
std::string Sha256Buffer(const void* data, size_t len);

} // namespace av
