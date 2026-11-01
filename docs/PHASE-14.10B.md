# 14.10B — USER32 and native controls

Delimited Windows 10 x64 delivery. The new `--native-user32` mode uses `user32.dll` and `win32u.dll` itself in the MiniNT, a proper kernel and v2 protocol. SDK's usual imports are attended by this DLL. The restricted helper creates HWNDs and controls **BUTTON, REAL STATIC and EDIT** with Windows USER32. Its content does not come from images rasterized by MiniNT. The 12F presentation and the 14.10A explicit GDI protocol remain available in separate modes.

## Contract and implementation

- Application class registration, WNDPROC, userdata, subclassification of
  own windows and TIMERPROC remain in the guest. The native class of the main window is fixed. Only the three allowed controls are created as additional native classes. You do not load DLL or callback from the application in the host process.
- Creation/destruction, children and windows with owner, geometry, repairing
  of children, declared styles, visibility, driver's license, focus, coordinate conversion, invalidation and timers use real USER32. The initial modality is to disable the owner and create a possessed window.
- BUTTON accepted pushbutton, default pushbutton, checkbox and autocheckbox;
  STATIC accepts aligned text; EDIT accepts a line. Text is UTF-16, up to 255 units, with validated substitute pairs; ANSI adapters use CP1252. `WM_SETTEXT/GETTEXT/GETTEXTLENGTH`, `BM_CLICK/GETCHECK/SETCHECK` and `EM_GETSEL/SETSEL` have explicit contracts. Selection points stay in the guest; in the IPC only circulates the validated numerical result.
- `WM_COMMAND` changes the native HWND control by session ID.
  position transport WINDOWPOS fields, which is rebuilt in guest memory. No address of Windows structure is returned to the program.
- `SendMessage` for a WNDPROC of the same thread executes callback in MiniNT.
  Among threads, declared scalar messages use a related request: the sender processes synchronous calls received while waiting, including nested calls, and receives the result of the recipient WNDPROC. The deadline is **2 seconds**. Expire cancels the message still queued and refuses late response. Posted messages preserve FIFO by thread; synchronous calls and notifications of native operations are met before the return of the API.
- `GetMessage/PeekMessage/DispatchMessage` attends guest queues.
  native is already translated by the Windows loop; `TranslateMessage` does not duplicate characters. Timers use internal IDs in the helper and return the application ID; timer functions are executed in the corresponding invited thread. Callback test cancels its own timer, also checking this reentry; very short timers can saturate a consumer slower than the requested period.
- The native interface thread never calls a guest WNDPROC and does not wait
  your return. A transport thread delivers tasks to an internal window; this window also serves IPC during modal Windows drive loops. Supervision closes an auxiliary that stops responding to the IPC.
- The guest view uses **96 DPI logical**. The assistant converts geometry and
  input to the monitor pixels, meets native DPI changes and returns logical coordinates. This does not affirm equality of metrics between NT versions, sources or multiple monitor topologies.

The actual exports are in `guest/fixtures/NativeUser32.def`; the list is the batch contract, not a complete USER32 declaration. The adapters follow the documented contracts of [SendMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessage) and [control notifications](https://learn.microsoft.com/en-us/windows/win32/controls/control-messages) within the limits stated above.

## Isolation and closure

The process reuses the supervised launch of 14.10A: restricted token, low integrity, Job with a process and 64 private MiB, two inherited endpoints and environment containing only SystemRoot/WINDIR. The manifest fixes SHA256 from the auxiliary next to `guardian-desktop.exe`; does not choose an arbitrary executable. The profile is not AppContainer and does not prevent all file reading by the assistant.

The contract allows up to 32 windows, 32 timers, 256 events, 32 pending shipments and 65,536 operations per session. IDs are monotonic and linked to the session, process and thread. Operations with incompatible owner, expired ID, extra fields, unauthorised native message, invalid text or exceeded quota return error. HWND, procedures, GDI objects and host structures pointers are not IDs.

Completion and termination commands are unique to the kernel; direct application attempts are rejected and tested, including invalid request pointer. The kernel informs the end of each thread when assisting: its windows and timers are removed, and pending senders are misplaced. At the normal end of all processes, the kernel completes the channel; a guest failure is not converted to success. Auxiliary crash, VM deadline, and locked transport closes the Job. `native-user32.json` records operations, guest authors, errors, native calls, last response features, and auxiliary process result.

## Validation and use

The delivery criterion requires **147 Release** regression tests and **72 Debug** acceptance, flawless or ignored and with stable source code/binary. The four new tests cover contract, real restricted process, SDK programs within MiniNT and empacotamento/implementation. rejections The extracted package repeats previous graphical acceptance and new scenarios in both settings. Original reports accompany the private build.

The previous resource test was also corrected: the rejection of SMP now uses the RAM that the test itself has already verified as admitted. Ask 128 MiB fixed in this step could first produce a real memory budget error, masking the CPU diagnosis that the test intended to verify. The record of the first release regression with this failure is preserved in the package.

The new corpus contains two independent programs: controls/notifications and messages between threads, as well as input variants, failure and locking. Tests check real control classes, text in Windows EDIT, click, input by Windows messages directed exclusively to the controls themselves, synchronous return, reentry, timeout without late callback, payload WINDOWPOS, IPC in native modal loop, quotas, authorship, two sessions and cleaning after failures. The scenario between threads injects 200 Windows characters only into the window: it checks application messages and leaves the end of the thread remove the pending native input. A continuous input queue should not be emptied as output condition of this test. The report includes types of messages, flags and queue size to diagnose delays and ordering. No global injection of mouse/keyboard is used nor if it executes malicious sample.

Build with MSVC/Windows SDK, Python and legal references already used in previous phases. Aggregated target: `guardian_native_user32_runtime`.

```text
python -B tools/native_user32_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output pacote-guest
PASTA-BUILD/Release/guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-native-user32.gk pacote-guest/runtime.gpv pacote-guest/manifest.json pacote-guest/binding.gnt sessao-nova --native-user32 --seconds 8
```

Use new output directory. Microsoft CRT, legal reference schema, Flappy, ISOs, disks and personal states are not distributed in the build. ZIP contains only own binaries, tools, documentation and selected evidence.

## Limits and next batch

There is still no universal USER32, GDI of general applications, complete source code/pictures, menus, full dialogues, clipboard, drag-and-drop, IME, accessibility, additional common controls, custom draw, owner draw or DirectX in this mode. The native procedure of a control by the guest callback is not subclassified. Messages that require immediate response of the application during the native procedure need to be signed by themselves before being enabled.

Controls are from a line, no password; extra styles, native menus and arbitrary cursor/icon features and class brushes are rejected in the registry. Class creation accepts procedure and name, no extra bytes or classes styles. Show window does not force activation of another host application. Timer without HWND and automatic ID, change between top-level/child, owner swapping, AttachThreadInput policies, and manually posted key translation are not covered. Invalidation of the window itself uses Windows default background; BeginPaint/HDC and custom design are in **14.10C**, no silent fallback for pixels. Compatibility of another NT profile and fidelity on multiple monitors need specific comparison; running USER32 from the host does not prove both.

**Stop at completion of 14.10B. The 14.10C depends on new request.**
