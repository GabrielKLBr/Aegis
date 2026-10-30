#include "Scanner.h"
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace av::scan
{
Json RunWorker(const fs::path &job, const Json &request, const std::atomic<bool> *cancel)
{
    OrdinaryDirectory(job);
    WriteJson(job / "request.json", request);
    const auto limits = Limits::Parse(request.at("limits"));
    auto executable = Executable().parent_path() / "guardian-scan-worker";
#ifdef _WIN32
    executable += ".exe";
#endif
    Require(fs::is_regular_file(fs::symlink_status(executable)), "Scanner worker missing");
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.milliseconds + 1000);
    bool timeout = false, cancelled = false;
    int exitCode = -1;
#ifdef _WIN32
    struct Handle
    {
        HANDLE h = nullptr;
        ~Handle()
        {
            if (h && h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    } group, process, thread;
    group.h = CreateJobObjectW(nullptr, nullptr);
    Require(group.h != nullptr, "Cannot create scanner process boundary");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
    policy.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                              JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_JOB_MEMORY;
    policy.ProcessMemoryLimit = 384ull << 20;
    const bool sandbox = request.at("action") == "sandbox";
    policy.BasicLimitInformation.ActiveProcessLimit = sandbox ? 3 : 1; // VM, RAM owner and restricted GUI helper.
    policy.JobMemoryLimit = sandbox ? (512ull << 20) : (384ull << 20);
    Require(SetInformationJobObject(group.h, JobObjectExtendedLimitInformation, &policy, sizeof(policy)) !=
                FALSE,
            "Cannot constrain scanner process");
    const auto arg = fs::absolute(job).native();
    Require(arg.find(L'"') == arg.npos && !arg.ends_with(L'\\'), "Worker path rejected");
    std::wstring command = L"\"" + executable.native() + L"\" \"" + arg + L"\"";
    // Do not inherit API credentials or handles into the parser/VM process.
    std::wstring environment;
    for (const auto *key : {L"SystemRoot", L"TEMP", L"TMP"})
    {
        wchar_t value[32768]{};
        const auto n = GetEnvironmentVariableW(key, value, 32768);
        if (n && n < 32768)
        {
            environment += key;
            environment += L'=';
            environment += value;
            environment += L'\0';
        }
    }
    environment += L'\0';
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION pi{};
    Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                           environment.data(), job.c_str(), &start, &pi) != FALSE,
            "Cannot start scanner worker");
    process.h = pi.hProcess;
    thread.h = pi.hThread;
    if (!AssignProcessToJobObject(group.h, process.h))
    {
        TerminateProcess(process.h, 3);
        WaitForSingleObject(process.h, 5000);
        throw std::runtime_error("Cannot apply scanner process boundary");
    }
    if (ResumeThread(thread.h) == DWORD(-1))
    {
        TerminateJobObject(group.h, 3);
        WaitForSingleObject(process.h, 5000);
        throw std::runtime_error("Cannot resume scanner worker");
    }
    for (;;)
    {
        const auto wait = WaitForSingleObject(process.h, 20);
        if (wait == WAIT_OBJECT_0)
            break;
        Require(wait == WAIT_TIMEOUT, "Worker wait failed");
        cancelled = cancel && cancel->load();
        timeout = std::chrono::steady_clock::now() >= deadline;
        if (cancelled || timeout)
        {
            TerminateJobObject(group.h, 3);
            WaitForSingleObject(process.h, 5000);
            break;
        }
    }
    DWORD code = 0;
    Require(GetExitCodeProcess(process.h, &code) != FALSE, "Worker exit status unavailable");
    exitCode = int(code);
#else
    const auto exe = executable.string(), folder = fs::absolute(job).string();
    const auto pid = ::fork();
    Require(pid >= 0, "Cannot start scanner worker");
    if (!pid)
    {
        struct rlimit mem{384ull << 20, 384ull << 20}, cpu{65, 65};
        ::setrlimit(RLIMIT_AS, &mem);
        ::setrlimit(RLIMIT_CPU, &cpu);
        ::setpgid(0, 0);
        char *const args[] = {const_cast<char *>(exe.c_str()), const_cast<char *>(folder.c_str()), nullptr};
        char *const env[] = {const_cast<char *>("LANG=C.UTF-8"), nullptr};
        ::execve(exe.c_str(), args, env);
        ::_exit(127);
    }
    int status = 0;
    for (;;)
    {
        const auto done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0 && errno == EINTR)
            continue;
        Require(done >= 0, "Worker wait failed");
        cancelled = cancel && cancel->load();
        timeout = std::chrono::steady_clock::now() >= deadline;
        if (cancelled || timeout)
        {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
            {
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    Require(!cancelled, "analysis cancelled");
    Require(!timeout, "worker wall time limit exceeded");
    Require(exitCode == 0, "scanner worker failed/resource limit exceeded");
    const auto result = ReadJson(job / "result.json");
    Require(result.is_object() && result.value("schema", 0) == 1 && result.contains("complete") &&
                result.at("complete").is_boolean(),
            "Worker result schema rejected");
    const auto classification = result.value("classification", std::string());
    Require(classification == "unknown" || classification == "inconclusive" ||
                classification == "suspicious" || classification == "detected",
            "Worker classification rejected");
    return result;
}
} // namespace av::scan
