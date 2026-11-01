# Phase 14.7 — graphical desktop and external applications

The `guardian-kernel-desktop.gk` kernel adds the window subsystem and own DLLs USER32, GDI32, GDI32FULL, WIN32U and DWMAPI to MiniNT. Delivery is limited to the Windows 10 x64 profile and CRT references fixed in 14.5. The user-provided Flappy Window 1.5 execution integrates acceptance; static resolution of its imports is not used as functional proof.

## Execution and border with the host

The program, its window callbacks, timers and libraries run on CPL3 in the guest. The kernel validates PID/TID, identifiers, permissions, coordinates, clippings and sizes. The input of graphical services is the A7 port exposed by the Win32U DLL itself; it does not allow you to perform functions in the host. The model keeps windows, messages, painting and input under MiniNT control.

The host only receives typed pixels and metadata. The new viewer features a desktop of 1280×720 within a single window. Flappy windows do not yet become Windows independent windows. This integration belongs to 12F. Host Handles HWND/HDC, function addresses and callbacks do not cross the border. Global hooks are not installed, nor is there global input, clipboard, drag files, external desktop access, or GPU acceleration.

## Lot implemented

- A/W classes and windows, parents and owners, basic styles, positions,
  resizing, client/screen coordinates, order of presentation, focus, titles, painting area and cleaning on closure. BUTTON/STATIC themselves meet the basic controls used by Flappy; they do not constitute a complete package of common controls.
- Rows by thread, messages posted and sent between threads of the same
  process, execution of WndProc in the recipient thread and synchronized response. Identifiers of another process and kinship cycles are refused.
- BeginPaint/EndPaint, update and invalidation, rectangles, simple lines,
  text, clipping, brushes, pens, own fonts and small bitmaps. Selected GDI objects are accompanied and access to DLL status is synchronized. The bitmap Guardian Sans font replaces the requested families: metrics and appearance do not match Windows TrueType fonts.
- Basic dark state by DWM, dimensions and DPI fixed in 96. Other operations
  DWM, composition effects, complex regions and graphics formats outside the contract explicitly fail or are not exported.
- Physical keys down/up, scan code, modifiers and text are distinct events.
  TranslateMessage generates characters in the guest. Explicit Unicode text, mouse, focus loss, and closing follow separate contracts; there is no duplication of WM_CHAR by the host viewer. The automatic translation covers the keyboard subset described in the tests; complete layouts/IME stay out of this acceptance.
- traffic lights, duplicate pseudohandles, real affinity of the only vCPU,
  priorities, suspension and consultation/restoration from the context of own threads. Invalid restoration is refused before changing the context. Unwind records chained forward, issued by MSVC itself, are accepted with crossing budget; cycles do not allow undefined execution.

## Files and Settings

14.7 GPVs may include data files limited to `C:\App`, with .txt/.ico/.dat/.bin extensions and up to 8 MiB per input. Executable and runtime continue read-only. Small data files are initially copied to the guest disk without overwriting a persisted file. Larger features, such as the Flappy icon, are read from the catalog unchanging by validated buffers. There is no opening of host arbitrary paths on guest's behalf.

The changeable disk preserves the limits of 14.6: 64 KiB image, 32 nodes and up to 512 bytes per file. The new reader treats the end of the file without turning a read beyond the last byte into space error. Settings persist between sessions by `security-state.bin`; short or corrupted states are rejected. Full desktop snapshots are not yet supported.

## Observable limits

There are 32 windows, 16 thread queues, 64 messages per queue, 128 graphical commands per window and 32 timers. Windows can have up to 4096×4096, with limited coordinates and cut-out design to the desktop. Bitmaps/pictures transferred by GDI are limited to 64×64; accepted icons use the 32-bit BMP subset. A session has time limit and outputs from vCPU. Excess resources is error, not a successful confirmation.

The graphical session allows explicit retention of up to 65536 events in the host. The default limit and the previous policy contract remain preserved; the expanded capacity is selected before starting the session. There is no snapshot with this expanded capacity. Long or intense sessions can exhaust it: the records begin to indicate loss and the behavioral result becomes inconclusive. The acceptance explicitly verifies the absence of loss in each execution of the corpus. It does not prove coverage of all calls or complete malware classification.

The original MSVCRT used by Flappy still emits non-fatal diagnostics of unimplemented heat options and parameter of runtime C. The application continues to run in the accepted corpus; these paths are not presented as complete implementation of CRT. Audio, full menus, COM, WinRT, DirectX, frameworks, x86 and general compatibility remain in the following batches.

## Reproduction and acceptance criteria

`tools/desktop_runtime.py bundle` creates GPV with its own DLLs and legal local references. `guardian-desktop` accepts `--interactive` or an input script with time counted from the first query to the message queue. This milestone avoids delivering all controls during slow startup. The private package includes examples and instructions; it does not contain either the original Microsoft input binaries or Flappy.

The seven new tests are:

| Test | Evidence required |
| --- | --- |
| desktop_windows_demo | Classes, painting, text, coordinates and temporizadores no Windows native. |
| desktop_windows_threads | Execution of callbacks and replies in the recipient thread on Windows. |
| desktop_core | Property of windows, cycles, rows/quotas, keyboard, Unicode, clipping, timers, protocols and limited unwind. |
| desktop_viewer | Input typed out the viewer window, focus, closing and display of pixels. |
| desktop_runtime_tools | Exports/forwarders, data files, hashes and refuses to change the package. |
| desktop_runtime_integration | MSVC /MD applications, messages between threads, context, settings in two sessions and invalid states. |
| desktop_flappy | Original executionable in three sessions, drawn text, physical keys, moving windows, alternative settings and closing. |

The fixed SHA-256 from Flappy is `f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6`. The test changes only one copy of settings.txt: bird from 20 to 28 pixels, pipes from 80 to 64 and speed from 4 to 2, conferring the effect on the guest. The original files are checked again after the test. Closing respects the additional windows that the game itself opens when closing its elements.

`tools/validate_phase14_7.py` requires 116 Release regression tests and 41 Debug acceptance tests, either flawless or ignored, and verifies that sources, binaries and entries have not changed during execution. Results, test names and hashes are in validation.json files. The packer refuses incomplete or outdated evidence. The smoke test scans each ZIP file and executes the contents packaged in both settings with local entries.

## Re-evaluation of 12F

The matrix of this delivery provides the basis for starting a delimited 12F to Flappy and its own applications: external PE loading, guest callbacks, multiple windows, text, input, closure and validated graphic protocol. The decision depends on the Release/Debug acceptance recorded above.

The 12F is still unimplemented. It should create and clean the corresponding host windows, validate session isolation, window focus, mode, DPI/redimensionamento and bridge failures. The proof with a single viewer does not anticipate these results. The start requires a new user request.

Reference contracts: [WM_PAINT](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-paint), [keyboard entry](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-keyboard-input) and [unwind x64](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
