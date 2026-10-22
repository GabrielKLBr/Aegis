// Sandbox.cpp
#define WINVER        0x0A00
#define _WIN32_WINNT  0x0A00
#define NTDDI_VERSION 0x0A000004

#include "Sandbox.h"

#include <windows.h>
#include <sddl.h>      // ConvertStringSid / ConvertStringSecurityDescriptor
#include <aclapi.h>    // SetSecurityInfo / SetNamedSecurityInfo
#include <shlobj.h>    // SHCreateDirectoryEx
#include <shellapi.h>  // ShellExecuteW
#include <tlhelp32.h>  // process tree lookup

#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

namespace av {

static const ULONG_PTR KEY_JOB  = 1;
static const ULONG_PTR KEY_STOP = 0xDEAD;
static const size_t    kMaxEvents = 600;

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
static std::wstring TempRoot() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    return (n > 0) ? std::wstring(buf, n) : std::wstring(L"C:\\Temp\\");
}

static std::wstring FileNameOf(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? path : path.substr(s + 1);
}

static void RemoveTreeBestEffort(const std::wstring& dir) {
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(dir), ec);
}

// Apply a Low mandatory-integrity label to a kernel object handle.
static bool SetLowLabelHandle(HANDLE h, SE_OBJECT_TYPE type) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr))
        return false;
    BOOL present = FALSE, defaulted = FALSE;
    PACL sacl = nullptr;
    GetSecurityDescriptorSacl(sd, &present, &sacl, &defaulted);
    DWORD r = SetSecurityInfo(h, type, LABEL_SECURITY_INFORMATION,
                              nullptr, nullptr, nullptr, sacl);
    LocalFree(sd);
    return r == ERROR_SUCCESS;
}

// Apply a Low mandatory-integrity label to a filesystem path (inheritable).
static bool SetLowLabelPath(const std::wstring& path) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"S:(ML;OICI;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr))
        return false;
    BOOL present = FALSE, defaulted = FALSE;
    PACL sacl = nullptr;
    GetSecurityDescriptorSacl(sd, &present, &sacl, &defaulted);
    DWORD r = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                    LABEL_SECURITY_INFORMATION, nullptr, nullptr,
                                    nullptr, sacl);
    LocalFree(sd);
    return r == ERROR_SUCCESS;
}

static bool SetTokenLowIntegrity(HANDLE token) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-16-4096", &sid)) return false; // Low IL
    TOKEN_MANDATORY_LABEL til{};
    til.Label.Attributes = SE_GROUP_INTEGRITY;
    til.Label.Sid = sid;
    DWORD len = sizeof(TOKEN_MANDATORY_LABEL) + GetLengthSid(sid);
    BOOL ok = SetTokenInformation(token, TokenIntegrityLevel, &til, len);
    LocalFree(sid);
    return ok != 0;
}

static std::wstring ImageNameOfPid(DWORD pid) {
    std::wstring name;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do { if (pe.th32ProcessID == pid) { name = pe.szExeFile; break; } }
            while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    return name;
}

static DWORD ParentOfPid(DWORD pid) {
    DWORD parent = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do { if (pe.th32ProcessID == pid) { parent = pe.th32ParentProcessID; break; } }
            while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    return parent;
}

// ---------------------------------------------------------------------------
// monitoring threads
// ---------------------------------------------------------------------------
struct Monitors {
    std::mutex mtx;
    std::vector<ProcEvent> procs;
    std::vector<FileEvent> files;
    std::atomic<bool> stop{false};
};

static void JobMonitorThread(HANDLE port, Monitors* mon) {
    for (;;) {
        DWORD msg = 0; ULONG_PTR key = 0; LPOVERLAPPED ov = nullptr;
        BOOL ok = GetQueuedCompletionStatus(port, &msg, &key, &ov, INFINITE);
        if (key == KEY_STOP) break;
        if (!ok && ov == nullptr) break; // port closed
        DWORD pid = (DWORD)(ULONG_PTR)ov;

        ProcEvent ev; ev.pid = pid;
        bool record = false;
        switch (msg) {
            case JOB_OBJECT_MSG_NEW_PROCESS:
                ev.action = L"start";
                ev.image  = ImageNameOfPid(pid);
                ev.parent = ParentOfPid(pid);
                record = true;
                break;
            case JOB_OBJECT_MSG_EXIT_PROCESS:
                ev.action = L"exit";  record = true; break;
            case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS:
                ev.action = L"abnormal-exit"; record = true; break;
            default: break;
        }
        if (record) {
            std::lock_guard<std::mutex> lk(mon->mtx);
            if (mon->procs.size() < kMaxEvents) mon->procs.push_back(std::move(ev));
        }
    }
}

