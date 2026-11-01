# Phase 14.12 — x86 and additional NT profiles

## Status: first batch 14.12A, x86 core for Windows 10

The MiniNT x64 kernel starts PE32 executables and 32-bit own DLLs in CPU x86 compatibility mode, CPL3, over WHP. The executable input and its callbacks remain within the VM.

This batch establishes the core of 14.12. The wide phase remains open: it does not implement complete WOW64, arbitrary Microsoft DLLs or general x86 support. The only accepted profile is **NT 10.0.19045 / Windows 10 x86**. Other profiles are refused in this package without promoting the inventory of versions to binary support.

## Implementation

- PE32 charger separated by kernel configuration: I386/0x14c, header
  0x10b, four-byte IAT and HYGHLOW relocations. Mixing PE32 and AMD64 in the same graph is refused. Limits, rollback and W^X protection remain.
- Input in CS 0x1b, 32-bit stack, stdcall/cdecl and interrupt return
  dedicated gate DPL3. FS:[18h] identifies the TEB32, FS:[30h] points to PEB32 and PEB:[8] contains the base of the image.
- `kernel32.dll`, `user32.dll`, `win32u.dll` and `msvcrt.dll` themselves generated
  by cross compiler x86, without linking the host CRT. DllMain and the application entry run on the guest.
- Search for DLLs in `C:\Windows\SysWOW64`; module consultation
  `C:\Windows\System32\kernel32.dll` redirected to x86 module. This is not yet general redirection of files/log.
- Thunks with 32-bit pointers, kernel memory scan and requests with
  size/version and validated origin. The graphical protocol maintains 64-bit opaque IDs, represented by 32-bit IDs in the guest. Old IDs do not identify new windows; real HWNDs do not cross the VM.
- USER32 uses the existing restricted auxiliary: window, BUTTON, EDIT and actual STATIC
  on Windows; events and WndProc stdcall x86 in MiniNT. The `native-user32.json` report keeps the identifier of the backend **14.10B/protocol 2**; manifest and proof of this delivery identify 14.12.
- Telemetry of observed operations. Output different from zero and access
  invalid generate unsuccessful session and analysis **inclusive**, with window cleaning and also assist in case of exception.

## Current contracts and limits

`kernel32` covers identification, pseudo-handles, LastError, WOW64 consultation of the current process, console by WriteFile, heap/allocation RW, clock, modules already loaded, export by name, ExitProcess and UTF-8/CP1252 conversions itself. The inventory generated allows you to check exports; the name does not imply full Windows contract.

There is a process and an application thread. LastError and the graphical table depend on this limit. The allocation accepts address chosen by the kernel, reserve+commit RW, up to 1 MiB per region and 64 simultaneous regions. VirtualFree releases only regions from this allocater; modules/TEB/batteries are protected. RWX pages are refused. The heat uses a region by allocation. The table saves up to 128 graphical IDs during the session, in addition to the limit of live objects from the backend. Sleep query the loop clock; it is not full Win32 synchronization.

`msvcrt` meets `puts` and `printf` of literal text without `%` conversions. It is not general CRT. Dynamic imports by ordinal, API sets, LoadLibrary, child processes, threads and the remaining NT/Win32 contracts require continuation.

TLS PE32, SEH x86, C++ exceptions, load config/CFG, delay imports and CLR are**refused**. Invalid access terminates the process with exception status, without dispatch of SEH handlers. There is no GDI/DirectX/audio x86, log/VFS x86 general, 16-bit programs or ARM in this batch.

## Delimited validation

**Windows/WHP results in 29/10/2026:**Release **18/18**, Debug **18/18**, with stable sources and binaries. Parser ASan approved 10,240 checks and 4,608 truncations in Release; 13,312 checks and 6,144 truncations in Debug, in addition to 1,000 mutations per configuration. These numbers are internal checks, not independent program counts.

