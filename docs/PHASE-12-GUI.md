# Phase 12 — batch of graphical compatibility

**Updating by 14.7:** this document preserves the history of the graphic batch of phase 12. The execution of the original Flappy, physical entries, text and new windows were promoted to [delivery 14.7](PHASE-14.7.md), with own acceptance Release/Debug. The current evaluation of the 12F is in this delivery; its implementation remains dependent on new request. The old limitations below describe the state of 23/10/2026.

**23/10/2026: partial advance; Flappy Window batch not completed. 12F has not been started.**This delivery first connects a separate PE file to the process kernel and guest composer. Validation of this small matrix does not declare compatibility with Windows applications in general.

## Path implemented

`guardian-gui-pe` receives kernel, application, and a new evidence folder. The PE file is copied unaltered to a reserved, read-only, and unrun-only region with boot-bound descriptor. The kernel checks the limits, reservation, and hash before using its existing PE charger. Headers, relocations, imports, and protections continue to be checked in the guest. The application does not stay embedded in the kernel. The own DLL USER32 is built in; no Windows host DLLs are loaded.

The `guardian-gui-application.exe` reference program imports functions with Windows signatures, creates a window, paints rectangles, processes messages and changes the drawing after a key and click. Its window procedures and all DLL code run on CPL3 in the guest. Typed drawing commands pass through the kernel and the existing graphical channel; the host receives validated pixels and metadata. The viewer remains a single native window. There is no arbitrary referral of functions, callbacks, HWND or HDC to the host.

The DLL implements a subset of 18 exports: `RegisterClassA`, `CreateWindowExA`, `DefWindowProcA`, `DestroyWindow`, `ShowWindow`, `UpdateWindow`, `InvalidateRect`, `BeginPaint`, `EndPaint`, `GetClientRect`, `FillRect`, `GetSysColorBrush`, `GetMessageA`, `PeekMessageA`, `TranslateMessage`, `DispatchMessageA`, `PostQuitMessage` and `GetSystemMetrics`. There are explicit restrictions of parameters: classes without extra data, windows visible with style `WS_CAPTION | WS_VISIBLE`, without menu or related; queue without filters and with `PM_REMOVE`/`PM_NOREMOVE`, integral invalidation without erasure, rectangle painting and two system colors from the virtual desktop. This does not represent integral implementation of these APIs for any parameter. The current device provides ASCII text, routed as `WM_CHAR`; does not provide physical key codes, modifier status or `WM_KEYUP`. `TranslateMessage` only covers a subset without modifiers. Games that depend on physical keys require expanding this contract before acceptance.

The matrix uses 320×240, a GUI thread in the root process, up to four classes and windows and 16 queued messages. The PE font and the loaded image have a roof of 128 KiB each. file/console calls are not part of this new contract and return without support; previous storage contracts remain separate. Snapshots of this contract are explicitly refused. There is no general CRT, complete GDI/DWM, Unicode, GPU acceleration or integration with the scanner queue in this delivery. Phase 14 scanner keeps its backend documented.

## Build and execute reference

On Windows x64, with MSVC and WHP available, use a new build folder:

```powershell
cmake -S . -B build-gui -A x64 -DBUILD_TESTING=ON -DGUARDIAN_GUI_COMPATIBILITY_ONLY=ON -DGUARDIAN_BUILD_PE_GUI=ON
cmake --build build-gui --config Release --target guardian-gui-pe pe_gui_boot_tests pe_gui_native_oracle --parallel 4
ctest --test-dir build-gui -C Release -R "^pe_gui_" --output-on-failure
build-gui\Release\guardian-gui-pe.exe build-gui\guest\Release\guardian-kernel-pe-gui.gk build-gui\guest\fixtures\Release\guardian-gui-application.exe build-gui\sessao-nova --interactive
```

In the viewer window, type `G`, click on the client area and close the window to complete the reference sequence. The execution has a real budget of 30 seconds. Manual input and timed script are separate modes; `--script ARQUIVO` uses the existing format of the guest scripts. The build profile above excludes frontends/dependencies from the scanner; it is intended for the tools and core tests. In the normal build, the new trial contract explicitly requires `GUARDIAN_BUILD_PE_GUI=ON`. Linux/KVM did not receive construction/acceptance from this new contract in this delivery.

