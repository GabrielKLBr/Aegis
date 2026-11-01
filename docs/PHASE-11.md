# Phase 11 — Graphics, input and windows

Started on 18/10/2026 by explicit user request. The product continues Windows x64/MSVC/WHP; Linux development and testing use native ELF/KVM without Wine or QEMU. Phase 12 is not authorized or started.

## Windows fixes accepted on 19/10/2026

The eight Windows failures have been corrected. The full suite approved **52/52 in Debug and 52/52 in Release** without ignoring. The official result is `passed` / `awaiting_linux_acceptance`: the corrected review still needs Linux revalidation with the same hashes. Phase 12 has not been started.

Corrections, new tests, memory assistant requirements and checkpoint limits: [correction report](WINDOWS-PHASE11-FIXES-20261019.md). [previous diagnosis](WINDOWS-PHASE11-VALIDATION-20261019.md) has been preserved. The following sections record the accepted Linux revision in 18/10; its Windows to-ed represent that moment.

## Final Linux Acceptance

**Implementation of own matrix completed; Linux/KVM approved in Debug, Release and Sanitize. Native acceptance Windows/MSVC/WHP pending. Phase 12 was not started.**

Evidence: `build-phase11/validation-20261018-191149-4baab81b/validation.json`. `passed` / `awaiting_native_windows_acceptance` status. The fonts were recompiled in new directories and their hashes remained equal until the end. There was no Wine/QEMU, reused binaries or ignored tests.

| Configuration | CTests | Boots/resumes graphs | Pauses graphics | Suite |
| --- | --- | --- | --- | --- |
| Debug | 32 approved | 15 | 3 | 433,33 s |
| Release | 32 approved | 15 | 3 | 305,55 s |
| Sanitize (ASan/UBSan) | 32 approved | 15 | 3 | 534,43 s |

Totals: **96 plays CTest, 132 old regression boots, four correctly rejected fatal/IST controls, 45 boots/resumes graphics and nine graphical pauses**. Each setting includes 9136 GUI core checks and 20,000 hostile commands, eight independent pixels and complete hash checked by Python oracle, cloning/resume/script linked to snapshot and native software viewer. No ASan/UBSan. alert

The old matrix remained approved: storage and recovery of 52 fault prefixes, catalog and their aborted transactions, 81 boots/resumes console snapshots and 27 pauses, 24 bootes of its manager, 72 boots/resumes network and its captures IPv4/IPv6. These values are identified in the reports by configuration and do not replace native Windows acceptance.

19 Windows modules were prepared with LLVM ELF producing COFF and sources identical to those accepted. There was no Windows execution. To formally close, run `python tools/validate_phase11.py` on Windows x64/MSVC/WHP: Gate requires 52 CTests on Debug/Release, including Microsoft comparisons of 153 handlers and 59 unwind cases. Host acceptance is only combined with hashes from corresponding sources.

The intermediate milestones below are historical; the results of this section constitute the final Linux acceptance.

## Profile implemented

The reference of phase 0 is a graphical application of its own. This implementation offers a CPL3 application with two tools/windows: Paint draws when clicking, and Notes shows the last character received by the keyboard. The two tools use the explicit graphical API of the project; do not matter USER32/GDI and do not prove binary compatibility with arbitrary Windows graphical applications. The matrix is deliberately small: bitmap BGRA opaque 320×240, software rasterization, four windows, ASCII text with 5×7 font itself (letters/digits/space; lowercases drawn as capitals; too printable use the replacement glyph `?`), rectangles, small bitmaps, focus, overlay order, internal cursors, modal dialogue and logical DPI 96/144/192. there is no announced GPU.

Manager and rasterizer stay in the guest kernel. Syscalls validate and copy 160 byte commands and return 32 byte events within the user data page; kernel pointers, overflow and out-of-range data are rejected. The CPL3 program creates windows, draws and processes actual channel events. They are not images drawn in advance by the host.

The release 1 device uses the 0xF3 port and private kernel pages: 0x135000 package, read-only enumeration 0x136000, 0x137000 report and 0x140000–0x18AFFF (307200 bytes). The host validates session, sequence, dimensions, stride, size, opacity, limited ASCII titles, IDs, geometry, focus, and modal kinship before presenting a copy. It does not receive HWND, callbacks, pointers, or requests to operate external windows.

Each window holds a maximum of 32 drawing operations. A fill of the entire client starts a new list; other excesses return Full. There are 16 events, 256 window creations per session, 8192 device changes and 30 seconds in the interactive viewer. These limits are explicit; they are not promises to run programs or long sessions.

