#include "Services.h"
#include <sddl.h>
#include <vector>
#include <stdexcept>

namespace av::services {
const wchar_t* Name(Kind k) { return k == Kind::Antivirus ? L"AegisAntivirus" : L"AegisSandbox"; }
const wchar_t* Label(Kind k) { return k == Kind::Antivirus ? L"Aegis — Antivirus" : L"Aegis — Sandbox MiniNT"; }
std::wstring PipeName(Kind k) { return std::wstring(L"\\\\.\\pipe\\Aegis.Engine.v1.") + Name(k); }
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string Text(const std::wstring& s) {
    if (s.empty()) return {};
    Check(s.size() <= INT_MAX, "Text limit exceeded");
    const auto n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    Check(n > 0, "Invalid UTF-16");
    std::string out(size_t(n), '\0');
    Check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n, nullptr, nullptr) == n, "UTF-8 conversion failed");
    return out;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    Check(s.size() <= INT_MAX, "Text limit exceeded");
    const auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    Check(n > 0, "Invalid UTF-8");
    std::wstring out(size_t(n), L'\0');
    Check(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n) == n, "UTF-16 conversion failed");
    return out;
}
std::wstring ErrorText(DWORD code) {
    wchar_t* message = nullptr;
    const auto n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring out = n ? std::wstring(message, n) : L"Windows error " + std::to_wstring(code);
    LocalFree(message);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    return out;
}
[[noreturn]] void WinError(const char* operation, DWORD error) {
    throw WindowsError(std::string(operation) + " (" + std::to_string(error) + "): " + Text(ErrorText(error)), error);
}
fs::path Executable() {
    std::vector<wchar_t> data(32768);
    const auto n = GetModuleFileNameW(nullptr, data.data(), DWORD(data.size()));
    Check(n && n < data.size(), "Executable path unavailable");
    return fs::path(std::wstring(data.data(), n));
}
std::wstring UserSid(HANDLE token) {
    Handle owned;
    if (!token) {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &owned.value)) WinError("Open process token");
        token = owned.value;
    }
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<uint8_t> data(bytes);
    if (!bytes || !GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes)) WinError("Read token user");
    wchar_t* sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) WinError("Format user SID");
    const std::wstring out(sid);
    LocalFree(sid);
    return out;
}
void Validate(const Request& r) {
    Check(r.magic == Magic && r.version == Version, "Engine protocol version rejected");
    Check(r.kind == Kind::Antivirus || r.kind == Kind::Sandbox, "Engine rejected");
    Check((r.kind == Kind::Antivirus && (r.operation == Operation::Scan || (r.operation>=Operation::QuarantineList&&r.operation<=Operation::QuarantineAudit))) ||
          (r.kind == Kind::Sandbox && (r.operation == Operation::Detonate || r.operation == Operation::DesktopBundle || r.operation == Operation::Checkpoint || r.operation == Operation::Resume)), "Engine operation rejected");
    Check(r.pathChars > 0 && r.pathChars <= 32767 && r.milliseconds >= 100 && r.milliseconds <= 3600000,
          "Engine request limit exceeded");
    Check(r.kind != Kind::Sandbox || r.milliseconds <= 60000, "Sandbox time limit exceeded");
}
void Transfer(HANDLE pipe, bool write, void* buffer, DWORD bytes, ULONGLONG deadline,
              HANDLE stop, const std::atomic<bool>* cancel) {
    auto* at = static_cast<uint8_t*>(buffer);
    while (bytes) {
        if ((cancel && cancel->load()) || (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0))
            WinError("Engine operation interrupted", ERROR_OPERATION_ABORTED);
        if (GetTickCount64() >= deadline) WinError("Engine operation timed out", ERROR_TIMEOUT);
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) WinError("Create pipe event");
        OVERLAPPED io{}; io.hEvent = event.value;
        DWORD transferred = 0;
        const auto ok = write ? WriteFile(pipe, at, bytes, &transferred, &io) : ReadFile(pipe, at, bytes, &transferred, &io);
        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING) WinError(write ? "Write engine pipe" : "Read engine pipe");
            for (;;) {
                const auto wait = WaitForSingleObject(event.value, 25);
                if (wait == WAIT_OBJECT_0) break;
                if (wait != WAIT_TIMEOUT || GetTickCount64() >= deadline || (cancel && cancel->load()) ||
                    (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)) {
                    CancelIoEx(pipe, &io);
                    GetOverlappedResult(pipe, &io, &transferred, TRUE);
                    WinError("Engine operation interrupted", GetTickCount64() >= deadline ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED);
                }
            }
            if (!GetOverlappedResult(pipe, &io, &transferred, FALSE)) WinError("Complete engine pipe transfer");
        }
        Check(transferred > 0 && transferred <= bytes, "Engine pipe closed");
        at += transferred; bytes -= transferred;
    }
}
void Connect(HANDLE pipe, ULONGLONG deadline, HANDLE stop) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.value) WinError("Create connection event");
    OVERLAPPED io{}; io.hEvent = event.value;
    if (ConnectNamedPipe(pipe, &io)) return;
    const auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) return;
    if (error != ERROR_IO_PENDING) WinError("Accept engine connection", error);
    for (;;) {
        if (WaitForSingleObject(event.value, 25) == WAIT_OBJECT_0) {
            DWORD count = 0;
            if (!GetOverlappedResult(pipe, &io, &count, FALSE)) WinError("Complete engine connection");
            return;
        }
        if (GetTickCount64() >= deadline || (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)) {
            CancelIoEx(pipe, &io);
            DWORD ignored = 0; GetOverlappedResult(pipe, &io, &ignored, TRUE);
            WinError("Engine connection interrupted", ERROR_OPERATION_ABORTED);
        }
    }
}
void Send(HANDLE pipe, FrameType type, const Json& value, ULONGLONG deadline) {
    auto text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    Check(!text.empty() && text.size() <= MaxFrame, "Engine response limit exceeded");
    Frame header{Magic, Version, type, uint32_t(text.size())};
    Transfer(pipe, true, &header, sizeof(header), deadline);
    Transfer(pipe, true, text.data(), DWORD(text.size()), deadline);
}
void FinishConnection(HANDLE pipe, HANDLE stop) noexcept {
    // DisconnectNamedPipe discards unread bytes. Let the caller consume the
    // terminal frame and close its handle first. Never block service shutdown
    // in FlushFileBuffers, or let an unresponsive caller hold this slot forever.
    try {
        unsigned char ignored = 0;
        Transfer(pipe, false, &ignored, 1, GetTickCount64() + 2000, stop);
    } catch (...) {}
}
} // namespace av::services
