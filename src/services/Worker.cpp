#include "Services.h"
#include "EngineFacade.h"
#include "microvm/runtime/Unified.h"
#include <fstream>
#include <vector>

namespace av::services {
namespace {
Json Identity() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) WinError("Inspect worker token");
    TOKEN_ELEVATION elevation{}; DWORD bytes = 0, session = 0;
    if (!GetTokenInformation(token.value, TokenElevation, &elevation, sizeof(elevation), &bytes) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &session)) WinError("Inspect worker identity");
    DWORD filtered = 0;
    if (!GetTokenInformation(token.value, TokenHasRestrictions, &filtered, sizeof(filtered), &bytes)) WinError("Inspect worker restrictions");
    GetTokenInformation(token.value, TokenPrivileges, nullptr, 0, &bytes);
    std::vector<uint8_t> data(bytes);
    if (!GetTokenInformation(token.value, TokenPrivileges, data.data(), bytes, &bytes)) WinError("Inspect worker privileges");
    LUID notify{};
    if (!LookupPrivilegeValueW(nullptr, L"SeChangeNotifyPrivilege", &notify)) WinError("Inspect traversal privilege");
    const auto* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(data.data());
    bool safe = true;
    for (DWORD i = 0; i < privileges->PrivilegeCount; ++i) {
        const auto& luid = privileges->Privileges[i].Luid;
        if (luid.LowPart != notify.LowPart || luid.HighPart != notify.HighPart) safe = false;
    }
    BYTE admin[SECURITY_MAX_SID_SIZE]{}; DWORD adminBytes = sizeof(admin); BOOL member = TRUE;
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin, &adminBytes) || !CheckTokenMembership(nullptr, admin, &member)) WinError("Inspect administrator membership");
    Check(!elevation.TokenIsElevated && session && filtered && safe && !member && UserSid(token.value) != L"S-1-5-18", "Worker must have restricted privileges in a non-elevated desktop session");
    return {{"type", "worker"}, {"pid", GetCurrentProcessId()}, {"session", session},
            {"elevated", elevation.TokenIsElevated != 0}, {"restricted", filtered != 0},
            {"privilegesRestricted", safe}, {"administrator", member != FALSE},
            {"restrictingSids", IsTokenRestricted(token.value) != FALSE}};
}
Json Snapshot(av::Engine& engine, size_t& sent) {
    auto& state = engine.State();
    Json result = {{"type", "scan"}, {"filesScanned", state.filesScanned.load()}, {"threatsFound", state.threatsFound.load()},
        {"suspicious", state.suspicious.load()}, {"inconclusive", state.inconclusive.load()}, {"unknown", state.unknown.load()},
        {"cancelled", state.cancel.load()}, {"detections", Json::array()}};
    std::lock_guard lock(state.mtx);
    result["currentFile"] = Text(state.currentFile);
    result["error"] = Text(state.error);
    for (; sent < state.detections.size(); ++sent) {
        const auto& detection = state.detections[sent];
        result["detections"].push_back({{"file", Text(detection.file)}, {"threat", Text(detection.threat)}, {"reason", Text(detection.reason)}});
    }
    return result;
}
Json Scan(HANDLE pipe, const fs::path& path, ULONGLONG deadline) {
    av::Engine engine;
    engine.LoadSignatures((Executable().parent_path() / L"signatures.txt").wstring());
    size_t sent = 0;
    std::atomic<bool> failed{false};
    engine.StartScan(path.wstring(), [&] {
        try { Send(pipe, FrameType::Progress, Snapshot(engine, sent), deadline); }
        catch (...) { failed = true; engine.Cancel(); }
    }, {});
    engine.Join();
    Check(!failed, "Scan client disconnected");
    auto result = Snapshot(engine, sent);
    result["complete"] = !engine.State().cancel && !engine.State().inconclusive && engine.State().error.empty();
    result["classification"] = engine.State().threatsFound ? "detected" : engine.State().suspicious ? "suspicious" :
        result["complete"].get<bool>() ? "unknown" : "inconclusive";
    return result;
}
struct Payload {
    fs::path folder;
    explicit Payload(const fs::path& path) {
        const auto root = scan::DefaultState().parent_path() / L"service-jobs";
        scan::PrivateState(root);
        folder = root / scan::Token();
        scan::PrivateState(folder);
        const auto bytes = scan::Read(path, 32ull << 20);
        scan::Write(folder / L"payload.exe", bytes);
    }
    ~Payload() {
        std::error_code error;
        fs::remove(folder / L"payload.exe", error); fs::remove(folder, error);
    }
};
Json Detonate(HANDLE pipe,const fs::path& path,ULONGLONG deadline,Operation operation) {
#ifdef GUARDIAN_SERVICE_UNIFIED
    fs::path input=path,restore;if(operation==Operation::Resume){scan::ReadJson(path,32768);restore=path;input=path.parent_path()/L"payload.bin";}
    Payload payload(input);auto config=scan::UserConfig();const auto state=config.state.empty()?scan::DefaultState():config.state;scan::PrivateState(state);fs::create_directory(state/"sessions");scan::OrdinaryDirectory(state/"sessions");
    guardian::telemetry::Policy policy;policy.eventLimit=guardian::telemetry::MaxEvents;
    const auto remaining=deadline>GetTickCount64()?deadline-GetTickCount64():100;
    const auto budget=operation==Operation::Checkpoint?ULONGLONG(5000):ULONGLONG(30000);
    auto result=microvm::runtime::Run(payload.folder/L"payload.exe",config.sandboxRuntime,state/"sessions"/scan::Token(),policy,DWORD((std::min)(remaining>1000?remaining-1000:100,budget)),operation==Operation::Checkpoint?12:0,restore,[&](const Json& value){Send(pipe,FrameType::Progress,value,deadline);});return result;
#else
    (void)pipe;(void)path;(void)deadline;(void)operation;throw std::runtime_error("The owned unified MiniNT runtime is not included in this build");
#endif
}
#ifdef GUARDIAN_SERVICE_DESKTOP
std::wstring Quote(const fs::path& path) {
    const auto text = path.wstring();
    Check(text.find(L'"') == text.npos && !text.ends_with(L'\\'), "Runtime path rejected");
    return L"\"" + text + L"\"";
}
#endif
Json Desktop(const fs::path& bundle, ULONGLONG deadline) {
#ifdef GUARDIAN_SERVICE_DESKTOP
    scan::OrdinaryDirectory(bundle);
    for (const auto* name : {L"runtime.gpv", L"manifest.json", L"binding.gnt"})
        Check(fs::is_regular_file(fs::symlink_status(bundle / name)), "Prepared MiniNT bundle required");
    const auto exe = Executable().parent_path() / L"guardian-desktop.exe";
    const auto kernel = Executable().parent_path() / L"guardian-kernel-audio.gk";
    const auto root = scan::DefaultState().parent_path() / L"minint-sessions";
    scan::PrivateState(root);
    const auto output = root / scan::Token(); // guardian-desktop requires a new directory.
    auto command = Quote(exe) + L" " + Quote(kernel) + L" " + Quote(bundle / L"runtime.gpv") + L" " +
        Quote(bundle / L"manifest.json") + L" " + Quote(bundle / L"binding.gnt") + L" " + Quote(output) + L" --audio --seconds 30";
    STARTUPINFOW start{}; start.cb = sizeof(start);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, exe.parent_path().c_str(), &start, &info)) WinError("Start prepared MiniNT session");
    Handle process(info.hProcess), thread(info.hThread);
    for (;;) {
        const auto wait = WaitForSingleObject(process.value, 25);
        if (wait == WAIT_OBJECT_0) break;
        Check(wait == WAIT_TIMEOUT && GetTickCount64() < deadline, "Prepared MiniNT session timed out");
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process.value, &code)) WinError("Read MiniNT session result");
    return {{"type", "sandbox"}, {"backend", "MiniNT-14.10G"}, {"complete", code == 0}, {"exitCode", code},
            {"classification", "inconclusive"}, {"output", Text(output.wstring())},
            {"error", code == 0 ? "" : "MiniNT rejected the bundle or could not finish; inspect session reports"}};