static void FileMonitorThread(HANDLE dir, std::wstring root, Monitors* mon) {
    std::vector<BYTE> buf(64 * 1024);
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                         FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                         FILE_NOTIFY_CHANGE_CREATION;
    while (!mon->stop.load()) {
        DWORD got = 0;
        BOOL ok = ReadDirectoryChangesW(dir, buf.data(), (DWORD)buf.size(), TRUE,
                                        filter, &got, nullptr, nullptr);
        if (!ok || got == 0) break; // aborted (CancelIoEx) or error

        BYTE* p = buf.data();
        for (;;) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            std::wstring rel(info->FileName, info->FileNameLength / sizeof(WCHAR));
            FileEvent fe;
            switch (info->Action) {
                case FILE_ACTION_ADDED:            fe.action = L"created";  break;
                case FILE_ACTION_REMOVED:          fe.action = L"deleted";  break;
                case FILE_ACTION_MODIFIED:         fe.action = L"modified"; break;
                case FILE_ACTION_RENAMED_OLD_NAME: fe.action = L"renamed";  break;
                case FILE_ACTION_RENAMED_NEW_NAME: fe.action = L"renamed";  break;
                default:                           fe.action = L"changed";  break;
            }
            fe.path = root + L"\\" + rel;
            {
                std::lock_guard<std::mutex> lk(mon->mtx);
                if (mon->files.size() < kMaxEvents) mon->files.push_back(std::move(fe));
            }
            if (info->NextEntryOffset == 0) break;
            p += info->NextEntryOffset;
        }
    }
}

// ---------------------------------------------------------------------------
// token / launch
// ---------------------------------------------------------------------------
static HANDLE BuildRestrictedToken(bool wantLow, bool& gotLow) {
    gotLow = false;
    HANDLE base = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
            TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_GROUPS | TOKEN_ADJUST_PRIVILEGES,
            &base))
        return nullptr;

    HANDLE restricted = nullptr;
    // DISABLE_MAX_PRIVILEGE strips privileges; LUA_TOKEN drops admin membership.
    if (!CreateRestrictedToken(base, DISABLE_MAX_PRIVILEGE | LUA_TOKEN,
                               0, nullptr, 0, nullptr, 0, nullptr, &restricted)) {
        CloseHandle(base);
        return nullptr;
    }
    CloseHandle(base);

    if (wantLow && SetTokenLowIntegrity(restricted))
        gotLow = true;

    return restricted;
}