`tools/validate_compat32.py` brings together **18 cases per configuration**, only for this batch and its used dependencies; does not repeat old batteries.

- Parser/charger under AddressSanitizer: grafo PE32, fixups, IAT, W^X, rollback,
  architecture/TLS/overflow refused, all the graph EXE truncages and 1,000 deterministic mutations. Analyzed entries, never executed in the host.
- Console/GUI own: ABI/FS/PEB, search SysWOW64, heat, quota, double release,
  invalid pointer, executable pages refused and old IDs without aliases.
- Input directed only to the test assistant: `abc` text in EDIT and click on
  BUTTON, checked by guest callback; without using global mouse/keyboard.
- Project independent `leachim6/hello-world` Console C, unaltered source
  fixed by blob/SHA-256 in `config/compat32-corpus.json`. Compiles with input and own DLLs and prints `Hello World` in MiniNT. It does not demonstrate broad CRT; the GUI itself does not count as an independent graphic corpus.
- Memory failure with/without windows and ExitProcess(17): inconclusive analysis,
  No Case/object/handle/reference alive, auxiliary closed and zero windows.
- Profile, architecture, CLR, RWX, truncation, DLL x64 and launch mode
  incompatible refused; tampered manifest to x64 refused before MV.

The evidence requires stability of source code/binary. hashes The packer only accepts current proofs approved in Release and Debug. The extracted package is tested in the same scope; hashes are checked before and after. Builds are in the private repository. Microsoft Runtimes, external source and binary are not redistributed; playback requires local tools/references.

## Play on Windows

Requirements: Windows x64 with NX/WHP, Python, Visual Studio 2022 C++ x86/x64 and Windows SDK. Provide local installation runtime ASan. Use CMake configuration that already enables the native USER32 kernel.

```powershell
cmake --build PASTA_BUILD --config Release --target guardian_compat32_runtime guardian-compat32-tests
cmake --build PASTA_BUILD --config Debug --target guardian_compat32_runtime guardian-compat32-tests
python -B tools/validate_compat32.py --build PASTA_BUILD --config Release --output PROVA_RELEASE_NOVA --corpus CACHE_EXTERNO --asan-runtime PASTA_ASAN
python -B tools/validate_compat32.py --build PASTA_BUILD --config Debug --output PROVA_DEBUG_NOVA --corpus CACHE_EXTERNO --asan-runtime PASTA_ASAN
python -B tools/package_phase14_12.py --build PASTA_BUILD --release PROVA_RELEASE_NOVA --debug PROVA_DEBUG_NOVA --output PACOTE_NOVO
```

The EXEs/DLLs x86 are entries **only for the guest**. The builder below inspects the files, validates imports and creates volume, manifest, inventory and binding without running the program. `compat32_runtime.verify` checks its hashes and links. The following command executes the entry inside MiniNT:

```powershell
python -B tools/compat32_runtime.py --build PASTA_BUILD --config Release --application APP_X86.exe --output BUNDLE_NOVO
PASTA_BUILD/Release/guardian-desktop.exe PASTA_BUILD/guest/Release/guardian-kernel-compat32.gk BUNDLE_NOVO/runtime.gpv BUNDLE_NOVO/manifest.json BUNDLE_NOVO/binding.gnt SESSAO_NOVA --compat32 --seconds 15
```

## Continuation of 14.12

Before the broad conclusion: corpus GUI independent; thunks of files/log with separate visions; TLS/SEH and native services required for the chosen programs; expansion of graphic DLLs and other families on demand. Each new NT profile requires legal references and acceptance by build. 14.13 It's not started on this delivery.

References: [32-bit applications](https://learn.microsoft.com/en-us/windows/win32/winprog64/running-32-bit-applications) and [WOW64](https://learn.microsoft.com/en-us/windows/win32/winprog64/wow64-implementation-details) details. MiniNT uses its own implementation without redistributing Windows WOW64.
