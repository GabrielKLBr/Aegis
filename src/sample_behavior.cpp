// sample_behavior.cpp - a HARMLESS program used to exercise the sandbox.
//
// It performs benign, observable actions so the sandbox's behavior capture has
// something to report: it writes files in its working directory, attempts a
// write to %TEMP%, allocates memory, spawns one child copy of itself, then
// exits. It does nothing destructive and touches nothing outside its sandbox.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstring>

int wmain(int argc, wchar_t** argv) {
    bool isChild = (argc >= 2 && std::wstring(argv[1]) == L"child");

    // 1) drop a couple of files in the current (sandbox) directory
    const wchar_t* names[2] = { L"sandbox_dropped.txt", L"sandbox_config.tmp" };
    for (int i = 0; i < 2; ++i) {
        HANDLE h = CreateFileW(names[i], GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const char* msg = isChild ? "written by child\n" : "written by sample\n";
            DWORD w = 0; WriteFile(h, msg, (DWORD)std::strlen(msg), &w, nullptr);
            CloseHandle(h);
        }
    }

    // 2) modify one of them
    HANDLE h = CreateFileW(names[0], FILE_APPEND_DATA, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const char* more = "appended line\n";
        DWORD w = 0; WriteFile(h, more, (DWORD)std::strlen(more), &w, nullptr);
        CloseHandle(h);
    }

    // 3) try to write OUTSIDE the sandbox (into %TEMP%) - expected to be blocked
    //    when running at low integrity; harmless either way.
    wchar_t tmp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp)) {
        std::wstring p = std::wstring(tmp) + L"guardian_sample_escape.txt";
        HANDLE e = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (e != INVALID_HANDLE_VALUE) CloseHandle(e);
    }

    // 4) allocate ~32 MB and touch it
    if (void* p = VirtualAlloc(nullptr, 32u * 1024 * 1024, MEM_COMMIT, PAGE_READWRITE)) {
        std::memset(p, 0x42, 1u * 1024 * 1024);
    }

    // 5) top-level instance spawns one child copy of itself
    if (!isChild) {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cmd = std::wstring(L"\"") + self + L"\" child";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE,
                           0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    Sleep(isChild ? 200 : 600);
    return isChild ? 7 : 0;
}