## Interactive channel

`guardian-gui` opens a single controlled native window. Windows uses a Win32 and StretchDIBits window; Linux uses SDL2 with software renderer. The guest composer draws its windows inside that surface. Entry is only accepted for the identifier of this window and while it has focus; coordinates are converted to the virtual desktop. Loss of focus clears pending entry in the guest. There is no global hook, global capture, artificial cursor movement, key injection in the host or forwarding titles to new real windows.

Clipboard, drag files, printer, USB, shared drives and access to host windows have separate operations and return Denied. The permission mask is zero; activation of these features is not yet implemented. Keyboard supports ASCII and Backspace/Tab/Enter/Escape; there is no IME, General Unicode, international layouts, global shortcuts, mouse capture or border drag. Cursors are drawn on the surface itself. API DPI is logical; it does not announce complete parity with Windows DPI awareness or USER32.

## Implementation

Linux, after compiling images and tools:

```sh
./guardian-gui guardian-kernel-graphics.gk evidencias
./guardian-gui guardian-kernel-graphics.gk evidencias --interactive
```

The first command executes the reference sequence with explicit input and saves `boot.json`, `desktop.ppm` and `graphics.json`. The second accepts interaction only in the created window. Enter or one click does not require dialog; click Paint to draw and Notes to focus before typing. Escape or close the window ends. The session that exceeds 30 seconds is diagnosed as timeout.

On Windows, the same executable is built by CMake/MSVC; it needs native validation of this revision. On Linux, SDL2 in runtime and its development headers enable the viewer. Without headers, the graphical execution without window remains available, but the full phase 11 gate refuses a suite without the viewer test. The development cache used on this machine is out of the project, in `~/.cache/guardian-native-deps/sdl2`.

## Manager and snapshots

`guardian-vm create ID IMAGE graphics` admits the new profile without network. The worker performs the reference sequence without window, with separate history in `graphics-history/<token>` and 64 session quota. Snapshots store RAM from the composer, pixels, sequence and position of the deterministic input. Restoration requires exactly the same script; clone and resume produce the same bitmap. The format of the old profiles remains compatible.

Interactive snapshots are explicitly rejected: external checkpoint events do not have an implemented replay semantics. The interactive window is started directly by `guardian-gui`; a window connected to a background worker is not promised. New profiles combining GUI, PE, network and disk require additional integration; the current `graphics` contract is distinct.

## Validation

Gate requires new Debug/Release and host builds with ASan/UBSan, 32 CTests per Linux configuration (52 on Windows), 132 old bootings, fatal/IST controls, and preservation of network tests, processes, disk, catalog, and snapshots. Graphic tests include invalid entry, clipping, quotas, useful life, modal/focus, pointers, 20,000 hostile commands, pixels and hash verified by independent Python code, eleven boots/resumes and three set-up pauses (including two pre-set closures), plus three managed boots, and one run of the reference program.

The pre-compilation of Windows branches on Linux is preparation, non-acceptance MSVC/WHP. The final Linux results are at the beginning of this document; Windows gate remains pending.

## Recovery and development checks

The original 350 source code/documents were copied before any change to `snapshots/codex-before-phase11-20261018`, with SHA-256 manifest. Packages and evidence from the previous phases were preserved. Compilation logs and preliminary tests remain in `build-phase11/development-logs`; they do not replace reconstructed acceptance.

The prechecks demonstrated the two CPL3, nine boots/resumes tools and three pauses with equal pixels, three managed executions and refusal of a different input script. The tests were expanded to close during the dialogue, including after loss of focus. There were corrections of test expectations (coordinated over a glyph and sequence counter of a distinct instance) and use of CLI (positional contract and clone resume with checkpoint). The Windows branch was prepared with wide cursor identifier and COFF compilation; it was not executed by Linux.

The viewer test has also passed on the available native video server, opening/closing only your own window. The automated suite uses the SDL dummy driver to repeat drawing and filter identifier without depending on a desktop. The input of application tests is routed via the device channel; it is not a physical keyboard certification, IME or all Linux window managers.

The time limit for the complete snapshots of the previous phases has gone from 240 to 360 seconds: the accepted evidence of phase 10 already consumed 232.45 seconds in Sanitize. The execution limits of the guests and success criteria have been preserved. The sources are frozen during the final matrix.

