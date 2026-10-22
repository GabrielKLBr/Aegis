// Common.h - shared declarations and small helpers for the AntiVirus project.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>
#include <cstdint>

namespace av {

// ---- string conversions --------------------------------------------------
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);
std::wstring ToLower(std::wstring s);

// ---- console output helpers ---------------------------------------------
enum class Color { Default, Red, Green, Yellow, Cyan, Gray };

void Print(const std::wstring& text, Color c = Color::Default);
void PrintLine(const std::wstring& text, Color c = Color::Default);

// Returns true if the current process is running elevated (Administrator).
bool IsElevated();

} // namespace av