// ---------------------------------------------------------------------------
// main entry
// ---------------------------------------------------------------------------
SandboxReport Sandbox::Run(const std::wstring& targetExe, const SandboxOptions& opt) {
    SandboxReport rep;

    if (GetFileAttributesW(targetExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        rep.error = L"Target file not found: " + targetExe;
        return rep;
    }

    // 1) isolated working directory ----------------------------------------
    std::wstring work = TempRoot() + L"GuardianAV_Sbx_" +
                        std::to_wstring(GetCurrentProcessId()) + L"_" +
                        std::to_wstring(GetTickCount());
    if (SHCreateDirectoryExW(nullptr, work.c_str(), nullptr) != ERROR_SUCCESS &&
        GetFileAttributesW(work.c_str()) == INVALID_FILE_ATTRIBUTES) {
        rep.error = L"Could not create sandbox directory.";
        return rep;
    }
    std::wstring sample = work + L"\\" + FileNameOf(targetExe);
    if (!CopyFileW(targetExe.c_str(), sample.c_str(), FALSE)) {
        rep.error = L"Could not copy sample into sandbox.";
        RemoveTreeBestEffort(work);
        return rep;
    }

    bool gotLow = false;
    HANDLE token = BuildRestrictedToken(opt.lowIntegrity, gotLow);
    if (opt.lowIntegrity && gotLow) {
        // let the (low-IL) sample read/write only inside its working dir
        SetLowLabelPath(work);
    }
    rep.lowIntegrity = gotLow;

    // 2) job object ---------------------------------------------------------
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
        lim.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
            JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        lim.BasicLimitInformation.ActiveProcessLimit = opt.activeProcessLimit;
        lim.ProcessMemoryLimit = opt.memoryLimitMB * 1024 * 1024;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim));

        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
        ui.UIRestrictionsClass =
            JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_EXITWINDOWS |
            JOB_OBJECT_UILIMIT_GLOBALATOMS | JOB_OBJECT_UILIMIT_HANDLES |
            JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_WRITECLIPBOARD |
            JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS;
        SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof(ui));

        JOBOBJECT_ASSOCIATE_COMPLETION_PORT assoc{};
        assoc.CompletionKey  = (PVOID)KEY_JOB;
        assoc.CompletionPort = port;
        SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation,
                                &assoc, sizeof(assoc));
    }

    // 3) hidden desktop -----------------------------------------------------
    HDESK desk = nullptr;
    std::wstring deskQualified;
    if (opt.hiddenDesktop) {
        desk = CreateDesktopW(L"GuardianAVSbx", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (desk) {
            if (gotLow) SetLowLabelHandle(desk, SE_WINDOW_OBJECT);
            wchar_t winsta[256]; DWORD need = 0;
            HWINSTA hws = GetProcessWindowStation();
            if (GetUserObjectInformationW(hws, UOI_NAME, winsta, sizeof(winsta), &need))
                deskQualified = std::wstring(winsta) + L"\\GuardianAVSbx";
        }
    }

    // 4) monitors -----------------------------------------------------------
    Monitors mon;
    HANDLE dirHandle = CreateFileW(work.c_str(), FILE_LIST_DIRECTORY,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    std::thread fileThread, jobThread;
    if (dirHandle != INVALID_HANDLE_VALUE)
        fileThread = std::thread(FileMonitorThread, dirHandle, work, &mon);
    if (port)
        jobThread = std::thread(JobMonitorThread, port, &mon);

    // 5) launch the sample (suspended), assign to job, resume ---------------
    STARTUPINFOW si{}; si.cb = sizeof(si);
    if (!deskQualified.empty()) si.lpDesktop = const_cast<LPWSTR>(deskQualified.c_str());
    PROCESS_INFORMATION pi{};

    std::wstring cmd = L"\"" + sample + L"\"";
    if (!opt.args.empty()) cmd += L" " + opt.args;

    DWORD flags = CREATE_SUSPENDED | CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP;

    BOOL launched = FALSE;
    if (token) {
        launched = CreateProcessAsUserW(token, sample.c_str(), cmd.data(), nullptr,
                                        nullptr, FALSE, flags, nullptr, work.c_str(),
                                        &si, &pi);
    }
    if (!launched) {
        // fall back to an unrestricted-token launch (still job + desktop + limits)
        rep.lowIntegrity = false; gotLow = false;
        launched = CreateProcessW(sample.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                                  flags, nullptr, work.c_str(), &si, &pi);
        if (launched) rep.observations.push_back(
            L"Restricted-token launch unavailable; ran with reduced (job-only) containment.");
    }

    if (!launched) {
        rep.error = L"Failed to launch sample (error " + std::to_wstring(GetLastError()) + L").";
    } else {
        rep.launched = true;
        AssignProcessToJobObject(job, pi.hProcess); // ok if already nested
        ResumeThread(pi.hThread);

        DWORD wait = WaitForSingleObject(pi.hProcess, opt.timeoutMs);
        if (wait == WAIT_TIMEOUT) {
            rep.timedOut = true;
            if (job) TerminateJobObject(job, 1);
            WaitForSingleObject(pi.hProcess, 2000);
        }
        DWORD code = 0;
        if (GetExitCodeProcess(pi.hProcess, &code)) rep.exitCode = code;

        Sleep(250); // brief grace for trailing FS notifications

        // accounting (query while the job is still open)
        if (job) {
            JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION acct{};
            if (QueryInformationJobObject(job, JobObjectBasicAndIoAccountingInformation,
                                          &acct, sizeof(acct), nullptr)) {
                rep.userTime100ns   = acct.BasicInfo.TotalUserTime.QuadPart;
                rep.kernelTime100ns = acct.BasicInfo.TotalKernelTime.QuadPart;
                rep.totalProcesses  = acct.BasicInfo.TotalProcesses;
                rep.ioReadBytes     = acct.IoInfo.ReadTransferCount;
                rep.ioWriteBytes    = acct.IoInfo.WriteTransferCount;
            }
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION ext{};
            if (QueryInformationJobObject(job, JobObjectExtendedLimitInformation,
                                          &ext, sizeof(ext), nullptr))
                rep.peakMemoryBytes = ext.PeakJobMemoryUsed;
        }
    }

    // 6) tear down ----------------------------------------------------------
    mon.stop = true;
    if (dirHandle != INVALID_HANDLE_VALUE) CancelIoEx(dirHandle, nullptr);
    if (port) PostQueuedCompletionStatus(port, 0, KEY_STOP, nullptr);
    if (fileThread.joinable()) fileThread.join();
    if (jobThread.joinable())  jobThread.join();

    if (pi.hThread)  CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (dirHandle != INVALID_HANDLE_VALUE) CloseHandle(dirHandle);
    if (job)   CloseHandle(job);   // kill-on-close reaps any stragglers
    if (port)  CloseHandle(port);
    if (desk)  CloseDesktop(desk);
    if (token) CloseHandle(token);

    // collect monitor results
    {
        std::lock_guard<std::mutex> lk(mon.mtx);
        rep.processes  = std::move(mon.procs);
        rep.fileEvents = std::move(mon.files);
    }

    RemoveTreeBestEffort(work);

    // 7) containment summary + heuristic observations -----------------------
    rep.containment = L"job(kill-on-close, mem<=" + std::to_wstring(opt.memoryLimitMB) +
                      L"MB, procs<=" + std::to_wstring(opt.activeProcessLimit) +
                      L"), UI-restricted, " +
                      (rep.lowIntegrity ? L"low-integrity token" : L"restricted token") +
                      (opt.hiddenDesktop ? L", hidden desktop" : L"") +
                      L", timeout " + std::to_wstring(opt.timeoutMs / 1000) + L"s";

    int created = 0, modified = 0, deleted = 0;
    for (auto& f : rep.fileEvents) {
        if (f.action == L"created")  created++;
        else if (f.action == L"modified") modified++;
        else if (f.action == L"deleted")  deleted++;
    }
    int starts = 0;
    for (auto& p : rep.processes) if (p.action == L"start") starts++;

    if (rep.timedOut)
        rep.observations.push_back(L"Process did not exit on its own; killed at timeout (long-running/persistent?).");
    if (starts > 0)
        rep.observations.push_back(L"Spawned " + std::to_wstring(starts) + L" child process(es).");
    if (created || modified || deleted)
        rep.observations.push_back(L"File activity: " + std::to_wstring(created) + L" created, " +
                                   std::to_wstring(modified) + L" modified, " +
                                   std::to_wstring(deleted) + L" deleted (inside sandbox).");
    if (rep.peakMemoryBytes > (SIZE_T)(opt.memoryLimitMB) * 1024 * 1024 * 8 / 10)
        rep.observations.push_back(L"Approached the memory limit.");
    if (rep.observations.empty() && rep.launched)
        rep.observations.push_back(L"No notable file or process activity observed.");

    return rep;
}

// ---------------------------------------------------------------------------
// Windows Sandbox (VM) handoff
// ---------------------------------------------------------------------------
static std::wstring SystemDir() {
    wchar_t buf[MAX_PATH];
    UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    return std::wstring(buf, n);
}

// True if a Windows hypervisor is currently running (CPUID leaf 1, ECX bit 31).
// Windows Sandbox needs this; without it WindowsSandbox.exe just exits.
#if defined(_MSC_VER)
#include <intrin.h>
static bool HypervisorPresent() {
    int r[4] = {0}; __cpuid(r, 1);
    return (r[2] & (1 << 31)) != 0;
}
#else
#include <cpuid.h>
static bool HypervisorPresent() {
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
    return (c & (1u << 31)) != 0;
}
#endif

static bool WindowsSandboxExePresent() {
    std::wstring exe = SystemDir() + L"\\WindowsSandbox.exe";
    return GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool WindowsSandboxAvailable() {
    return WindowsSandboxExePresent() && HypervisorPresent();
}

static bool WriteAllBytes(const std::wstring& path, const std::string& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &w, nullptr);
    CloseHandle(h);
    return ok && w == bytes.size();
}

// The script that runs inside the VM: stage the sample onto the VM's own disk,
// show a warning, wait for a keypress, then detonate. (No XML escaping needed
// because this lives in a .cmd file, not in the .wsb.)
static std::string DetonationScript() {
    return
        "@echo off\r\n"
        "title GuardianAV Detonation Chamber (disposable VM)\r\n"
        "mkdir C:\\detonation >nul 2>&1\r\n"
        "for %%F in (\"C:\\sample\\*\") do if /I not \"%%~nxF\"==\"detonate.cmd\" "
            "copy /y \"%%F\" \"C:\\detonation\\\" >nul\r\n"
        "cls\r\n"
        "echo ============================================================\r\n"
        "echo    GuardianAV DETONATION CHAMBER - disposable Windows Sandbox\r\n"
        "echo ------------------------------------------------------------\r\n"
        "echo    Networking : OFF         Clipboard : OFF\r\n"
        "echo    Host disk  : not shared writable (sample is read-only)\r\n"
        "echo.\r\n"
        "echo    This VM is THROWAWAY. Whatever the sample does - including\r\n"
        "echo    overwriting the MBR (e.g. MEMZ) - only destroys THIS VM.\r\n"
        "echo    Close the Windows Sandbox window to discard everything.\r\n"
        "echo ============================================================\r\n"
        "echo.\r\n"
        "echo    Sample staged in C:\\detonation\r\n"
        "echo    Press any key to DETONATE now, or close this window to abort.\r\n"
        "pause >nul\r\n"
        "for %%F in (\"C:\\detonation\\*\") do start \"\" \"%%F\"\r\n"
        "echo.\r\n"
        "echo    Detonated. Observe the behavior, then close the Sandbox window.\r\n";
}

bool GenerateWindowsSandboxConfig(const std::wstring& targetExe,
                                  std::wstring& outWsbPath, std::wstring& error) {
    if (GetFileAttributesW(targetExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"Target file not found."; return false;
    }

    // Private staging dir holding ONLY a copy of the sample + the script, so we
    // never map the user's real folder (e.g. Downloads) into the VM.
    std::wstring stage = TempRoot() + L"GuardianAV_Detonate_" +
                         std::to_wstring(GetCurrentProcessId()) + L"_" +
                         std::to_wstring(GetTickCount());
    if (SHCreateDirectoryExW(nullptr, stage.c_str(), nullptr) != ERROR_SUCCESS &&
        GetFileAttributesW(stage.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"Could not create staging directory."; return false;
    }
    if (!CopyFileW(targetExe.c_str(), (stage + L"\\" + FileNameOf(targetExe)).c_str(), FALSE)) {
        error = L"Could not stage the sample."; return false;
    }
    if (!WriteAllBytes(stage + L"\\detonate.cmd", DetonationScript())) {
        error = L"Could not write detonation script."; return false;
    }

    // Use only the universally-supported .wsb elements so the config never gets
    // rejected (an unknown element makes Windows Sandbox refuse the WHOLE file).
    // The boundary that actually contains MEMZ is the VM itself + Networking off;
    // the read-only mapping keeps the host file untouched. (Optional extra
    // hardening elements like ProtectedClient/ClipboardRedirection exist on newer
    // builds but are omitted here for maximum compatibility.)
    std::wstring xml;
    xml += L"<Configuration>\r\n";
    xml += L"  <vGPU>Disable</vGPU>\r\n";
    xml += L"  <Networking>Disable</Networking>\r\n";
    xml += L"  <MappedFolders>\r\n";
    xml += L"    <MappedFolder>\r\n";
    xml += L"      <HostFolder>" + stage + L"</HostFolder>\r\n";
    xml += L"      <SandboxFolder>C:\\sample</SandboxFolder>\r\n";
    xml += L"      <ReadOnly>true</ReadOnly>\r\n";
    xml += L"    </MappedFolder>\r\n";
    xml += L"  </MappedFolders>\r\n";
    xml += L"  <LogonCommand>\r\n";
    xml += L"    <Command>cmd.exe /k C:\\sample\\detonate.cmd</Command>\r\n";
    xml += L"  </LogonCommand>\r\n";
    xml += L"</Configuration>\r\n";

    outWsbPath = stage + L"\\detonate.wsb";
    if (!WriteAllBytes(outWsbPath, WideToUtf8(xml))) {
        error = L"Could not write .wsb config."; return false;
    }
    return true;
}

bool LaunchWindowsSandbox(const std::wstring& targetExe, std::wstring& error) {
    if (!WindowsSandboxExePresent()) {
        error = L"Windows Sandbox is not installed. Enable “Windows Sandbox” in "
                L"'Turn Windows features on or off' (requires Windows Pro/Enterprise/Education).";
        return false;
    }
    if (!HypervisorPresent()) {
        error = L"Windows Sandbox is installed, but the Windows hypervisor is not running, "
                L"so no VM can start. In an ELEVATED PowerShell run:  "
                L"Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All  "
                L"then  bcdedit /set hypervisorlaunchtype auto  and REBOOT.";
        return false;
    }
    std::wstring wsb;
    if (!GenerateWindowsSandboxConfig(targetExe, wsb, error)) return false;

    HINSTANCE r = ShellExecuteW(nullptr, L"open", wsb.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { error = L"Failed to launch Windows Sandbox."; return false; }
    return true;
}

} // namespace av