Reference of the SDL API used by the viewer: [event file](https://wiki.libsdl.org/SDL2/SDL_PollEvent) and [ Window identification](https://wiki.libsdl.org/SDL2/SDL_GetWindowID).

## Preserved platform limits

The second-stage physical execution protection remains different between hosts: WHP controls R/W/X, while the KVM backend declares `secondStageExecuteProtection:false` and uses NX in the guest tables. This phase does not change this limitation. There is no SMP, 3D acceleration, DirectX/OpenGL, import of GPU driver, binary API USER32/GDI or full Windows profile. The Win32 font of the viewer does not make these features available for the invited application.

| Reference graphics | Contract implemented | Evidence required |
| --- | --- | --- |
| Paint own x64/CPL3 | Rectangles, text, bitmap and click | Pixel designed and complete hash |
| x64/CPL3 own notes | Text ASCII, focus and DPI 144 | K character; focusless keys discarded |
| Modal own dialogue | Block Dad., accepted waiver/closing | Focus refused; closing after blur works |
| Visualizador Linux | SDL2/software, a controlled window | Presentation and process ID filter |
| Visualizador Windows | Win32/software Window | Compile in precheck; native execution pending |
| Generic Windows GUI PE Programs | Out of the current matrix | Not advertised as compatible |

## First result of the reconstructed matrix

Debug approved the 32 CTests and 66 regression bootes of the previous phases. The graphical core approved 9136 checks and 20000 hostile commands; the graphics executed eleven boots/resumes and three pauses, three managed boots and an execution with independent Python oracle. Release and Sanitize remain running; this isolated milestone does not end acceptance.

## Unit of implementation of the reference

Paint and Notes are two tools in the same reference program, executed by a CPL3 entry in the CPU profile. They share the address space; they are neither two PE processes nor two independent USER32 executables. The `applications=2` field of the report counts these two window workloads, not system processes. The initial matrix of phase 0 requires a specific GUI application. The “two applications” names present in the test outputs identify the two tools of this reference; they do not prove isolation between graphical processes. The isolation between phase 5 processes remains tested in their separate contract.

## Second result of the reconstructed matrix

Release also approved 32 CTests and 66 regression boots. The matrix already sums up 64 CTests and the 132 predicted old boots, with equal pixels in the graphics resumes and three executions managed by configuration. The host with ASan/UBSan is running; the final acceptance still depends on this round. The previous `preview.png` in the evidence folder was generated from the pixels produced by the guest Release and conferred by the independent oracle.

The reference bitmap coincides byte to byte between Debug and Release (PPM SHA-256 `4d26b5738133e4946a3d86aa6b7f246c25561d0a77df9f0c4c14ad4ded3c5c9b`). This includes focusless key disposal and resumption; it does not constitute graphical compatibility with other applications.

## Delivery and Recovery

The revisionable changes are in `changes.patch` and `changes.json` in the evidence folder. There is documentation manifest, Windows precheck and `accepted-sources.tar.gz` file with exact copy of the fonts and hashes per file. The previous copy of 350 files remains available in the snapshot directory.

The packer is `python3 tools/package_phase11.py EVIDENCE`. It requires approved acceptance and the same hashes, refuses to overwrite existing packages and includes four ELF executables, 15 images (13 of production and two fatal controls), reference captures, documentation and SHA-256 manifest. The package is built on this Linux Mint 22.3/GCC 13 machine; portability for other versions of libc/distributions has not yet been certified.

## Verified Linux Package

File: `dist/guardianav-phase11-linux-x86_64-20261018.tar.gz` (approximately 1.8 MiB), with SHA-256 `18e53606193f319d828573f8d92f46ea780638cf86b3b566194cd40f4f78f749`. The `.sha256` file is next door.

Package was extracted in temporary folder outside the project. Checked 97 files by manifest and the four ELF executables, with 15 guest images. Passed the native scanner, the oracle of pixels of the application, three boots managed with clone/restoration, the native viewer and two console boots complete with persistence (sequence recovered and base intact). Wine/QEMU Evidence: `package-validation.json`, `package-commands.json`, `package-graphics/`, `package-graphics-manager.json` and `package-complete-0/1.json`.

Delivery includes 37 revisionable textual changes, the 350 previous files intact in snapshot and an exact font file with 369 files. The source copy and internal package documentation represent the time of packaging; this external record adds the hash of the package and its extraction test to avoid circular hashe reference. Hashes from all accepted sources remain the same.

**Stop:** Phase 11 is implemented and approved for the own matrix on Linux. Formal completion depends on 52 CTests per configuration on native Windows. Phase 12 has not been started.
