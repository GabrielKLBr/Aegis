// nt_gui.c - a gcc-compiled, CRT-free program that pops a GUI dialog. It runs
// fully inside the MicroVM sandbox; the MessageBoxW call is proxied by the
// synthetic kernel to the REAL host user32, so a native Windows dialog appears
// (the "execute in the sandbox, render natively" idea). The sandbox auto-
// dismisses it so the detonation doesn't block.
//
// Build (MinGW-w64 x64):
//   gcc -m64 -nostdlib -fno-stack-protector -Wl,--entry=nt_main \
//       -Wl,--image-base=0x200000 -Wl,--subsystem,windows \
//       -o nt_gui.exe nt_gui.c -luser32 -lkernel32

#include <stddef.h>   // wchar_t (compiler header; fine with -nostdlib)

typedef void* HWND;
typedef unsigned UINT;
typedef const wchar_t* LPCWSTR;

__attribute__((dllimport)) int  __stdcall MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
__attribute__((dllimport)) void __stdcall ExitProcess(unsigned);

void nt_main(void) {
    MessageBoxW(0,
                L"This window is drawn by the REAL host user32 -- but the code "
                L"that called it ran isolated inside the MicroVM sandbox.",
                L"GuardianAV sandbox: native GUI proxy",
                0 /*MB_OK*/);
    ExitProcess(0);
}