## Flappy Window 1.5 Rating

User-provided file, SHA-256 `f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6`. Static inventory, without running the game, found:

| Request | Flappy | New contract |
|---|---|---|
| EP file | 3.202.095 bytes | Up to 131.072 bytes |
| Memory Image | 1.548.288 bytes | Up to 131.072 bytes |
| DLLs imported | USER32, GDI32, KERNEL32, MSVCRT and DWMAPI | USER32 itself, with explicit subset |
| Import entries | 178, including one duplicate | 16 match a exports available; 162 stay absent on this contract |
| Game execution, settings and closure | Roadmap acceptance criteria | Not shown |

The 88 imports from MSVCRT and 59 from KERNEL32 are not automatically resolved by existing implementations under another DLL name. There are also nine imports from USER32, five from GDI32 and one from DWMAPI. Quantities are import inputs, not behaviorally validated functions. Having an export does not prove that the parameters used by the game are supported.

It is still necessary to expand the loader and memory budgets with tests, implement and validate the dependencies and startup of runtime, integrate files/settings and complete the graphics operations actually used. The current limit of four modules and the private page budget also need revision for this application; increase only constants or use empty success returns does not meet the compatibility criteria. The game has not been executed on Windows host as an alternative.

`tools/inspect_gui_compatibility.py` produces import inventory compared to actual compiled DLL exports. `candidateForGuestLoader` just means that verified static prerequisites have passed; the loader and execution remain the authority. `functionalCompatibilityProven` remains false in this tool, which does not run programs.

## Validation and decision on 12F

The same source of the reference application is compiled against the native Windows USER32 and against the implementation of the guest. The native test sends messages only to its own window; it does not inject global input. The boot test provides PE as an independent file in 16, 32 and 64 MiB and confers three pixel states independently: initial painting, change after key and change after click. It also checks closing, process cleaning, pages of the RO/NX source, preserving bytes, refusal of invalids/overflow pointers and access to windows of the host, invalid PE, tampered source and unsupported snapshots. A private host control remains outside the invited RAM and its file remains unchanged. These tests cover the exercised channels; they do not constitute general proof of absence of vulnerabilities.

The evidence remains in `build-phase12-gui/`. The `tools/validate_phase12_gui.py` validator records JUnit, hashes of selected sources and GUI binaries and their stability, maintaining `phaseComplete: false`.

| Configuration Windows/MSVC/WHP | Final result | Evidence |
|---|---|---|
| Release: three new tests and twelve PE regressions, graphics and analysis | 15/15, without ignored; 139,19 s | `acceptance-release-final/validation.json` and JUnit |
| Debug: three new tests | 3/3, without ignored | `acceptance-debug-final/validation.json` and JUnit |

The regressions include the four previous PE kernels, the runtime core, graphics, viewer, previous graphical application, and behavioral analysis. The complete suite of the phase 14 scanner was not executed in this delivery. During preparation, an incomplete dependency of the analysis test was corrected: it also needs to build the reference kernels CPU, storage, network, hardware, and the driver/API itself. Previous attempts with missing references and compilation adjustments were preserved without counting them as acceptance. The final maps of Debug/Release sources coincide and were stable during the tests. The recoverable backup is `baseline-verified.zip`, linked to the initial commit and preserving the previous inclusion of 12F in the roadmap.

Demonstration package: `dist/aegis-phase12-gui-prototype-windows-x64-20261023/`, with SHA-256 manifest and copy of the evidence. The reference PE is distributed with `.gpe` extension, intended for loading to the guest. It is neither a Flappy compatibility delivery nor a phase 12 closure.

**Minimum prerequisites demonstrated for the proper reference:**loading an external PE to the kernel, drawing and receiving input by the viewer. **Prerequirements for the Flappy matrix and to start the 12F in this scope:**not met.The representative external application does not yet execute; the batch is not declared closed or the 12F is started automatically.

Consulted contracts: [CreateWindowExA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexa), [PeekMessageA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagea) and [BeginPaint](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-beginpaint)].
