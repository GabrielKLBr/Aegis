// main.cpp - command-line driver for the AntiVirus project.
#include "Common.h"
#include "SignatureDatabase.h"
#include "Scanner.h"
#include "Quarantine.h"
#include "RealTimeMonitor.h"
#include "WscIntegration.h"
#include "Sandbox.h"
#include "microvm/host/Preflight.h"

using namespace av;

static const wchar_t* kProductName = L"GuardianAV";
static const wchar_t* kVersion     = L"1.0";

static std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}

static void Banner() {
    PrintLine(L"", Color::Default);
    PrintLine(std::wstring(L"  ") + kProductName + L" " + kVersion +
              L"  -  a teaching antivirus for Windows", Color::Cyan);
    PrintLine(L"  ------------------------------------------------------", Color::Gray);
}

static void Usage() {
    Banner();
    PrintLine(L"Usage:  av <command> [args]", Color::Default);
    PrintLine(L"", Color::Default);
    PrintLine(L"Commands:", Color::Default);
    PrintLine(L"  scan <path>        Scan a file or folder (recursive). Detections", Color::Gray);
    PrintLine(L"                     are reported but NOT removed.", Color::Gray);
    PrintLine(L"  clean <path>       Scan and auto-quarantine anything detected.", Color::Gray);
    PrintLine(L"  monitor <folder>   Real-time protection: watch a folder and scan", Color::Gray);
    PrintLine(L"                     files as they are created/modified.", Color::Gray);
    PrintLine(L"  status             List antivirus products Windows Security shows.", Color::Gray);
    PrintLine(L"  register           Attempt WSC registration (explains MVI rules).", Color::Gray);
    PrintLine(L"  quarantine         List quarantined items.", Color::Gray);
    PrintLine(L"  sandbox <file>     Dynamically analyze a program in a restricted", Color::Gray);
    PrintLine(L"                     in-OS sandbox and report its behavior.", Color::Gray);
    PrintLine(L"  detonate <file>    Run a file in a hardened disposable VM", Color::Gray);
    PrintLine(L"                     (Windows Sandbox). Safe for destructive malware.", Color::Gray);
    PrintLine(L"  vmcheck            Probe MicroVM/WHP virtualization readiness and", Color::Gray);
    PrintLine(L"                     report how to enable it if it is off.", Color::Gray);
    PrintLine(L"  detonate-micro [f] Detonate a PE in the from-scratch MicroVM NT", Color::Gray);
    PrintLine(L"                     personality (no file = built-in sample).", Color::Gray);
    PrintLine(L"  eicar <file>       Write a harmless EICAR test file to verify", Color::Gray);
    PrintLine(L"                     detection works.", Color::Gray);
    PrintLine(L"", Color::Default);
    PrintLine(L"Signature DB: signatures.txt (next to the executable).", Color::Gray);
}

static SignatureDatabase LoadDb() {
    SignatureDatabase db;
    std::wstring sigPath = ExeDir() + L"\\signatures.txt";
    if (db.Load(sigPath))
        PrintLine(L"Loaded " + std::to_wstring(db.Count()) +
                  L" signatures + built-in EICAR rule.", Color::Gray);
    else
        PrintLine(L"signatures.txt not found - using built-in EICAR rule only.", Color::Yellow);
    return db;
}

static int WriteEicar(const std::wstring& file) {
    // Assemble the standard EICAR string at runtime (kept out of source as a literal).
    std::string s;
    s += "X5O!P%@AP[4\\PZX54(P^)7CC)7}$";
    s += "EICAR-STANDARD-ANTIVIRUS-TEST-FILE";
    s += "!$H+H*";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLine(L"Could not write: " + file, Color::Red);
        return 1;
    }
    DWORD w = 0;
    WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr);
    CloseHandle(h);
    PrintLine(L"Wrote harmless EICAR test file: " + file, Color::Green);
    PrintLine(L"Now run:  av scan \"" + file + L"\"", Color::Gray);
    return 0;
}

