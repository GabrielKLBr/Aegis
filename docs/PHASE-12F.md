# Phase 12F — MiniNT independent windows on Windows

This delivery adds an optional presentation mode for the corpus **Windows 10 x64** already accepted in phases 14.7–14.9. Each top-level window, including dialogues belonging to another window, wins an independent HWND on the host desktop. Controls and other daughter windows are drawn within your main window. The original Flappy and the matrix's own applications are the acceptance criteria; there is no promise of general graphical compatibility.

EXE, its DLLs and its callbacks continue in MiniNT. The Windows component creates its own windows, displays images, and returns typed events. It does not perform WNDPROCs, callbacks, or function addresses provided by the guest. No arbitrary forwarding of USER32/GDI. messages

** Scope distinction:** this 12F presents images rasterized by MiniNT. Running control creation and drawing operations by the actual Windows host APIs is the additional requirement planned in **14.10A–14.10D** of [roadmap](COMPLETE-ROADMAP.md#1410-subphase--gui-native-to-host-directx-media-and-audio). A [14.10A](PHASE-14.10A.md) adds the first native command bridge; 14.10B–14.10D remain pending. The tests of this 12F do not prove this native rendering. EXE, DLLs and callbacks will continue in the guest as well in this evolution.

## Bridge and isolation

`guardian-kernel-integrated.gk` inherits the kernel from 14.9 and adds the `GuardianWindowBridge/1` protocol. The mode is selected by `--integrated`; the enumeration negotiates this capacity separately. Previous kernels refuse this mode. The unique viewer mode remains available with `--interactive`.

The host assigns a session random identifier to each startup. The binary protocol contains this identifier, boot, version, size and sequence. It uses the existing F8 channel with a separate operation. Frames have start, metadata, an image for each main window visible and commit. The presentation only gets a frame after validating all images, dimensions and relationships. Duplicities, gaps, replay, owner of another process, cycles, resurrected IDs, and PID/TID changes of an ID are denied.

Each image is drawn separately by the guest composer. Thus, one window covered by another keeps its contents, including outside the visible region of the virtual desktop. No windows are cut from the already flattened image of the desktop. The previous path of composition and its hashes are preserved.

Real HWNDs are exclusively on the host. Even a guest number equal to the value of an external HWND is treated as opaque ID and gets a new window of its own. Owner relations are translated only between main windows of the same session. Owner who is a daughter window is outside this contract and is refused.

The title has `[MiniNT]` prefix. Untitled popups receive a blue border identification, designed only in the presentation. Native styles are selected from a limited list. The guest's sorting and priority `topmost` are applied between session windows; they do not make the application globally superior to other host applications.

## Input, focus, size and DPI

The presenter has a separate interface thread from the VM execution. Drag/resize a window in the Windows modal loop does not block the thread running MiniNT. A pending frame box maintains the latest state and accounts for replaced frames. This limits memory; transitory windows that appear and disappear between presentations may not be shown.

Physical keys, Unicode text, mouse, focus, blur, closing and geometry have their own formats. Each event identifies your session and window. The kernel resolves the recipient child control within that window, preserves the scan code and executes the callbacks in the guest. Late blur from one window does not clear the focus of another. Events for deleted or disabled windows are discarded or rejected and counted. Overruns and limits close the execution with observable error.

Guest focus is only applied to the native interface when the currently active Windows window already belongs to that session. The host does not call `SetForegroundWindow` or inject keyboard/mouse into other applications. There are no global hooks. Committed text is separated from the physical keys, avoiding a second character translation by the host.

The guest's logical space remains at **96 DPI**. The presentation thread uses DPI perception by monitor and converts coordinates/sizes to **96–240 DPI** in the host. `WM_DPICHANGED` adjusts the presentation; customer's movement and size return to MiniNT in logical coordinates. This is DPI virtualization, not implementation of the entire Windows API of DPI by monitor in the guest. The changed DPI context is only thread without changing PC settings.

Close the native window sends a request to the guest program. The window remains until the program is destroyed, including when it opens a confirmation dialog. Modality keeps the native owner disabled and restores it when the guest enables it. Minimization/restoration are presentation operations; no full support is declared for all Win32 states.

## Quotas and limits

| Resource | Limit |
| --- | --- |
| Guest windows, including children | 32 by meeting |
| Client of a main window | 1.280 × 720 pixels logical |
| Images of one tableau | 16 MiB added |
| Tables and commands of the Protocol | 4,096 frames and 131,072 commands per session |
| Rate of presentation | 512 table transactions and 128 MiB images per second |
| Entry | Native file of 2,048 events and 4,096 events delivered per session |
| Window identities throughout the session | 4.096, without reuse after exclusion |
| Test script | 2.048 actions, up to 30 seconds |
| Captures of diagnosis | Up to 16 client images per execution |

Repeated motions and pending geometry can be combined. Frame rates are transaction limits, including flushes, and not an FPS guarantee. Native presentation memory also depends on the DPI scale within the allowed range. There is no GPU passthrough, DirectX, complete transparent composition, complete IME, clipboard, drag files or access to Explorer. The USER32/GDI/controls limitations of phases 14.7 and 14.8 continue to apply.

CLI saves its budget of **1–30 seconds** per run. Capture and restore of RAM snapshots from this graphics family remain rejected. The 64 KiB virtual disk can be exported and provided to a new session by `--state`; it receives a new namespace and new native windows. HWNDs or features from the previous process are not resurrected.

## Usage

The example prepares the application itself. CRT/schema references are legal local entries and do not follow the builds:

```powershell
python -B tools/integrated_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output runtime-12f
.\PASTA-BUILD\Release\guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-integrated.gk runtime-12f/runtime.gpv runtime-12f/manifest.json runtime-12f/binding.gnt sessao-12f --integrated --seconds 30
```

The own test application requires A, text, click, movement/size and M to open your dialog; the acceptance script automates this interaction. For Flappy, add `--application CAMINHO-FLAPPY --asset CAMINHO-SETTINGS --asset CAMINHO-ICON` to the package creation. Execution of the external file is always in MiniNT. The network remains offline by default; an explicit policy of 14.9 can be provided separately.

`--window-script ARQUIVO` is a host type script for playable tests. It cannot be combined with `--interactive` or `--script` of the viewer. The format is `milissegundos|título convidado|operação|argumentos`, for example:

```text
300|12F Main|down|65|30|1
360|12F Main|up|65|30|0
450|12F Main|text|233
900|12F Main|resize|480|300
2400|@focused|close
```

Titles need to be unique for explicit selection; `@focused` selects the main window of the guest focus. The watch starts at the first native presentation. `scale` and `fail` actions exercise synthetic DPI paths and fail; they do not modify the display scale or Windows settings. There is no arbitrary Windows message operation in this file.

`window-bridge.json` records frames, commands, discards and a timeline of types/destinations of events without its text or key codes. `native-windows.json` records creation, destruction, geometry, DPI, coalescence and errors. `native-window-*.ppm` captures use `WM_PRINTCLIENT` only in their own windows and allow you to check content without capturing the personal desktop. The host program always collects your windows when closing or failing.

## Acceptance

The six new tests are `window_bridge_core`, `window_bridge_native`, `window_bridge_runtime_tools` and `window_bridge_programs_integration`, `window_bridge_flappy_integration` and `window_bridge_components_integration` integrations. Invalid packages and scripts are rejected before starting VM. An existing output folder is refused without overwriting the reports from the previous session.

The core checks the protocol, quotas, replay, relationships, session identity, independent design, clippings, window outside the desktop, focus, directed input, geometry and DPI conversions. The native test creates real HWNDs of different sessions, confers its pixels, input, modality, minimize/restore, synthetic DPI messages, cleaning and preservation of an external control window. Failure injections after HWND creation and allocation of DIB/DC verify partial resource collection; these injection points only exist in the native test, without exposure in the protocol or executable production. A guest ID with the value of an external HWND does not receive power over it.

They are **11 MiniNT scenarios by configuration**: own application in two sessions, guest drop, watchdog, simulated bridge failure, viewer alternative, three executions of the original Flappy and two control dialogues, confirmed and cancelled. Flappy tests alternate settings, motion, text and effect of flight commands. EXE, settings and original icon are checked before/later. Automated events use Win32 APIs only in the test windows; this does not represent a manual evaluation of all keyboards, monitors or desktop settings.

The `tools/validate_phase12f.py` validator requires **139 Release and 64 Debug acceptance tests**, without faults or ignored and with stable source code, binaries and references. The package is only created with these tests and is tested again after extraction and verification of all files/ZIP in both settings. It includes the regressions of the previous phases. Concrete results and hashes are on the manifests and reports of the private build. Debug of the host tests also requires the debugging runtime of Visual Studio. Linux has not been revalidated.

The 12F ends in this matrix. The WinUI main interface and the wider graphical compatibility keep their own lots on roadmap. **14.10 is not started** by this delivery.

Contract references: [SetWindowPos](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowpos), [context of DPI by thread](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setthreaddpiawarenesscontext), [frame adjustment for DPI](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-adjustwindowrectexfordpi) and [WM_DPICHANGED](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged)].
