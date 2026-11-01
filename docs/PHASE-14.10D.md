# 14.10D — Controls, dialogues and acceptance of the native GUI

Delimited delivery for x64 applications in the NT profile 10.0.19045. **--native-controls** mode uses actual Windows host windows, controls and GDI. The application, its DLLs and its callbacks remain in the MiniNT; the external EXE is not loaded into the host. The A/B/C variants and the 12F host are preserved.

## Contract and functions

The variant uses guardian-kernel-native-controls.gk, capacity GuardianNativeControls/4/guest, protocol v4 and guardian-native-controls-host.exeCLI checks kernel, mode and SHA-256 The aide. USER32/GDI32/WIN32U own forwarders and COMDLG32, COMCTL32 and DWMAPI meet the exports declared in the tables NativeControlsUser.def, NativeControlsGdi32.def and NativeControls-*.def.

- LISTBOX and COMBOBOX natives, with strings, selection and mediated messages.
- Text menus, one-level submenus and notifications in session windows.
- STATIC with icon; standard icons and ICO limited to 32-bit DIB up to 64×64.
  Decoding occurs in the guest; CreateIconIndirect receives a bitmap validated in the helper. These icons are treated as opaque in this batch; PNG and all ICO transparency combinations are not declared.
- WM_SETFONT/WM_GETFONT in the controls and WM_SETICON/WM_GETICON in the windows.
  Selected objects are retained until the reference is released.
- CreateFontA/CreateFontIndirectA and TextOutA with conversion CP1252. The catalog
  admits Arial, Consoles, Segoe UI, Sans Serif, MS Shell Dlg, MS Shell Dlg 2 and standard selection. The resolved face and metrics are Windows host.
- MessageBoxA/W and a GetOpenFileNameA/W clipping, with native controls and
  mode/callbacks no MiniNT.
- logical 1280×720 desktop, system colors/pins, extended styles
  allowed, session focus and dark theme attribute.

Declaring an export does not mean accepting all your options. Arbitrary messages, owner draw and guest pointers are not sent to controls.

## Archives, modality and presentation

The selector is a path input dialog in the guest namespace. Use CreateFile ed MiniNT to open existing files; does not use Shell browser or personal host disks. Cancellation and error are different, with CommDlgExtendedError and validation of buffers. Hooks, templates, multiple selection, rescue and full Shell navigation stay out of this clipping.

The dialogs disable the owner and restore his state upon termination. Procedure pointers and application status remain in the guest. Clipboard, drag/drop and arbitrary activation of COM/Shell are not released.

The logical view uses 96 DPI. The helper creates, moves, measures and paints the real HWNDs. Its explicit policy guest-bound-rectangles prevents automatic minimums of the host manager from expanding a rectangle requested by the guest, including before WM_NCCREATE and during drive. This meets the small Flappy windows. The SDK reference applies the same policy and gives creation and movement. It does not declare universal equivalence with the window management of a Windows installation; general WM_GETMINMAXINFO/WM_WINDOWPOSCHANGING callbacks do not receive host pointers.

Text, fonts and shapes are designed by GDI in the application host. Bitmaps/icons can cross the limited contract; the window is not rasterized previously in MiniNT. Fallback of 12F does not count as native acceptance. Versions of Windows host and target NT are recorded separately. Sources, substitutions, themes and DPI may vary; comparisons are valid for the registered machine, without automatic equivalence between all NT versions.

## Limits and isolation

Up to 32 windows/surfaces, customers of 1280×720, 128 GDI entries, 32 DCs, 24 MiB of pixels and 8 MiB sent per session, maintaining the bitmap and text limits of 14.10C. There are eight menus, 32 items per menu and 32 icons; menus, icons and timers share a ceiling of 64 entries. USER32 texts have up to 255 UTF-16 units; GDI texts, up to 127.

IDs are linked to the session, process and thread. Real HWND/HDC/HFONT/HICON, hands and host procedures do not cross the channel. Resources in DCs, controls or saved states cannot be deleted as referenced. Closing removes windows/controls before associated objects.

The assistant keeps token restricted, low integrity, Job of a process, 64 private MiB and two inherited endpoints. The D protocol waits use anonymous high-resolution thread timers, preserving size, sequence, consumption monitor, timeout and closure. The global resolution is not changed. These measures do not guarantee absence of escape: the auxiliary is not AppContainer and its profile does not prevent all file reading. See [SECURITY.md](../SECURITY.md).

## Acceptance and Use

Criterion: **158 Regression Release and 83 Debug acceptance tests**, without ignored and with stable source code/binary, plus extracted ZIP test. The six new tests cover protocol, real auxiliary, SDK reference, integration, Flappy and implementation/empacotamento. rejections

The corpus includes an application of controls/menus/selector, an ANSI editor, threads, the GDI scene of 14.10C and the original Flappy. The same SDK sources of controls/editor and the GDI scene also perform as native references, comparing results, sources, metrics, pixels and geometry.

Scenarios include confirmation/cancellation, native controls entry, mode, guest file and refusal of an existing file only in the host, resizing, competition, closing, fall, deadline and death of the auxiliary. Authorship, invalid types, selection, quotas, partial failures and leaks are verified.

Flappy remains unchanged, with SHA-256 f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6. These are three sessions: normal, alternative and normal configuration. Independent windows, movement, keys, captured scoreboard text from the window itself and cleaning are provided. EXE/assets Originals are not redistributed. Previous CRT diagnostics such as HeapSetInformation without support, they remain registered and do not equal a complete Windows runtime.

Reports native-controls.json, boot.json and analysis.json, references and own captures follow the evidence. Development errors are separated from the final matrix.

In Windows regression, the old package bond test started using application, DLL and kernel compiled from the project: the artificial PE container he created was blocked by the Defender (Win32 225 error). The protection remains active; the parser specific cases remain synthetic. Also a limited wait for the first visible presentation of the 12F was added, so the guest does not advance while his first windows are created; the following frames maintain asynchronous coalescence.

    Python -B tools/native_controls_runtime.py --build PASTA-BUILD --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output PASTA-BUILD/Release/guardian-desktop.exe PASTA-BUILD/guest/Release/guardian-kernel-native-controls.gk control package/runtime.gpv control package/manifest.json control package/binding.gnt session-new --native-controls --seconds 30

For Flappy, provide --application WAY-FLAPPY and --asset for settings.txt and icon.ico. Use new outputs. Target CMake: guardian_native_controls_runtime.

Universal USER32/GDI, all controls, full Shell dialogs, arbitrary sources and all NT profiles are not declared. GDI expansion follows on 14.10E; DirectX and media/audio stay on 14.10F/G.

**Stop when completing 14.10D. The 14.10E requires a new request.**
