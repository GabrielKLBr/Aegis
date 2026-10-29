#include "Services.h"
#include <shellapi.h>
#include <vector>

namespace av::services {
namespace {
struct ScHandle {
    SC_HANDLE value = nullptr;
    ~ScHandle() { if (value) CloseServiceHandle(value); }
};
}
Status Query(Kind kind) {
    Status out;
    ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!scm.value) { out.error = GetLastError(); return out; }
    ScHandle service{OpenServiceW(scm.value, Name(kind), SERVICE_QUERY_STATUS)};
    if (!service.value) {
        const auto error = GetLastError();
        out.error = error == ERROR_SERVICE_DOES_NOT_EXIST ? ERROR_SUCCESS : error;
        return out;
    }
    out.installed = true;
    SERVICE_STATUS_PROCESS state{}; DWORD bytes = 0;
    if (!QueryServiceStatusEx(service.value, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&state), sizeof(state), &bytes))
        out.error = GetLastError();
    else { out.state = state.dwCurrentState; out.process = state.dwProcessId; }
    return out;
}
void SetRunning(Kind kind, bool running, DWORD timeout) {
    ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!scm.value) WinError("Open Windows service manager");
    ScHandle service{OpenServiceW(scm.value, Name(kind), SERVICE_QUERY_STATUS | (running ? SERVICE_START : SERVICE_STOP))};
    if (!service.value) WinError("Open Aegis service");
    SERVICE_STATUS_PROCESS state{}; DWORD bytes = 0;
    const auto deadline = GetTickCount64() + timeout;
    bool requested = false;
    for (;;) {
        if (!QueryServiceStatusEx(service.value, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&state), sizeof(state), &bytes)) WinError("Query Aegis service");
        const DWORD desired = running ? SERVICE_RUNNING : SERVICE_STOPPED;
        if (state.dwCurrentState == desired) return;
        if (!requested && state.dwCurrentState != SERVICE_START_PENDING && state.dwCurrentState != SERVICE_STOP_PENDING) {
            if (running) {
                if (!StartServiceW(service.value, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) WinError("Start Aegis service");
            } else {
                SERVICE_STATUS stopped{};
                if (!ControlService(service.value, SERVICE_CONTROL_STOP, &stopped) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) WinError("Stop Aegis service");
            }
            requested = true;
        }
        if (GetTickCount64() >= deadline) WinError("Aegis service transition timed out", ERROR_TIMEOUT);
        Sleep(40);
    }
}
DWORD Setup(HWND owner, bool uninstall) {
    const auto exe = Executable().parent_path() / L"guardian-engine-service.exe";
    Check(fs::is_regular_file(exe), "Service installer missing beside the app");
    if (!uninstall) for (const auto* name : PackageFiles)
        Check(fs::is_regular_file(exe.parent_path() / name), "O pacote dos serviços está incompleto; use a pasta preparada para instalação.");
    const auto parameters = std::wstring(uninstall ? L"--uninstall --owner " : L"--install --owner ") + UserSid();
    SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner; info.lpVerb = L"runas"; info.lpFile = exe.c_str();
    info.lpParameters = parameters.c_str(); info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) WinError("Authorize Aegis service installation");
    Handle process(info.hProcess);
    if (WaitForSingleObject(process.value, INFINITE) != WAIT_OBJECT_0) WinError("Wait for service installation");
    DWORD code = 0;
    if (!GetExitCodeProcess(process.value, &code)) WinError("Read installation result");
    return code;
}
Json Call(Kind kind, Operation operation, const fs::path& input,
          const std::function<void(const Json&)>& progress, const std::atomic<bool>* cancel, uint32_t milliseconds) {
    const auto status = Query(kind);
    if (status.error) WinError("Query engine service", status.error);
    Check(status.Running(), kind == Kind::Antivirus ? "O serviço do antivírus está desligado. Ligue-o em Configurações." : "O serviço do MiniNT está desligado. Ligue-o em Configurações.");
    const auto path = fs::absolute(input).lexically_normal().wstring();
    Check(path.find(L'\0') == path.npos, "Input path contains NUL");
    Request request{Magic, Version, kind, operation, uint32_t(path.size()), milliseconds};
    Validate(request);
    const auto name = PipeName(kind);
    Handle pipe;
    const auto opening = GetTickCount64() + 2500;
    for (;;) {
        pipe.value = CreateFileW(name.c_str(), PipeClientAccess,
            0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr);
        if (pipe.value != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) WinError("Connect to Aegis engine", error);
        Check(!(cancel && cancel->load()), "Análise cancelada");
        if (GetTickCount64() >= opening) WinError("Engine busy or unavailable", ERROR_BUSY);
        Sleep(25);
    }
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe.value, &pid)) WinError("Identify engine server");
    const auto current = Query(kind);
    Check(current.Running() && current.process == pid && status.process == pid, "Engine server identity rejected");
    const auto deadline = GetTickCount64() + milliseconds + 5000ull;
    Transfer(pipe.value, true, &request, sizeof(request), deadline, nullptr, cancel);
    Transfer(pipe.value, true, const_cast<wchar_t*>(path.data()), DWORD(path.size() * sizeof(wchar_t)), deadline, nullptr, cancel);
    for (;;) {
        Frame frame;
        Transfer(pipe.value, false, &frame, sizeof(frame), deadline, nullptr, cancel);
        Check(frame.magic == Magic && frame.version == Version && frame.bytes && frame.bytes <= MaxFrame &&
              (frame.type == FrameType::Progress || frame.type == FrameType::Result || frame.type == FrameType::Error), "Engine response rejected");
        std::string text(frame.bytes, '\0');
        Transfer(pipe.value, false, text.data(), frame.bytes, deadline, nullptr, cancel);
        const auto value = Json::parse(text);
        Check(value.is_object(), "Engine response must be an object");
        if (frame.type == FrameType::Error) throw std::runtime_error(value.value("error", "Engine failed"));
        if (frame.type == FrameType::Result) return value;
        if (progress) progress(value);
    }
}
} // namespace av::services
