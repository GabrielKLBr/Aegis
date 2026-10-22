// Sandbox.h - lightweight in-OS behavioral sandbox + Windows Sandbox (VM) handoff.
//
// SAFETY NOTE
// -----------
// The built-in sandbox runs the target in a heavily restricted user-mode
// container (Job Object + restricted/low-integrity token + hidden desktop +
// auto-timeout) and observes its behavior. This is fast triage, NOT bulletproof
// isolation: advanced malware can detect or escape a user-mode sandbox. For
// genuine safety, hand the sample to Windows Sandbox (a disposable VM) via the
// functions at the bottom of this header.
#pragma once
#include "Common.h"
#include <vector>

namespace av {

struct SandboxOptions {
    DWORD        timeoutMs          = 8000;   // wall-clock kill
    SIZE_T       memoryLimitMB      = 256;    // per-process memory cap
    DWORD        activeProcessLimit = 32;     // max processes in the job
    bool         lowIntegrity       = true;   // run at Low IL (best effort)
    bool         hiddenDesktop      = true;   // isolate any windows it creates
    std::wstring args;                        // command line passed to target
};

struct ProcEvent {
    std::wstring action;   // "start" | "exit" | "abnormal-exit"
    DWORD        pid    = 0;
    DWORD        parent = 0;
    std::wstring image;    // best-effort image name
};

struct FileEvent {
    std::wstring action;   // created | modified | deleted | renamed
    std::wstring path;
};

struct SandboxReport {
    bool         launched   = false;
    bool         timedOut   = false;
    bool         lowIntegrity = false;       // whether Low IL was actually applied
    DWORD        exitCode   = 0;
    std::wstring containment;                 // human summary of applied limits
    std::wstring error;

    // resource accounting (from the Job Object)
    ULONGLONG    userTime100ns   = 0;
    ULONGLONG    kernelTime100ns = 0;
    SIZE_T       peakMemoryBytes = 0;
    ULONGLONG    ioReadBytes     = 0;
    ULONGLONG    ioWriteBytes    = 0;
    DWORD        totalProcesses  = 0;

    std::vector<ProcEvent> processes;
    std::vector<FileEvent> fileEvents;
    std::vector<std::wstring> observations; // heuristic notes
};

class Sandbox {
public:
    // Copies 'targetExe' into an isolated working directory, runs it under the
    // restrictions in 'opt', captures behavior, then tears everything down.
    // Blocking; returns a populated report (check report.error on failure).
    SandboxReport Run(const std::wstring& targetExe, const SandboxOptions& opt = {});
};

// --- Windows Sandbox (disposable VM) handoff -------------------------------

// True if the Windows Sandbox feature appears to be installed.
bool WindowsSandboxAvailable();

// Writes a .wsb config that maps the sample's folder read-only into a VM with
// networking disabled and auto-runs it. Returns the .wsb path in outWsbPath.
bool GenerateWindowsSandboxConfig(const std::wstring& targetExe,
                                  std::wstring& outWsbPath, std::wstring& error);

// Generates the config and launches Windows Sandbox on it.
bool LaunchWindowsSandbox(const std::wstring& targetExe, std::wstring& error);

} // namespace av
