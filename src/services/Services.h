#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <stdexcept>

namespace av::services {
using Json = nlohmann::json;
namespace fs = std::filesystem;
enum class Kind : uint32_t { Antivirus = 1, Sandbox = 2 };
enum class Operation : uint32_t { Scan = 1, Detonate = 2, DesktopBundle = 3 };
enum class FrameType : uint32_t { Progress = 1, Result = 2, Error = 3 };
inline constexpr uint32_t Magic = 0x53474541, Version = 1, MaxFrame = 2u << 20;
// Read attributes are required by Win32's pipe open/identity operations. Do not
// include FILE_GENERIC_WRITE: it also grants FILE_CREATE_PIPE_INSTANCE.
inline constexpr DWORD PipeClientAccess = FILE_GENERIC_READ | FILE_WRITE_DATA;
inline constexpr const wchar_t* PackageFiles[] = {L"guardian-engine-service.exe", L"guardian-scan-worker.exe", L"guardian-whp-memory.exe",
    L"guardian-service-sample.exe", L"signatures.txt"
#ifdef GUARDIAN_SERVICE_DESKTOP
    , L"guardian-desktop.exe", L"guardian-audio-host.exe", L"guardian-kernel-audio.gk"
#endif
};
inline constexpr const wchar_t* RuntimeFiles[] = {L"concrt140.dll", L"msvcp140.dll", L"msvcp140_1.dll", L"msvcp140_2.dll",
    L"msvcp140_atomic_wait.dll", L"msvcp140_codecvt_ids.dll", L"vccorlib140.dll", L"vcruntime140.dll", L"vcruntime140_1.dll", L"vcruntime140_threads.dll"};
struct WindowsError : std::runtime_error {
    DWORD code;
    WindowsError(const std::string& message, DWORD value) : std::runtime_error(message), code(value) {}
};
struct Request {
    uint32_t magic = Magic, version = Version;
    Kind kind = Kind::Antivirus;
    Operation operation = Operation::Scan;
    uint32_t pathChars = 0, milliseconds = 3600000;
};
struct Frame {
    uint32_t magic = Magic, version = Version;
    FrameType type = FrameType::Error;
    uint32_t bytes = 0;
};
static_assert(sizeof(Request) == 24 && sizeof(Frame) == 16);
struct Status {
    bool installed = false;
    DWORD state = SERVICE_STOPPED, process = 0, error = ERROR_SUCCESS;
    bool Running() const { return installed && !error && state == SERVICE_RUNNING; }
};
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE release() { const auto h = value; value = nullptr; return h; }
};
const wchar_t* Name(Kind);
const wchar_t* Label(Kind);
std::wstring PipeName(Kind);
std::string Text(const std::wstring&);
std::wstring Wide(const std::string&);
std::wstring ErrorText(DWORD);
std::wstring UserSid(HANDLE token = nullptr);
fs::path Executable();
void Check(bool, const char*);
[[noreturn]] void WinError(const char*, DWORD error = GetLastError());
void Validate(const Request&);
void Transfer(HANDLE, bool write, void*, DWORD, ULONGLONG deadline,
              HANDLE stop = nullptr, const std::atomic<bool>* cancel = nullptr);
void Connect(HANDLE, ULONGLONG deadline, HANDLE stop = nullptr);
void Send(HANDLE, FrameType, const Json&, ULONGLONG deadline);
void FinishConnection(HANDLE, HANDLE stop = nullptr) noexcept;
Json Call(Kind, Operation, const fs::path&, const std::function<void(const Json&)>& = {},
          const std::atomic<bool>* cancel = nullptr, uint32_t milliseconds = 3600000);
Status Query(Kind);
void SetRunning(Kind, bool, DWORD timeout = 15000);
DWORD Setup(HWND owner = nullptr, bool uninstall = false);
int Install(const std::wstring& ownerSid, bool uninstall);
int Serve(Kind, const std::wstring& ownerSid);
int Worker(const std::wstring& pipe, DWORD serverPid);
} // namespace av::services