static std::wstring FmtMs(ULONGLONG t100ns) {
    double ms = (double)t100ns / 10000.0;
    wchar_t b[32]; swprintf(b, 32, L"%.1f ms", ms);
    return b;
}
static std::wstring FmtBytes(ULONGLONG n) {
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB" };
    double v = (double)n; int i = 0;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
    wchar_t b[32]; swprintf(b, 32, L"%.1f %ls", v, u[i]);
    return b;
}

static void PrintSandboxReport(const SandboxReport& r) {
    if (!r.error.empty()) { PrintLine(L"Sandbox error: " + r.error, Color::Red); return; }

    PrintLine(L"Containment: " + r.containment, Color::Gray);
    PrintLine(L"", Color::Default);
    PrintLine(L"Result", Color::Cyan);
    PrintLine(L"  launched     : " + std::wstring(r.launched ? L"yes" : L"no"),
              r.launched ? Color::Green : Color::Red);
    PrintLine(L"  exit code    : " + std::to_wstring((int)r.exitCode) +
              (r.timedOut ? L"  (killed at timeout)" : L""),
              r.timedOut ? Color::Yellow : Color::Gray);
    PrintLine(L"  processes    : " + std::to_wstring(r.totalProcesses), Color::Gray);
    PrintLine(L"  cpu (user)   : " + FmtMs(r.userTime100ns), Color::Gray);
    PrintLine(L"  cpu (kernel) : " + FmtMs(r.kernelTime100ns), Color::Gray);
    PrintLine(L"  peak memory  : " + FmtBytes(r.peakMemoryBytes), Color::Gray);
    PrintLine(L"  disk written : " + FmtBytes(r.ioWriteBytes), Color::Gray);

    PrintLine(L"", Color::Default);
    PrintLine(L"Process activity (" + std::to_wstring(r.processes.size()) + L")", Color::Cyan);
    for (auto& p : r.processes)
        PrintLine(L"  [" + p.action + L"] pid " + std::to_wstring(p.pid) +
                  (p.image.empty() ? L"" : L"  " + p.image) +
                  (p.parent ? L"  (parent " + std::to_wstring(p.parent) + L")" : L""),
                  Color::Gray);
    if (r.processes.empty()) PrintLine(L"  (none captured)", Color::Gray);

    PrintLine(L"", Color::Default);
    PrintLine(L"File activity (" + std::to_wstring(r.fileEvents.size()) + L")", Color::Cyan);
    for (auto& f : r.fileEvents)
        PrintLine(L"  [" + f.action + L"] " + f.path, Color::Gray);
    if (r.fileEvents.empty()) PrintLine(L"  (none captured)", Color::Gray);

    PrintLine(L"", Color::Default);
    PrintLine(L"Observations", Color::Cyan);
    for (auto& o : r.observations) PrintLine(L"  - " + o, Color::Yellow);
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) { Usage(); return 0; }
    std::wstring cmd = ToLower(argv[1]);

    std::wstring quarantineDir = ExeDir() + L"\\quarantine";

    if (cmd == L"scan" || cmd == L"clean") {
        if (argc < 3) { PrintLine(L"Missing <path>.", Color::Red); return 1; }
        Banner();
        bool autoQ = (cmd == L"clean");
        SignatureDatabase db = LoadDb();
        Quarantine q(quarantineDir);
        Scanner scanner(db, &q, autoQ);

        PrintLine(L"", Color::Default);
        PrintLine((autoQ ? L"Cleaning: " : L"Scanning: ") + std::wstring(argv[2]), Color::Cyan);
        ScanStats stats;
        scanner.ScanPath(argv[2], stats);

        PrintLine(L"", Color::Default);
        PrintLine(L"Done. " + std::to_wstring(stats.filesScanned) + L" files scanned, " +
                  std::to_wstring(stats.threatsFound) + L" threat(s), " +
                  std::to_wstring(stats.quarantined) + L" quarantined, " +
                  std::to_wstring(stats.suspicious) + L" suspicious, " +
                  std::to_wstring(stats.errors) + L" inconclusive/error(s).",
                  (stats.threatsFound || stats.suspicious || stats.errors) ? Color::Yellow : Color::Green);
        return stats.errors ? 3 : ((stats.threatsFound || stats.suspicious) ? 2 : 0);
    }

    if (cmd == L"monitor") {
        if (argc < 3) { PrintLine(L"Missing <folder>.", Color::Red); return 1; }
        Banner();
        SignatureDatabase db = LoadDb();
        Quarantine q(quarantineDir);
        Scanner scanner(db, &q, true /*auto-quarantine on detect*/);
        RealTimeMonitor mon(scanner, argv[2]);
        mon.Run();
        return 0;
    }

    if (cmd == L"status") {
        Banner();
        ListSecurityProducts();
        return 0;
    }

    if (cmd == L"register") {
        Banner();
        AttemptRegister(kProductName);
        return 0;
    }

    if (cmd == L"quarantine") {
        Banner();
        PrintLine(L"Quarantine contents:", Color::Cyan);
        Quarantine q(quarantineDir);
        q.List();
        return 0;
    }

    if (cmd == L"sandbox") {
        if (argc < 3) { PrintLine(L"Missing <file>.", Color::Red); return 1; }
        Banner();
        PrintLine(L"Dynamic analysis (lightweight in-OS sandbox)", Color::Cyan);
        PrintLine(L"WARNING: this EXECUTES the file in a restricted container. It is", Color::Yellow);
        PrintLine(L"good for quick triage but is NOT VM-grade isolation. For real", Color::Yellow);
        PrintLine(L"malware use 'sandbox-vm' (Windows Sandbox).", Color::Yellow);
        PrintLine(L"", Color::Default);

        SandboxOptions opt;
        if (argc >= 4) {
            int secs = _wtoi(argv[3]);
            if (secs > 0 && secs < 120) opt.timeoutMs = (DWORD)secs * 1000;
        }
        Sandbox sb;
        SandboxReport rep = sb.Run(argv[2], opt);
        PrintSandboxReport(rep);
        return rep.launched ? 0 : 1;
    }

    if (cmd == L"sandbox-vm" || cmd == L"detonate") {
        if (argc < 3) { PrintLine(L"Missing <file>.", Color::Red); return 1; }
        Banner();
        PrintLine(L"Detonation chamber (disposable Windows Sandbox VM)", Color::Cyan);
        PrintLine(L"Networking off, vGPU off, sample staged read-only (your folders are", Color::Gray);
        PrintLine(L"not exposed). Safe for destructive malware (e.g. MEMZ): an MBR wipe", Color::Gray);
        PrintLine(L"only destroys the throwaway VM. Close the VM to discard everything.", Color::Gray);
        PrintLine(L"", Color::Default);
        std::wstring err;
        if (LaunchWindowsSandbox(argv[2], err))
            PrintLine(L"Launched. Press a key inside the VM's console to detonate.", Color::Green);
        else
            PrintLine(err, Color::Red);
        return 0;
    }

    if (cmd == L"vmcheck") {
        Banner();
        microvm::PreflightResult pf = microvm::RunPreflight();
        microvm::PrintPreflight(pf);
        return pf.verdict == microvm::Verdict::Ready ? 0 : 1;
    }

    if (cmd == L"detonate-micro" || cmd == L"micro") {
        Banner();
        PrintLine(L"MicroVM NT-personality detonation (microsandbox)", Color::Cyan);
        std::wstring tool = ExeDir() + L"\\microsandbox.exe";
        if (GetFileAttributesW(tool.c_str()) == INVALID_FILE_ATTRIBUTES) {
            PrintLine(L"microsandbox.exe not found next to av.exe. Build the "
                      L"'microsandbox' CMake target.", Color::Red);
            return 1;
        }
        std::wstring cmdline = L"\"" + tool + L"\"";
        for (int i = 2; i < argc; ++i) cmdline += L" \"" + std::wstring(argv[i]) + L"\"";
        if (argc < 3) cmdline += L" --demo";  // default to the built-in sample
        STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
        std::vector<wchar_t> buf(cmdline.begin(), cmdline.end()); buf.push_back(0);
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0,
                            nullptr, nullptr, &si, &pi)) {
            PrintLine(L"Failed to launch microsandbox.exe.", Color::Red);
            return 1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return (int)ec;
    }

    if (cmd == L"eicar") {
        if (argc < 3) { PrintLine(L"Missing <file>.", Color::Red); return 1; }
        return WriteEicar(argv[2]);
    }

    Usage();
    return 0;
}
