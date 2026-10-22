// nt_gcc.c - a real, gcc-compiled, CRT-free Windows program for the MicroVM
// sandbox. Built with -nostdlib so the only thing it depends on is a handful of
// kernel32 imports -- which the synthetic NtKernel implements. Entry point is
// nt_main (set via the linker); no C runtime runs.
//
// Build (MinGW-w64 x64):
//   gcc -m64 -nostdlib -fno-stack-protector -Wl,--entry=nt_main \
//       -Wl,--image-base=0x200000 -Wl,--subsystem,console \
//       -o nt_gcc.exe nt_gcc.c -lkernel32

typedef unsigned long DWORD;
typedef int           BOOL;
typedef void*         HANDLE;
typedef const char*   LPCSTR;
typedef const void*   LPCVOID;

__attribute__((dllimport)) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__attribute__((dllimport)) BOOL   __stdcall WriteFile(HANDLE, LPCVOID, DWORD, DWORD*, void*);
__attribute__((dllimport)) HANDLE __stdcall CreateFileA(LPCSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
__attribute__((dllimport)) BOOL   __stdcall CloseHandle(HANDLE);
__attribute__((dllimport)) void   __stdcall ExitProcess(unsigned);

static DWORD slen(LPCSTR s) { DWORD n = 0; while (s[n]) n++; return n; }

void nt_main(void) {
    // 1) Write a line to stdout (the console).
    HANDLE out = GetStdHandle((DWORD)-11);            // STD_OUTPUT_HANDLE
    LPCSTR hello = "hello from a gcc-compiled freestanding .exe\n";
    DWORD written = 0;
    WriteFile(out, hello, slen(hello), &written, 0);

    // 2) Drop a file (5th arg, dwCreationDisposition, travels on the stack).
    HANDLE f = CreateFileA("C:\\out\\result.txt",
                           0x40000000 /*GENERIC_WRITE*/, 0, 0,
                           2 /*CREATE_ALWAYS*/, 0, 0);
    LPCSTR data = "written by a compiled binary, captured virtually";
    WriteFile(f, data, slen(data), &written, 0);
    CloseHandle(f);

    ExitProcess(0);
}