#else
    (void)bundle; (void)deadline;
    throw std::runtime_error("Esta build não inclui o runtime gráfico. Compile com GUARDIAN_BUILD_EXTERNAL_PE=ON e o runtime de áudio habilitado.");
#endif
}
}
int Worker(const std::wstring& name, DWORD server) {
    Check(name.starts_with(L"\\\\.\\pipe\\Aegis.Engine.v1.") && name.find(L".worker.") != name.npos && name.size() < 200, "Worker pipe rejected");
    Handle pipe(CreateFileW(name.c_str(), PipeClientAccess, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (pipe.value == INVALID_HANDLE_VALUE) WinError("Connect worker pipe");
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe.value, &pid)) WinError("Identify worker supervisor");
    Check(server == pid, "Worker supervisor mismatch");
    try {
        Request request;
        Transfer(pipe.value, false, &request, sizeof(request), GetTickCount64() + 5000);
        Validate(request);
        std::wstring path(request.pathChars, L'\0');
        Transfer(pipe.value, false, path.data(), DWORD(path.size() * sizeof(wchar_t)), GetTickCount64() + 5000);
        Check(path.find(L'\0') == path.npos && fs::path(path).is_absolute(), "Worker input path rejected");
        const auto deadline = GetTickCount64() + request.milliseconds;
        auto identity = Identity(); identity["engine"] = request.kind == Kind::Antivirus ? "antivirus" : "sandbox";
        Send(pipe.value, FrameType::Progress, identity, deadline);
        const auto result = request.operation == Operation::Scan ? Scan(pipe.value, path, deadline) :
            request.operation == Operation::DesktopBundle ? Desktop(path, deadline) : Detonate(pipe.value,path,deadline,request.operation);
        Send(pipe.value, FrameType::Result, result, deadline);
        return 0;
    } catch (const std::exception& error) {
        try { Send(pipe.value, FrameType::Error, {{"error", error.what()}}, GetTickCount64() + 1000); } catch (...) {}
        return 3;
    }
}
} // namespace av::services
