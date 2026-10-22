// Util.cpp - implementation of small shared helpers.
#include "Common.h"
#include <cwctype>

namespace av {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

static WORD ColorToAttr(Color c) {
    switch (c) {
        case Color::Red:    return FOREGROUND_RED | FOREGROUND_INTENSITY;
        case Color::Green:  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case Color::Yellow: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case Color::Cyan:   return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case Color::Gray:   return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        default:            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

void Print(const std::wstring& text, Color c) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    // Detect a real console. WriteConsoleW only works on a console screen
    // buffer; when stdout is redirected to a pipe or file we must emit bytes.
    DWORD mode = 0;
    bool isConsole = GetConsoleMode(h, &mode) != 0;

    if (!isConsole) {
        // Redirected: write UTF-8 bytes, ignore colors.
        std::string utf8 = WideToUtf8(text);
        DWORD written = 0;
        WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO info{};
    WORD saved = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if (GetConsoleScreenBufferInfo(h, &info)) saved = info.wAttributes;
    if (c != Color::Default) SetConsoleTextAttribute(h, ColorToAttr(c));
    DWORD written = 0;
    WriteConsoleW(h, text.c_str(), (DWORD)text.size(), &written, nullptr);
    if (c != Color::Default) SetConsoleTextAttribute(h, saved);
}

void PrintLine(const std::wstring& text, Color c) {
    Print(text, c);
    Print(L"\r\n", Color::Default);
}

bool IsElevated() {
    bool elevated = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return elevated;
}

} // namespace av
