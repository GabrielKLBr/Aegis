#include "common/ProcessPolicy.h"
#include "services/Services.h"
#include "scanner/Scanner.h"
#include "../src/microvm/gui/NativeGuiBroker.h"
#include <sddl.h>
#include <fstream>
#include <iostream>
#include <thread>

using namespace av::services;
using aegis::security::ProcessRole;
namespace {
unsigned checks = 0;
void Expect(bool ok, const char* message) { ++checks; Check(ok, message); }
struct Descriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    ~Descriptor() { if (value) LocalFree(value); }
};
struct Sid {
    PSID value = nullptr;
    explicit Sid(const wchar_t* text) {
        Check(ConvertStringSidToSidW(text, &value) != FALSE, "Probe SID");
    }
    ~Sid() { LocalFree(value); }
};
std::wstring Quote(const fs::path& path) {
    Check(path.wstring().find(L'"') == std::wstring::npos, "Test path quoting");
    return L"\"" + path.wstring() + L"\"";
}
// This executable is an inert negative-permission probe. It never executes a
// guest file, modifies user files, opens a browser or changes security settings.
int Probe(int argc, wchar_t** argv) {
    Check(aegis::security::SealLeafPolicy(), "Seal probe child policy");
    Check(argc == 9, "Probe arguments");
    Json result;
    result["launchPolicy"] = aegis::security::HasProcessPolicy(GetCurrentProcess(), ProcessRole::Leaf);
    const auto sentinel = fs::path(argv[2]);
    Handle readable(CreateFileW(sentinel.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    result["ordinaryUserFileReadable"] = readable.value != INVALID_HANDLE_VALUE;
    Handle write(CreateFileW(sentinel.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    result["mediumFileWriteDenied"] = write.value == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED;
    const auto parent = DWORD(std::stoul(argv[3]));
    Handle memory(OpenProcess(PROCESS_VM_WRITE | PROCESS_DUP_HANDLE, FALSE, parent));
    result["parentMemoryWriteDenied"] = memory.value == nullptr && GetLastError() == ERROR_ACCESS_DENIED;
    const auto hwnd = reinterpret_cast<HWND>(std::stoull(argv[4]));
    const auto dc = GetDC(hwnd);
    result["foreignWindowDcDenied"] = dc == nullptr;
    if (dc) ReleaseDC(hwnd, dc);
    const auto allowed = reinterpret_cast<HANDLE>(std::stoull(argv[5]));
    const auto excluded = reinterpret_cast<HANDLE>(std::stoull(argv[6]));
    result["listedHandleUsable"] = SetEvent(allowed) != FALSE;
    result["unlistedHandleDenied"] = !SetEvent(excluded) && GetLastError() == ERROR_INVALID_HANDLE;
    Handle pipe(CreateFileW(argv[7], PipeClientAccess, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    result["mediumPipeWriteDenied"] = pipe.value == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED;
    const auto child = Quote(Executable()) + L" --must-not-run";
    std::wstring command = child;
    STARTUPINFOW start{}; start.cb = sizeof(start);
    PROCESS_INFORMATION info{};
    const auto created = CreateProcessW(Executable().c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &start, &info);
    const auto childError = GetLastError();
    if (created) { TerminateProcess(info.hProcess, 3); CloseHandle(info.hProcess); CloseHandle(info.hThread); }
    result["childCreationDenied"] = !created && childError == ERROR_CHILD_PROCESS_BLOCKED;
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    result["desktopJobPolicy"] = QueryInformationJobObject(nullptr, JobObjectBasicUIRestrictions,
        &ui, sizeof(ui), nullptr) && ui.UIRestrictionsClass == aegis::security::UiRestrictions;
    const auto text = result.dump(); DWORD bytes = 0;
    Check(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), DWORD(text.size()), &bytes, nullptr) &&
          bytes == text.size(), "Probe report");
    return 0;
}
Json LaunchProbe(const fs::path& folder) {
    fs::create_directories(folder);
    const auto sentinel = folder / L"host-sentinel.txt";
    Check(!fs::exists(sentinel), "Use a new isolation test folder");
    std::ofstream(sentinel) << "OWNED_INERT_HOST_SENTINEL\n";
    const auto originalSentinel = av::scan::Read(sentinel, 128);
    Descriptor sd;
    const auto acl = L"D:P(A;;GA;;;SY)(A;;0x12008b;;;" + UserSid() + L")S:(ML;;NW;;;ME)";
    Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1,
        &sd.value, nullptr) != FALSE, "Probe pipe permissions");
    SECURITY_ATTRIBUTES pipeSecurity{sizeof(pipeSecurity), sd.value, FALSE};
    const auto name = L"\\\\.\\pipe\\Aegis.Isolation19A." + std::to_wstring(GetCurrentProcessId());
    Handle boundary(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, &pipeSecurity));
    Check(boundary.value != INVALID_HANDLE_VALUE, "Probe boundary pipe");
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    Handle read, output, allowed(CreateEventW(&inherit, TRUE, FALSE, nullptr)),
        excluded(CreateEventW(&inherit, TRUE, FALSE, nullptr));
    Check(CreatePipe(&read.value, &output.value, &inherit, 4096) &&
          SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0), "Probe output pipe");
    Handle original, restricted;
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
                           TOKEN_ADJUST_DEFAULT, &original.value) != FALSE, "Probe token");
    Sid admin(L"S-1-5-32-544"), low(L"S-1-16-4096");
    SID_AND_ATTRIBUTES denied{admin.value, 0};
    Check(CreateRestrictedToken(original.value, DISABLE_MAX_PRIVILEGE, 1, &denied, 0, nullptr, 0,
        nullptr, &restricted.value) != FALSE, "Probe restricted identity");
    TOKEN_MANDATORY_LABEL label{{low.value, SE_GROUP_INTEGRITY}};
    Check(SetTokenInformation(restricted.value, TokenIntegrityLevel, &label,
          DWORD(sizeof(label) + GetLengthSid(low.value))) != FALSE, "Probe integrity");
    // The probe isolates process/job/integrity policy. The real helper also
    // verifies its restricting SIDs during the authenticated broker handshake.
    const HANDLE handles[]{output.value, allowed.value};
    aegis::security::ProcessAttributes attributes(handles);
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 2; // Child denial must come from the creation policy.
    Check(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
          "Probe process job");
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{aegis::security::UiRestrictions};
    Check(SetInformationJobObject(job.value, JobObjectBasicUIRestrictions, &ui, sizeof(ui)) != FALSE,
          "Probe desktop job");
    const auto window = CreateWindowExW(0, L"STATIC", L"Aegis owned boundary probe", WS_OVERLAPPEDWINDOW,
                                       0, 0, 20, 20, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(window != nullptr, "Owned test window");
    struct Window { HWND value; ~Window() { DestroyWindow(value); } } owned{window};
    const auto ownDc = GetDC(window);
    Expect(ownDc != nullptr, "Control window DC must be accessible to its owner");
    ReleaseDC(window, ownDc);
    auto command = Quote(Executable()) + L" --probe " + Quote(sentinel) + L" " +
        std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(reinterpret_cast<uintptr_t>(window)) +
        L" " + std::to_wstring(reinterpret_cast<uintptr_t>(allowed.value)) + L" " +
        std::to_wstring(reinterpret_cast<uintptr_t>(excluded.value)) + L" \"" + name + L"\" reserved";
    STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start);
    start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdOutput = start.StartupInfo.hStdError = output.value;
    start.lpAttributeList = attributes.Get();
    PROCESS_INFORMATION info{};
    Check(CreateProcessAsUserW(restricted.value, Executable().c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr,
        Executable().parent_path().c_str(), &start.StartupInfo, &info) != FALSE, "Start permission probe");
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job.value, process.value)) {
        TerminateProcess(process.value, 3); WinError("Assign probe job");
    }
    Check(ResumeThread(thread.value) != DWORD(-1), "Resume permission probe");
    CloseHandle(output.release());
    if (WaitForSingleObject(process.value, 15000) != WAIT_OBJECT_0) {
        TerminateJobObject(job.value, 3); throw std::runtime_error("Permission probe timeout");
    }
    DWORD exit = 0, available = 0, bytes = 0;
    Check(GetExitCodeProcess(process.value, &exit) != FALSE, "Probe exit status");
    const auto peeked = PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr);
    if (exit != 0 || !peeked || !available || available >= 4096)
        throw std::runtime_error("Permission probe failed; exit=" + std::to_string(exit) +
                                 " available=" + std::to_string(available));
    std::string text(available, '\0');
    Check(ReadFile(read.value, text.data(), available, &bytes, nullptr) && bytes == available, "Probe report read");
    const auto result = ParseFrame(text);
    for (auto it = result.begin(); it != result.end(); ++it)
        Expect(it.value().get<bool>(), it.key().c_str());
    Expect(WaitForSingleObject(allowed.value, 0) == WAIT_OBJECT_0, "Listed event was not signaled");
    Expect(WaitForSingleObject(excluded.value, 0) == WAIT_TIMEOUT, "Unlisted event leaked into child");
    Expect(av::scan::Read(sentinel, 128) == originalSentinel, "Host sentinel changed");
    return result;
}
void RealHelper(const fs::path& helper) {
    av::microvm::native_gui::Broker broker(helper, 1, 0, true);
    const auto pid = broker.ProcessForTesting();
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    Expect(process.value && aegis::security::HasProcessPolicy(process.value, ProcessRole::Leaf),
           "Real GUI helper launch policy");
    Expect(broker.CompleteUnused(), "Restricted real helper IPC/cleanup");
    broker.Finish();
    Expect(broker.Completed() && WaitForSingleObject(process.value, 3000) == WAIT_OBJECT_0,
           "Real helper job cleanup");
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--probe") return Probe(argc, argv);
        if (argc > 1 && std::wstring(argv[1]) == L"--must-not-run") return 99;
        Check(argc == 3, "Provide a fresh output folder and native USER32 helper");
        const auto folder = fs::absolute(argv[1]) / av::scan::Token();
        const auto result = LaunchProbe(folder);
        RealHelper(argv[2]);
        // Exercise the same disk reader used by the privileged protection broker.
        const fs::path bad = folder / L"deep.json";
        std::ofstream(bad) << std::string(10000, '[') << '0' << std::string(10000, ']');
        bool rejected = false;
        try { (void)av::scan::ReadJson(bad, 65536); } catch (...) { rejected = true; }
        Expect(rejected, "Caller file complexity was not bounded");
        std::cout << Json({{"phase", "19A"}, {"status", "PASS"}, {"checks", checks},
                          {"negativePermissionProbe", result}}).dump() << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
