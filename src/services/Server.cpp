#include "Services.h"
#include <bcrypt.h>
#include <sddl.h>
#include <userenv.h>
#include <thread>
#include <vector>
#include <cstring>

namespace av::services {
namespace {
SERVICE_STATUS_HANDLE statusHandle = nullptr;
SERVICE_STATUS serviceStatus{};
HANDLE stopping = nullptr;
Kind serviceKind = Kind::Antivirus;
std::wstring serviceOwner;

void Report(DWORD state, DWORD error = ERROR_SUCCESS) {
    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState = state;
    serviceStatus.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    serviceStatus.dwWin32ExitCode = error;
    serviceStatus.dwWaitHint = state == SERVICE_STOP_PENDING || state == SERVICE_START_PENDING ? 10000 : 0;
    serviceStatus.dwCheckPoint = state == SERVICE_STOP_PENDING || state == SERVICE_START_PENDING ? 1 : 0;
    SetServiceStatus(statusHandle, &serviceStatus);
}
DWORD WINAPI Control(DWORD code, DWORD, void*, void*) {
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
        // The service loop alone publishes final states, avoiding a late handler
        // overwriting SERVICE_STOPPED while request cleanup is finishing.
        SetEvent(stopping);
        return ERROR_SUCCESS;
    }
    return code == SERVICE_CONTROL_INTERROGATE ? ERROR_SUCCESS : ERROR_CALL_NOT_IMPLEMENTED;
}
HANDLE Pipe(const std::wstring& name) {
    // Read metadata plus data-only writes omit FILE_CREATE_PIPE_INSTANCE.
    // The mandatory label permits a normal desktop user, never a low-IL target.
    const auto sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x12008b;;;" + serviceOwner + L")S:(ML;;NW;;;ME)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) WinError("Create engine pipe security");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), sd, FALSE};
    const auto h = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 1000, &attributes);
    const auto error = GetLastError(); LocalFree(sd);
    if (h == INVALID_HANDLE_VALUE) WinError("Create protected engine pipe", error);
    return h;
}
std::wstring Nonce() {
    unsigned char bytes[16]{};
    Check(BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0, "Generate worker identity");
    std::wstring out;
    for (const auto b : bytes) { out += L"0123456789abcdef"[b >> 4]; out += L"0123456789abcdef"[b & 15]; }
    return out;
}
HANDLE Caller(HANDLE pipe) {
    if (!ImpersonateNamedPipeClient(pipe)) WinError("Authenticate engine caller");
    Handle token;
    const auto opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &token.value);
    const auto error = GetLastError();
    if (!RevertToSelf()) { TerminateProcess(GetCurrentProcess(), ERROR_CANNOT_IMPERSONATE); }
    if (!opened) WinError("Read engine caller token", error);
    Check(UserSid(token.value) == serviceOwner, "Engine caller is not the installation owner");
    DWORD session = 0, bytes = 0;
    if (!GetTokenInformation(token.value, TokenSessionId, &session, sizeof(session), &bytes)) WinError("Read caller session");
    Check(session != 0, "A user session is required");
    Handle primary;
    if (!DuplicateTokenEx(token.value, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primary.value)) WinError("Duplicate caller token");
    Handle restricted;
    if (!CreateRestrictedToken(primary.value, DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 0, nullptr, 0, nullptr, 0, nullptr, &restricted.value)) WinError("Restrict engine worker");
    PSID medium = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-16-8192", &medium)) WinError("Create worker integrity label");
    TOKEN_MANDATORY_LABEL label{}; label.Label.Sid = medium; label.Label.Attributes = SE_GROUP_INTEGRITY;
    const auto set = SetTokenInformation(restricted.value, TokenIntegrityLevel, &label, DWORD(sizeof(label) + GetLengthSid(medium)));
    const auto setError = GetLastError(); LocalFree(medium);
    if (!set) WinError("Lower worker integrity", setError);
    return restricted.release();
}
std::wstring Environment(HANDLE token) {
    void* block = nullptr;
    if (!CreateEnvironmentBlock(&block, token, FALSE)) WinError("Create user environment");
    std::wstring out;
    for (const auto* at = static_cast<const wchar_t*>(block); *at; at += wcslen(at) + 1) {
        const std::wstring line(at);
        const auto end = line.find(L'=');
        const auto key = line.substr(0, end);
        for (const auto* allowed : {L"SystemRoot", L"SystemDrive", L"USERPROFILE", L"LOCALAPPDATA", L"APPDATA", L"TEMP", L"TMP"})
            if (!_wcsicmp(key.c_str(), allowed)) { out += line; out += L'\0'; break; }
    }
    DestroyEnvironmentBlock(block); out += L'\0';
    Check(out.size() > 2, "Worker environment unavailable");
    return out;
}
struct Process {
    Handle job, process, thread;
    ~Process() {
        if (job.value) TerminateJobObject(job.value, ERROR_OPERATION_ABORTED);
        if (process.value) WaitForSingleObject(process.value, 5000);
    }
    void Start(HANDLE token, const std::wstring& pipe) {
        const auto exe = Executable();
        auto command = L"\"" + exe.wstring() + L"\" --worker \"" + pipe + L"\" " + std::to_wstring(GetCurrentProcessId());
        job.value = CreateJobObjectW(nullptr, nullptr);
        if (!job.value) WinError("Create engine worker job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
            JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
        policy.BasicLimitInformation.ActiveProcessLimit = serviceKind == Kind::Antivirus ? 3 : 5;
        policy.ProcessMemoryLimit = 512ull << 20;
        policy.JobMemoryLimit = serviceKind == Kind::Antivirus ? (768ull << 20) : (1024ull << 20);
        if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &policy, sizeof(policy))) WinError("Constrain engine worker");
        auto environment = Environment(token);
        STARTUPINFOW start{}; start.cb = sizeof(start);
        wchar_t desktop[] = L"winsta0\\default"; start.lpDesktop = desktop;
        PROCESS_INFORMATION info{};
        if (!CreateProcessAsUserW(token, exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(), exe.parent_path().c_str(), &start, &info)) WinError("Start user engine worker");
        process.value = info.hProcess; thread.value = info.hThread;
        if (!AssignProcessToJobObject(job.value, process.value)) {
            const auto error = GetLastError(); TerminateProcess(process.value, ERROR_OPERATION_ABORTED);
            WinError("Assign engine worker job", error);
        }
        if (ResumeThread(thread.value) == DWORD(-1)) WinError("Resume engine worker");
    }
};
struct DisconnectWatch {
    Handle finish{CreateEventW(nullptr, TRUE, FALSE, nullptr)}, cancelled{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread thread;
    explicit DisconnectWatch(HANDLE pipe) {
        Check(finish.value && cancelled.value, "Create cancellation boundary");
        thread = std::thread([this, pipe] {
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event.value) { SetEvent(cancelled.value); return; }
            OVERLAPPED io{}; io.hEvent = event.value;
            unsigned char signal = 0; DWORD count = 0;
            const auto read = ReadFile(pipe, &signal, 1, &count, &io);
            if (!read && GetLastError() == ERROR_IO_PENDING) {
                const HANDLE handles[] = {event.value, stopping, finish.value};
                WaitForMultipleObjects(3, handles, FALSE, INFINITE);
                CancelIoEx(pipe, &io);
                GetOverlappedResult(pipe, &io, &count, TRUE);
            }
            SetEvent(cancelled.value);
        });
    }
    ~DisconnectWatch() { SetEvent(finish.value); if (thread.joinable()) thread.join(); }
};
void Execute(HANDLE client) {
    Request request;
    Transfer(client, false, &request, sizeof(request), GetTickCount64() + 5000, stopping);
    Validate(request);
    Check(request.kind == serviceKind, "Request sent to the wrong engine");
    std::wstring path(request.pathChars, L'\0');
    Transfer(client, false, path.data(), DWORD(path.size() * sizeof(wchar_t)), GetTickCount64() + 5000, stopping);
    Check(path.find(L'\0') == path.npos && fs::path(path).is_absolute(), "Absolute input path required");
    // No caller path is opened by LocalSystem. Payloads and configuration are
    // read exclusively by the restricted process in the caller's session.
    Handle token(Caller(client));
    const auto name = PipeName(serviceKind) + L".worker." + Nonce();
    Handle pipe(Pipe(name));
    Process worker; worker.Start(token.value, name);
    const auto connecting = GetTickCount64() + 8000;
    for (;;) {
        Connect(pipe.value, connecting, stopping);
        ULONG pid = 0;
        if (!GetNamedPipeClientProcessId(pipe.value, &pid)) WinError("Identify engine worker");
        if (pid == GetProcessId(worker.process.value)) break;
        DisconnectNamedPipe(pipe.value);
        Check(GetTickCount64() < connecting, "Worker authentication timed out");
    }
    DisconnectWatch watch(client);
    const auto deadline = GetTickCount64() + request.milliseconds;
    Transfer(pipe.value, true, &request, sizeof(request), deadline, watch.cancelled.value);
    Transfer(pipe.value, true, path.data(), DWORD(path.size() * sizeof(wchar_t)), deadline, watch.cancelled.value);
    for (;;) {
        Frame frame;
        Transfer(pipe.value, false, &frame, sizeof(frame), deadline, watch.cancelled.value);
        Check(frame.magic == Magic && frame.version == Version && frame.bytes && frame.bytes <= MaxFrame &&
              (frame.type == FrameType::Progress || frame.type == FrameType::Result || frame.type == FrameType::Error), "Worker frame rejected");
        std::vector<uint8_t> data(frame.bytes);
        Transfer(pipe.value, false, data.data(), frame.bytes, deadline, watch.cancelled.value);
        Transfer(client, true, &frame, sizeof(frame), deadline, watch.cancelled.value);
        Transfer(client, true, data.data(), frame.bytes, deadline, watch.cancelled.value);
        if (frame.type != FrameType::Progress) break;
    }
}
void WINAPI Main(DWORD, wchar_t**) {
    statusHandle = RegisterServiceCtrlHandlerExW(Name(serviceKind), Control, nullptr);
    if (!statusHandle) return;
    Report(SERVICE_START_PENDING);
    const auto stop = stopping;
    DWORD result = ERROR_SUCCESS;
    try {
        Check(stop != nullptr, "Create service stop event");
        Handle pipe(Pipe(PipeName(serviceKind)));
        Report(SERVICE_RUNNING);
        while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
            try {
                Connect(pipe.value, GetTickCount64() + 24ull * 60 * 60 * 1000, stop);
                Execute(pipe.value);
            } catch (const std::exception& e) {
                if (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0)
                    try { Send(pipe.value, FrameType::Error, {{"error", e.what()}}, GetTickCount64() + 1000); } catch (...) {}
            }
            FinishConnection(pipe.value, stop);
            DisconnectNamedPipe(pipe.value);
        }
        Report(SERVICE_STOP_PENDING);
    } catch (...) { result = ERROR_SERVICE_SPECIFIC_ERROR; }
    Report(SERVICE_STOPPED, result);
}
}
int Serve(Kind kind, const std::wstring& owner) {
    serviceKind = kind; serviceOwner = owner;
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stop.value) WinError("Create service stop event");
    stopping = stop.value;
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t*>(Name(kind)), Main}, {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) WinError("Connect to Windows service manager");
    return 0;
}
} // namespace av::services
