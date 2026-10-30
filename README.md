# Aegis

**Advanced Endpoint Guardian for Inspection & Sandboxing**

Translated all the way to [GuardianAV Project History](README.md#guardianav-project-history) using [DeepL Translator](https://www.deepl.com/translator) since I ran out of weekly limit.
Please open a Pull Request reporting any English mistake if you find one.

[![Windows API Coverage](https://img.shields.io/endpoint?url=https%3A%2F%2Fgabrielklbr.github.io%2FAegis%2Fbadges%2Fgeral.json)](https://gabrielklbr.github.io/Aegis/windows-apis.html)
[![Graphics and Audio Coverage](https://img.shields.io/endpoint?url=https%3A%2F%2Fgabrielklbr.github.io%2FAegis%2Fbadges%2Finterface.json)](https://gabrielklbr.github.io/Aegis/progress.html)

Percentages of entries in the catalogs, including partial support; do not represent the entire Windows system.

Antivirus and inspection/sandbox lab in C/C++, with the guest's own implementatio
the product supports Windows/MSVC/WHP and native Linux/KVM validation
the names of the `guardian-*` executables remain compatible

**Phase 16** adds protection for the Windows service, persistent queue, scheduler,
notifications, background scans, and a SYSTEM-protected vault:
[usage, validation, and limits](docs/PHASE-16.md).

**Phase 15** replaces simple quarantine with authenticated transactional storage,
featuring verified isolation, non-overwriting restoration, history,
and integration with the engine: [scope, validation, and limits](docs/PHASE-15.md).

The set of COM/RPC components, Shell, and MiniNT controls is described in the
[Phase 14.8 scoped delivery](docs/PHASE-14.8.md), with explicit acceptance criteria
and limitations.

Phase 14.9 adds contained network, HTTP, TLS 1.2 in the guest, CNG, and
virtual key/certificate stores: [scope and validation](docs/PHASE-14.9.md).
The implementation supports a limited set of Windows 10 x64 systems.

Phase 12F adds an optional mode for independent windows on the Windows desktop,
with content rendered by MiniNT and input restricted to the session:
[implementation, usage, and limitations](docs/PHASE-12F.md).
**14.10A** adds the first GDI command and drawing bridge via a
restricted Windows helper: [scope, usage, and limitations](docs/PHASE-14.10A.md).
**14.10B** adds custom USER32 imports, as well as real windows and BUTTON/STATIC/EDIT controls with callbacks in MiniNT: [specification and tests](docs/PHASE-14.10B.md).
**14.10C** adds GDI drawing, fonts, and bitmaps rendered by Windows,
with a comparison of the same application running on MiniNT and natively: [specification and tests](docs/PHASE-14.10C.md).
**14.10D** adds controls, menus, and dialogs in the guest namespace and
support for Flappy with a native GUI: [specification and tests](docs/PHASE-14.10D.md).
**14.10E** extends curves, regions, paths, transformations, bitmaps, and text in host windows, with an independent corpus and API matrix: [contract and tests](docs/PHASE-14.10E.md).
**14.10F** adds software-based Direct2D/DirectWrite, PNG/BMP/JPEG, and
limited D3D11 WARP/DXGI texture operations:
[specifications, tests, and pending items](docs/PHASE-14.10F.md).
Shaders, the 3D pipeline, and full DirectX remain pending.

The initial release of **14.11** adds limited console CIL, scalar
WinRT contracts, and portable installation in the guest:
[scope, reproduction, and open issues](docs/PHASE-14.11.md).
The next release adds selected Form, Button, Label, and TextBox controls, with
real windows/controls on Windows and interpreted events on MiniNT.
Full CLR and WinForms, WPF, WinUI, and MSI installers remain pending.

The first release of **14.12 (14.12A)** adds x86/PE32 execution on the x64 kernel,
native 32-bit DLLs, and native USER32 windows for NT 10.0.19045:
[scope, validation, and follow-up](docs/PHASE-14.12.md).
Full WOW64 support, an independent GUI corpus, and additional NT profiles remain open.

**14.13** connects the queue to the runtime via SHA-256-linked packages and
adds execution recovery with basic x86/x64 USER32 windows:
[usage, testing, and limitations](docs/PHASE-14.13.md).
Snapshots with GDI/DirectX/audio and open connections remain pending.

Update **14.13A** adds automatic packages based on local templates,
progress tracking, and save/resume functionality via the app and updated services:
[usage, validation, and results with real programs](docs/PHASE-14.13A.md).

The **14.14** evaluation covers 32 applications and publishes the matrix by profile and
architecture. In the Win10 automated workflow, 1 completed its workflow, 1 was partial,
and 30 encountered blockers; Win7/Win11 were not evaluated. The goals for
broad compatibility remain **unmet**:
[method, results, and readiness for Phase 15](docs/PHASE-14.14.md).

Public repository with source code, tests, configuration, and documentation. Builds,
releases, executables, and virtual disks are not included in Git. Some tests require
recreating their compiled data: [preparation from source](docs/GITHUB-AEGIS.md).

To manage Git, repositories, branches, and PRs:

```text
python tools/aegis_git.py
```

The menu also generates reviews of isolation and sandbox consistency tests.
These preserve actual errors and reports; they do not guarantee undetectability.
[Automation and usage](docs/GITHUB-AEGIS.md) · [Security](SECURITY.md) ·
[Current status and limitations](docs/PHASE-13.md) · [Roadmap](docs/ROADMAP-COMPLETO.md)

**Windows Review on 10/23/2026:** Phases 12B–13 received fixes and passed
the MSVC/WHP test matrix: **80 distinct Debug tests**, with documented retests,
and **80/80 Release** in a complete suite, with no tests skipped.
[Fixes, evidence, and limitations](docs/REVISAO-WINDOWS-FASES12B-13-20261023.md).
The PE/SMP integration and specific NT references remain open.

**Phase 14 on 10/23/2026:** shared scanner with YARA, hashes, PE analysis,
limited scripts/containers, SQLite cache, hash-based reputation connectors, and
queue for MiniNT. Offline mode by default; unknown does not mean safe.
Windows: **83/83 Release** for the full suite and **3/3 Debug** for the scanner; Linux
Debug: engine/pipeline **2/2** and CLI **1/1**, with partial scope documented.
[Usage, configuration, local deployment, and limitations](docs/PHASE-14.md).
Phase 15 has not been started.

The records dated below document the historical evolution of GuardianAV.
The compiled builds mentioned in them remain local and are not part
of this source repository.

> Phase 13 on October 22, 2026: telemetry, policies, and behavioral analysis implemented and accepted on Linux — 59 Debug, 59 Release, and 35 ASan/UBSan, without Wine. Nine domains on the timeline, versioned rules, allow/deny/simulate, JSON/HTML, and state differences. [Logging, delivery, and limits](docs/PHASE-13.md). Native Windows acceptance and the previous requirements remain open; Phase 14 has not begun.
>
> Phase 12E on 10/22/2026: Synthetic profiles implemented and accepted on Linux — 56 Debug, 56 Release, and 34 ASan/UBSan, without Wine. Consistent accounts, documents, reader, associations, dates, and history, with persistence and privacy testing. [Logging, delivery, and limits](docs/PHASE-12E.md). Native Windows acceptance and previous open issues remain open; Phase 13 has not begun.
>
> Phase 12D on 10/21/2026: sessions, resumption, and interaction implemented and accepted on Linux — 53 Debug, 53 Release, and 33 ASan/UBSan, without Wine. [Registration, commands, and delivery](docs/PHASE-12D.md). Windows acceptance of this revision and the previous open issues remain pending.
>
> Phase 12C on October 21, 2026: Linux implementation and validation approved — 50 Debug, 50 Release, and 32 ASan/UBSan; 1,590 compliance observations. [Matrix, delivery, and open items](docs/PHASE-12C.md).
> Phase 12B retains its open items; the approval of Phase 12C does not mark them as closed.
>
> Continuation of Phase 12B on October 20, 2026: [implemented features, validation, and open items](docs/PHASE-12B-FEATURES.md).
> The phase remains open; Windows/MSVC/WHP acceptance of the new revision is required.

## GuardianAV Project History

**Phase 12B in progress as of October 20, 2026:** cross-catalog resource allocation, expanded RAM with a reserve for the host, and storage limits that include snapshots. The release is undergoing validation; SMP and large volumes remain pending. [Scope and status](docs/PHASE-12B.md).

**Phase 12A completed for the reference matrix on October 19, 2026:** consistent identity across CPUID/firmware/registry, audited INF packages, and a custom driver loaded, tested, and unloaded on the guest, with snapshots. Windows: 59 validated tests per configuration; Linux: 39 per configuration and 27 with sanitizers, considering the documented retests. Commercial drivers and general Windows compatibility remain limited. Phase 12 remains open; 12B is in progress, and 12C–12E and Phase 13 have not yet begun. [Scope, evidence, and limitations](docs/PHASE-12A.md).

**Windows fixes validated on October 19, 2026:** all eight vulnerabilities have been fixed.
The complete suite passed Debug and Release: **52/52 in each configuration**,
with no skips. The fixed revision is still awaiting Linux revalidation.
[Results, fixes, and limitations](docs/CORRECOES-WINDOWS-FASE11-20261019.md).

The Linux logs below describe the revisions accepted on October 18; the references
to pending Windows acceptance in them are historical. The historical status is in the
report above. Phase 12 was subsequently approved on October 19; its current status is at the top of this page.

**Phase 11 implemented and accepted on Linux:** a custom application in CPL3 with
Paint/Notes windows, software-based drawing, focus, dialogs, DPI, and input via the
native viewer. It passed 96 CTest runs and 132 regression boots,
without Wine/QEMU. **Native Windows/MSVC/WHP acceptance pending.**
Evidence, commands, and limits: [PHASE-11.md](docs/PHASE-11.md).
Extracted and tested package: `dist/guardianav-phase11-linux-x86_64-20261018.tar.gz`.
Phase 12 has not yet begun.

**Phase 10 implemented and accepted on Linux:** IPv4/IPv6 UDP virtual network,
simulated DNS/echo, isolated lab, explicit proxy, and VM-based captures.
The default profile is network-less. It passed 81 CTest runs and 132 regression boots,
with memory checkers, without Wine/QEMU. **Native Windows acceptance pending.**
Commands, evidence, and limits: [PHASE-10.md](docs/PHASE-10.md).
Package: `dist/guardianav-phase10-linux-x86_64-20261018.tar.gz`. Phase 11 was subsequently approved and is documented in [PHASE-11.md](docs/PHASE-11.md).

**Native Linux available (10/18/2026):** signature scanner, SHA-256,
image inspection, and kernel tests compile as ELF, without Wine.
Run `./Build-Linux.sh Release`; the program is `build-linux/Release/guardianav`.
The guest now also runs on KVM, with native recompilation on Linux and without Wine/QEMU.
The Windows/MSVC/WHP workflow remains unchanged. See [LINUX-KVM.md](docs/LINUX-KVM.md).
Commands, limits, and validation: [LINUX-NATIVE.md](docs/LINUX-NATIVE.md).

**Phase 9 implemented and accepted in the Linux matrix:** full state snapshots, pause/resume,
cloning, restart, and disposable/persistent disks are implemented in the
new `guardian-vm`. Tests for concurrent VMs, recovery after failures, and
isolation between VMs are included. The build passed 69 CTest runs and 132 regression boots; native Windows
acceptance remains pending. Commands, limits, and evidence:
[PHASE-9.md](docs/PHASE-9.md). Package: `dist/guardianav-phase9-linux-x86_64-20261018.tar.gz`.
Phase 10 was subsequently authorized; Linux implementation and acceptance documented in [PHASE-10.md](docs/PHASE-10.md).

**Phase 8 — implementation completed for native consoles; Windows acceptance
pending (10/18/2026).** The guest loads PEs/DLLs, uses files,
the registry, the heap, locks, threads, and child processes. It now handles x64 exceptions in user mode,
performs non-local unwind with `__finally`, and loads/unloads DLLs with TLS,
references, and rollback. The Linux/KVM matrix passed **132 boots and 60
CTest runs**, including memory checks and the previous milestones.

The product remains in the Windows/MSVC/WHP phase. The following items still need to be completed in this release for native acceptance:
**40 CTests in Debug/Release and comparisons with Windows APIs**.
Phase 8 has not yet been completed on Windows. Phase 9 was authorized subsequently. Code, criteria
per application, evidence, limits, and resumption command:
[PHASE-8-COMPLETION.md](docs/PHASE-8-COMPLETION.md).
Documents from previous milestones remain in `docs/`.

**Phase 7 completed:** Files and the registry survive
reboots on a dedicated, read-only volume with independent overlays.
Permissions, sharing, renaming, deletion, file links,
registry notifications, flushing, and recovery of aborted commits are supported.
Validation passed 784 volume checks, 32 host persistence checks,
80 Phase 7 boots, an additional 480 regression tests for Phases 1–6, 31 tests, and seven
examples per configuration in Debug and Release, with no tests skipped.
Code, commands, and limits: [PHASE-7.md](docs/PHASE-7.md).
Previous steps: [PHASE-6.md](docs/PHASE-6.md), [PHASE-5.md](docs/PHASE-5.md), [PHASE-4.md](docs/PHASE-4.md), [PHASE-3.md](docs/PHASE-3.md), [PHASE-2.md](docs/PHASE-2.md),
[PHASE-1.md](docs/PHASE-1.md), and [PHASE-0.md](docs/PHASE-0.md).
MiniNt remains the reference on the host.
**Windows acceptance of phase 8 pending; phase 9 approved on Linux, pending on Windows.** Order and stop rule:
[ROADMAP-COMPLETO.md](docs/ROADMAP-COMPLETO.md).

**Core Update (10/16/2026):** This implementation is the active foundation for
development, using WHP directly, without QEMU. In addition to its own heap,
`MiniNt` now handles memory reservation, commit, decommit, and deallocation, queries
regions, and enforces read, write, and execute permissions via WHP. Section views
share address control with private allocations.
There is also a single table of handles with rights and references for files,
sections, events, keys, threads, mutexes, and semaphores. The cooperative scheduler
allows waiting on events, threads, mutexes, and semaphores, delays, and resumption with
its own context on the same vCPU, including mutex transfer and release.
Instructions, tests, and limits: [KERNEL-SYNCHRONIZATION.md](docs/KERNEL-SYNCHRONIZATION.md),
[KERNEL-THREADS.md](docs/KERNEL-THREADS.md),
[KERNEL-OBJECTS.md](docs/KERNEL-OBJECTS.md), [VIRTUAL-MEMORY.md](docs/VIRTUAL-MEMORY.md), and
[KERNEL-CONTINUATION.md](docs/KERNEL-CONTINUATION.md).
`Build-Kernel.ps1` compiles this step without overwriting previous builds.

**(OUTDATED)** A small, real, **teaching** antivirus for Windows written in C/C++ using only
the Windows SDK (no external dependencies). It includes both a **command-line
scanner** (`av.exe`) and a **hardware-accelerated desktop UI** (`avui.exe`).

[**Phase 17**](docs/ROADMAP-COMPLETO.md#fase-17--aplicação-winui-3-e-internacionalização) will bring **WinUI 3** interface using **Windows Fluent Design**.

![Dashboard](docs/dashboard.png)

| Scanning (live) | Detections | Settings |
|---|---|---|
| ![Scanning](docs/scanning.png) | ![Results](docs/scan-results.png) | ![Settings](docs/settings.png) |

## The UI

The desktop app is rendered with **Direct2D** (GPU-accelerated 2D vector
graphics) + **DirectWrite** (text). That means it is hardware-accelerated
through Direct3D under the hood, but contains:

- **no web/browser engine** (no Electron, WebView2, or CEF), and
- **no raw OpenGL / DirectX 3D / Vulkan** — no shaders, swap chains, or pipeline
  code. Direct2D is the high-level 2D API native apps use for crisp, accelerated
  UI. (It's part of the DirectX *family*, but not the "pure 3D" layer.)

Everything you see — rounded cards, the shield badge, the spinning scan
indicator, toggle switches, icons — is drawn as vector geometry each frame, so
it stays sharp at any DPI. The UI runs the **same scan engine** as the CLI; a
background thread scans while the UI thread animates at ~60 FPS.

It demonstrates how a real AV product is structured:

- **Signature scanning** — SHA-256 hash matching (via Windows CNG/BCrypt) and
  content/substring rules, loaded from `signatures.txt`.
- **EICAR detection** — recognizes the industry-standard, *harmless* antivirus
  test file, so you can prove detection works without any real malware.
- **Quarantine** — isolates detected files by XOR-neutralizing their bytes,
  storing them with metadata, and deleting the original.
- **Real-time protection** — watches a folder with `ReadDirectoryChangesW` and
  scans files as they are created or modified.
- **Windows Security Center integration** — reads the exact antivirus list the
  Windows Security panel shows (WMI `ROOT\SecurityCenter2`) and contains the
  documented registration path.
- **Dynamic-analysis sandbox** — *detonate* a file and watch what it does
  (process tree, file activity, resource usage) without harming the PC. See
  below.

## Dynamic-analysis sandbox

![Sandbox report](docs/sandbox-report.png)

Two tiers of isolation, both reachable from the **Sandbox** page (or the CLI):

- **Lightweight in-OS sandbox** (`av sandbox <file>`) — runs the file in a
  tightly restricted container and records its behavior:
  - **Job Object**: kill-on-close, memory cap, process-count cap, full UI
    restrictions, auto-timeout.
  - **Restricted, low-integrity token**: privileges stripped, admin membership
    dropped; the process can only write inside an isolated low-integrity working
    directory — attempts to write elsewhere are blocked by Windows.
  - **Hidden desktop**: any windows it creates never touch your session.
  - **Monitoring**: process tree (via a Job completion port) + file activity
    (via `ReadDirectoryChangesW`), plus CPU/memory/IO accounting.

  This is fast triage, **not** bulletproof. Advanced malware can detect or
  escape a user-mode sandbox — so for real samples, use the VM tier:

- **Windows Sandbox (disposable VM)** (`av sandbox-vm <file>`, or *Open in VM*) —
  hands the sample to the OS's built-in Hyper-V sandbox with **networking
  disabled** and the file mapped **read-only**. The VM is thrown away on close.
  This is genuine isolation. (Requires the Windows Sandbox feature: Windows
  Pro/Enterprise + virtualization.)

Verify it safely with the bundled **harmless** sample (it drops files, spawns a
child, and *tries* to escape to `%TEMP%` — which the low-integrity container
blocks):
```
build\av.exe sandbox build\sample_behavior.exe
```

## ⚠️ About showing up in the Windows Security panel

You asked for the Windows Security ("Defender") panel to display this product as
the active antivirus. That specific outcome is **gated by Microsoft** and is not
achievable with a normal program. To register a third-party AV (and have Defender
stand down), Windows requires the product to:

1. Be a member of the **Microsoft Virus Initiative (MVI)**.
2. Ship an **ELAM** (Early-Launch Anti-Malware) driver so its service runs as a
   **Protected Process Light (PPL)**.
3. Be **code-signed** with the certificate Microsoft issues to MVI members.

The registration API (`WscRegisterSecurityProduct` in `wscapi.dll`) returns an
access/privilege error for any unsigned, non-PPL process. This is an intentional
security boundary — it is exactly what malware tries (and fails) to bypass, so
there is no supported workaround. `av register` runs the documented call and
reports precisely why Windows accepts or rejects it. `av status` shows the live
contents of the panel's data source.

## Build

You need **CMake 4.0+**, a **C++20** compiler and **Python 3.9+** for the guest
object audit. The phase 0 toolchain is **MSVC x64 / Visual Studio 2022**.
Host-only MinGW builds can disable the MSVC guest contract with
`-DGUARDIAN_BUILD_GUEST_CONTRACT=OFF`; that path is not part of phase 0 validation.
CMake builds both `av.exe` (CLI) and `avui.exe` (UI).

### Recommended — CMake
```sh
# MinGW + Ninja:
cmake -S . -B build -G Ninja -DGUARDIAN_BUILD_GUEST_CONTRACT=OFF
cmake --build build

# or Visual Studio (MSVC):
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Binaries land in `build/` (`signatures.txt` is copied next to each).

### Legacy batch scripts
`build.bat` and `build-mingw.bat` are preserved older CLI helpers, with C++17
flags and source lists that predate the current sandbox dependencies. Use the
CMake commands above for the current project and phase 0 validation.

## Run the UI

```
build\avui.exe
```
Click **Quick Scan** (scans your Downloads folder) or **Choose folder…** to pick
any target. Detections appear live on the **Scan** page. The **Sandbox** page
detonates a chosen file (or the bundled test sample) and shows its behavior, or
hands it to a disposable VM via **Open in VM**.

## Usage

```
av scan <path>         Scan a file/folder recursively (report only).
av clean <path>        Scan and auto-quarantine detections.
av monitor <folder>    Real-time protection on a folder.
av status              List antivirus products Windows Security shows.
av register            Attempt WSC registration (explains the MVI rules).
av quarantine          List quarantined items.
av sandbox <file> [s]  Dynamic analysis in the in-OS sandbox (timeout s sec).
av sandbox-vm <file>   Run a file in Windows Sandbox (disposable VM).
av vmcheck             Probe MicroVM/WHP virtualization readiness + how to fix.
av detonate-micro [f]  Detonate a PE in the from-scratch MicroVM NT personality
                       (no file = built-in sample). Wraps microsandbox.exe.
av eicar <file>        Write a harmless EICAR test file.
```

## MicroVM sandbox (in progress)

A from-scratch "Wine-in-reverse" sandbox with a thin hypervisor floor is
designed in [`docs/ARCHITECTURE-microvm-sandbox.md`](docs/ARCHITECTURE-microvm-sandbox.md).
The first pieces of that engine are implemented:

- **`av vmcheck`** — the engine's entry gate. It probes (no admin needed)
  whether the **Windows Hypervisor Platform (WHP)** is usable on this machine
  via `WHvGetCapability`, detects likely blockers (BIOS VT-x off, WHP feature
  missing, `hypervisorlaunchtype=off`, a conflicting VMware/VirtualBox install),
  and prints an ordered remediation plan.
- **`vmpoc.exe`** — roadmap step 1. A minimal WHP harness that creates an
  EPT/SLAT-isolated partition, maps guest RAM at a guest-physical address,
  runs a 13-byte 16-bit guest, and **intercepts the guest's port I/O as
  VM-exits** on the host — the seed of the paravirtual device layer
  (`PvConsole`/`PvDisk`). If the hypervisor is off it stops cleanly and points
  you at `av vmcheck`.

- **`vmdemo.exe`** — roadmap step 2 (device layer). Builds on a small engine
  (`MicroVm` + `IoDevice` device models) and runs a guest that performs a
  **MEMZ-style boot-sector overwrite** (512×`0xCC` to LBA 0). A `PvConsole`
  captures the guest's output and a `PvDisk` redirects the write into a
  throwaway differential image (`sandbox-disk.diff`) — reporting
  `host disk opened: no`. It proves the containment thesis: destructive
  disk writes hit a disposable file, never your real drive.

- **`vmsnap.exe`** — roadmap step 6 (disposable reset). A `SnapshotManager`
  captures the guest's full RAM image + vCPU registers, then restores them after
  a detonation in **~100 µs** (no reboot), reverting every change. The restored
  image is live and re-runnable — this is the engine half of the "disposable"
  requirement.
- **`vmsyscall.exe`** — first brick of the **"Wine in reverse" NT personality**.
  A 64-bit guest runs *natively* in the partition and makes a "syscall" via a
  doorbell port; the host **services it in its own synthetic kernel** (reads the
  guest's registers + memory, runs `NtWriteConsole`, returns). No real Windows
  kernel is involved — we are the kernel. This is the seam every Win32/NT call a
  PE makes will arrive through, to be implemented against virtual files, registry
  and fake devices.
- **`vmpe.exe`** — the **PE loader** milestone. Emits a small CRT-free 64-bit
  `.exe` to disk, then `PeLoader` maps its sections, applies DIR64 relocations,
  and rewrites the import table so `WriteFile`/`ExitProcess` point at host stubs.
  The image runs in the MicroVM (at **ring-3**, via the real `SYSCALL` path) and
  its Win32 calls are serviced by the synthetic kernel — the **"feed a .exe and
  watch what it does"** loop, live (for CRT-free PEs importing supported
  functions), on the same evasion-resistant path as `vmguard`.
- **`vmguard.exe`** — **evasion hardening**. The guest runs at **ring-3** with
  `SCE` off, so the *raw* `SYSCALL` instruction — even one a malware issues
  directly to bypass hooked DLLs — raises `#UD`, which the VMM intercepts and
  services in the synthetic kernel. A privileged instruction at ring-3 faults
  with `#GP` and is reported as a blocked action. Both escape attempts are
  caught on real hardware-isolated WHP.
- **`vmfs.exe`** — **virtual file system + behavior report**. Runs a PE that does
  `CreateFileA`/`WriteFile`/`CloseHandle`; the synthetic `NtKernel` services each
  call against an in-memory `VirtualFs` and a `ReportBuilder` records the
  timeline (created / written / bytes). The sample's "loot" lands only in the
  virtual FS — the real disk is never touched — and you get a *"what the sample
  did"* report.
- **`vmreg.exe`** — **virtual registry + fake system profile**. A PE calls
  `RtlGetVersion` and reads/writes the registry; the synthetic kernel answers
  with the **OS version, build, ProductName and GPU we invent** (a configurable
  `SystemProfile`: Win7/10/11). The *same* sample sees Windows 11 then Windows 7
  on consecutive runs, and its persistence write to `HKCU\...\Run` is captured
  virtually — the real registry is never touched.
- **`microsandbox.exe`** — the **consolidated tool** (`MicroSandbox` facade). One
  call wires the whole pipeline — ring-3 WHP MicroVM + PE loader + synthetic
  `NtKernel` (VirtualFs, VirtualRegistry, SystemProfile) + ReportBuilder — and
  prints a single unified behavior report. Run `microsandbox --demo [win7|win10|
  win11]` for a built-in "fake malware" sample (profiles the OS, drops a file,
  writes a Run key), or `microsandbox <file.exe>` for a CRT-free PE. Also reachable
  as `av detonate-micro`.
- **`vmpeb.exe`** — **PEB/TEB foundation** (toward real CRT binaries). Builds a
  minimal TEB/PEB/process-parameters in guest RAM and points the `GS` base at the
  TEB, then a ring-3 guest reads `GS:[0x30]` (TEB self), `GS:[0x60]` (PEB),
  `PEB->ImageBaseAddress` and the command line — exactly the way `ntdll`/the C
  runtime do before `main()`. The seam that real compiled `.exe`s need.
- **`vmheap.exe`** — **module list (`Ldr`) + working heap**. The guest walks
  `PEB->Ldr->InLoadOrderModuleList` to read a module's `DllBase` (what
  `GetModuleHandle`/CRT startup do), and calls `GetProcessHeap`/`HeapAlloc` —
  serviced by a bump allocator in a guest-RAM arena — writing and reading back
  its allocation. Two more pillars CRT startup stands on.
- **`vmstartup.exe`** — **CRT startup surface + TLS**. The guest calls what a C
  runtime hits before `main()` — `GetCurrentProcessId`/`ThreadId`,
  `GetCommandLineW`, `GetModuleHandleW` (NULL and by-name via the `Ldr` walk),
  `GetSystemTimeAsFileTime`/`QueryPerformanceCounter` (security cookie), and TLS
  (`TlsAlloc`/`TlsSetValue`/`TlsGetValue`) — all serviced by the synthetic kernel.

### Running a real, compiler-produced binary

`test/nt_gcc.c` is a **gcc-compiled, CRT-free** Windows program (it imports from
`KERNEL32.dll`; no C runtime). Build it and detonate it in the from-scratch
MicroVM:

```sh
gcc -m64 -nostdlib -fno-stack-protector -Wl,--entry=nt_main \
    -Wl,--image-base=0x200000 -Wl,--subsystem,console \
    -o build/nt_gcc.exe test/nt_gcc.c -lkernel32
build\microsandbox.exe build\nt_gcc.exe
```

The loader maps the real PE, resolves its `KERNEL32.dll` imports to syscall
stubs, and the synthetic kernel services the run: `GetStdHandle`+`WriteFile` show
up as **console output**, `CreateFileA` (with its 5th argument,
`dwCreationDisposition`, read **off the stack**) drops a file into the **virtual
FS**, and `ExitProcess` ends it — with a behavior report and your real disk
untouched. This is a binary a real compiler produced, running in the sandbox.

### GUI by proxy (the Wine model)

Reimplementing the Windows GUI kernel (`win32k`) is a decades-long effort
(ReactOS/Wine scale). The pragmatic path is to **proxy GUI calls to the host's
real `user32`/`gdi32`** — execution stays isolated in the sandbox, only the
*drawing* runs natively. First brick: `test/nt_gui.c` (gcc, imports `USER32.dll`)
calls `MessageBoxW`; the synthetic kernel forwards it to the real host `user32`,
so a **native Windows dialog appears** (auto-dismissed), with the call logged:

```sh
gcc -m64 -nostdlib -fno-stack-protector -Wl,--entry=nt_main \
    -Wl,--image-base=0x200000 -Wl,--subsystem,windows \
    -o build/nt_gui.exe test/nt_gui.c -luser32 -lkernel32
build\microsandbox.exe build\nt_gui.exe
```

Next bricks: real windows (`CreateWindow` + a message pump that calls the guest's
`WndProc` **back inside the VM**) and GDI drawing. A product would render into a
sandbox-owned surface (not the real desktop), since proxying to the real desktop
would be an escape channel.

### Running the real ntdll.dll (Model B)

Instead of synthesizing each Win32 function, the scalable path is to **load the
real DLLs and service only the ~488 `Nt*` syscalls** (the `Rtl*`/`Ldr*` functions
are user-mode code inside ntdll and come for free). `tools/ntsyscalls.cpp`
extracts the live `name→SSN` table (e.g. `NtAllocateVirtualMemory = 0x18`), and
**`vmntdll.exe`** proves it: `PeLoader` maps the **real `C:\Windows\System32\
ntdll.dll`** (~2.5 MB, relocated) into the guest, a ring-3 guest **calls the real
`NtAllocateVirtualMemory`/`NtTerminateProcess` stubs**, and `MiniNt` services them
by the real SSN — a real, usable allocation (`STATUS_SUCCESS`, written and read
back). **`vmntfile.exe`** grows it to the **real NT file subsystem**: the guest
calls the real `NtCreateFile`/`NtWriteFile`/`NtClose` stubs with real
`OBJECT_ATTRIBUTES`/`UNICODE_STRING`/`IO_STATUS_BLOCK` (and `NtWriteFile`'s
buffer/length read off the **stack**), serviced against the `VirtualFs` — a file
dropped virtually, real disk untouched. **`vmntsect.exe`** adds **NT sections**
(the loader's core): the guest calls the real `NtCreateFile`→`NtCreateSection`→
`NtMapViewOfSection` stubs to map a **file-backed section** into memory and reads
the file's bytes from the mapped view. **`vmntimage.exe`** takes the final step to
**real DLL mapping**: the guest creates a **`SEC_IMAGE`** section over a genuine
relocatable DLL (`test/imgtest.dll`) and maps it — `MiniNt` performs the full **PE
layout** (headers + sections by RVA per `SizeOfImage` + **DIR64 base
relocations**), then the guest **calls the mapped image's exported `Probe()`**,
which returns a relocated pointer's target (`0xC0FFEE5`) — proving in one shot
that sections landed at the right RVAs, the image's code is executable, and
relocations were applied. **`vmntquery.exe`** fills in the **queries + sync the
loader hits first**: the guest calls the real `NtQueryInformationProcess`
(`ProcessBasicInformation` → its PEB, read by `MiniNt` from the GS-base TEB),
`NtQuerySystemInformation` (`SystemBasicInformation` → 1 CPU, 4 KiB pages), and
`NtCreateEvent`/`NtSetEvent`/`NtWaitForSingleObject` (a satisfied wait) — the
pre-`LdrpInitialize` probes. **`vmntreg.exe`** adds the **NT registry**: the guest
calls the real `NtOpenKey`/`NtQueryValueKey`/`NtSetValueKey` stubs against MiniNt's
in-memory `\Registry` namespace — reading seeded system values with correct types
(`SafeDllSearchMode` = `REG_DWORD 1`, `ProductName` = `REG_SZ "Windows 11 Pro"`)
and **capturing a persistence write** (`Run\Updater = "…evil.exe"`, read back to
prove the round-trip) with the real HKLM/HKCU untouched.

**`vmldr.exe` — the boss.** Instead of calling `Nt*` by hand, it points the vCPU at
ntdll's real **`LdrInitializeThunk`** (the entry the Windows kernel itself jumps to
on a new thread) with a genuine process environment, and lets the **real Microsoft
loader drive** while MiniNt services and traces every syscall (#UD/#GP/#PF all trap
to pinpoint the exact stop). It took two fixes — enabling **SSE** in `CR4`
(`OSFXSR`; ntdll's `xorps`/`movaps` `#UD` without it) and **16-byte stack
alignment** (`RSP ≡ 8 mod 16` at entry, else aligned `movaps` spills `#GP`) — after
which the real loader runs **60+ syscalls deep** into `LdrpInitializeProcess`:
security cookie, heap via `NtAllocateVirtualMemoryEx`, NLS, the entire **Image File
Execution Options / GlobalFlag** scan, Session Manager, `ProcessCookie`, system
info. It then hits the genuine wall: after `NtWaitForAlertByThreadId` the loader
enters its **lock-free work queue / thread-alert sync** (`LdrpDrainWorkQueue`) and
spins waiting on worker threads that don't exist in our single-threaded model (a
watchdog via `WHvCancelRunVirtualProcessor` cuts the no-exit spin and reports the
RIP). This is *why* **Wine and ReactOS reimplement the loader** rather than run
ntdll's — the probe proved we can run the real loader deep into bring-up and
localized exactly where its multi-threaded-state coupling stops us.

**The Wine path — `vmwinload` / `vmwinsys` / `vmwincall`.** Rather than run ntdll's
`LdrpInitialize`, we bring up DLLs *ourselves* — exactly what Wine and ReactOS do.
`src/microvm/host/WinLoader.*` is a recursive host-side loader: it maps each DLL as
a `SEC_IMAGE` (headers + sections by RVA + DIR64 relocations), recursively loads its
dependencies, resolves cross-module imports (including **forwarders** and the **API
set** heuristic `api-ms-win-*` → `kernelbase`/`ntdll`/`kernel32`), and patches every
IAT. Three proofs: **`vmwinload.exe`** links two of our own DLLs (`libb` imports
`AddOne` from `liba`) and the guest runs `libb!Compute(20)` — which calls the
imported `liba!AddOne` across the DLL boundary — getting `50`; **`vmwinsys.exe`**
points it at the real `C:\Windows\System32\kernel32.dll` and links the whole graph —
**kernel32 + kernelbase + ntdll, 2024 imports resolved, 0 missing**; and
**`vmwincall.exe`** is the capstone — the guest **calls the real `kernel32!lstrlenA`**
export and gets the right length (`24`). That's real Microsoft DLL code executing in
our from-scratch hardware sandbox, linked entirely by our own loader, with **no
ntdll `LdrpInitialize`**. Finally, **`vmwinall.exe`** drives **every one of
kernel32's 1693 exports automatically** — resetting the vCPU and calling each with
null args, a HLT return trampoline, MiniNt servicing syscalls, and a per-call
watchdog for spinners — and classifies each outcome with no hand-written
signatures: **1317 (77.8%) execute and return cleanly** (`GetCurrentProcessId`
gives the right PID, `GetVersion` a real version, …), ~17% spin on locks/waits with
no initialized state, and ~5% fault on functions needing a real heap/handles
(CreateProcess, FindFirstFile, threadpool) — an empirical map of exactly what's
left, generated automatically.

**Lighting up the heavy APIs — `vmwininit.exe` + the Wine patches.** The keystone
the failing ~22% need is the process heap, and `HeapCreate` turned out to spin at
**ntdll+0xBBDB — the very same primitive the loader boss hit**: `0xBBD0` isn't a
loader thing, it's a core ntdll lock/list primitive (the process-heaps list under a
thread-alert lock) that both the loader and the heap use, and it assumes initialized
multi-threaded state. The fix is the Wine technique — **hot-patch the function
*code*** (covering forwarders *and* internal calls): `ApplyWinePatches` rewrites
ntdll's `Rtl*Heap` to `mov eax,<op>; syscall; ret` (MiniNt services them with a
simple bump allocator) and the SRW/critical-section locks to no-ops (safe
single-threaded). `vmwininit` proves it with a full `HeapCreate`/`HeapAlloc`/
`HeapSize`/`HeapFree` round-trip on real heap code. Re-running the fuzzer **with**
the patches, clean returns climb from **1317 (77.8%) to 1589 (93.9%)** of all 1693
exports and spins collapse from **289 to 24** — **93.9% of the real kernel32 surface
now executes and returns cleanly** in our sandbox. The rest are functions that *mean*
to exit (`ExitProcess`/`FatalAppExit`/`DebugBreak`) or need valid args/threadpool
(`CreateProcess`, `GetDriveType` with a null buffer). (Next: detonate a real `.exe`
linked against the kernel32 we brought up — the full analysis loop.
`user32`/`gdi32` still need `win32k`, hence the GUI proxy above.)

**Looking like bare metal — `vmspoof.exe` + `vmspoofreg.exe`.** Sandbox-aware malware
won't detonate if it smells a VM, so we close both avenues it checks. **CPUID**
(`vmspoof`): `MicroVm::SpoofCpuid` arms `CpuidExitList` together with the
`X64CpuidExit` master-enable bit in `ExtendedVmExits` — *without that bit the exit
list is silently accepted but never traps* (a real gotcha we hit and fixed here).
`HandleCpuid` then clears the hypervisor-present bit (leaf 1 ECX[31]), blanks the
`0x40000000` hypervisor-vendor leaf, and injects the vendor/brand/core count: this
host is an **AMD Ryzen 5 5500**, but the guest sees an **Intel i7-10700K, 8 cores, no
hypervisor**. **Registry** (`vmspoofreg`): `MiniNt::SeedPersonality` seeds a coherent
victim-machine identity malware walks — an **ASUS** board (not "VMware"/"innotek"), an
Intel CPU, an **NVIDIA RTX 3070 with a do-nothing fake driver** (`nvlddmkm` registered,
no real kernel module), a `MachineGuid`, `ComputerName`, `InstallDate`, owner, and a
list of **installed programs** (Chrome, Firefox, Office, Steam, Discord…). The guest
reads all of it **through the real ntdll stubs** (`NtOpenKey`/`NtQueryValueKey`) and not
one of the probes returns a VM tell — the box reads as lived-in metal. The persona also
covers **CPU/RAM** (`NtQuerySystemInformation` now reports **8 processors and 16 GiB**,
not the tell-tale 1-CPU/1-GiB sandbox) and **disk**: `VirtualFs` gained a blocklist of VM
artifacts (`vboxguest`, `vmci.sys`, `vmware`, `qemu`, `viostor`, `xenpci`, `sbiedll`…)
that open with `STATUS_OBJECT_NAME_NOT_FOUND`, plus seeded normal files that exist — and
`Open` now honors `CreateDisposition` so a `FILE_OPEN` of an absent/blocked path actually
fails (previously *everything* was created on open, so the VM drivers "existed"). The same
proof opens `vmci.sys`/`VBoxGuest.sys` → NOT_FOUND and `kernel32.dll`/`notas.txt` → present.

**Timing & execution artifacts — `vmspoofsys.exe`.** Two more checks a sandbox-aware
sample runs, read the way malware reads them. **Uptime**: `GetTickCount64` just reads
`KUSER_SHARED_DATA`, and a VM booted seconds ago is a sandbox tell — `MiniNt::SeedSharedData`
seeds the `TickCountQuad`/`InterruptTime`/multiplier for **~3 days of uptime** (the guest
reads `[0x7FFE0320]` directly and the host confirms 79h 21m). **Process list**: a sandbox
runs a handful of processes and malware scans the list for analysis tools (`vmtoolsd`,
`vboxservice`, `wireshark`, `procmon`, `x64dbg`…); `NtQuerySystemInformation`
(`SystemProcessInformation`) now answers with a believable **46-process** desktop (svchost,
explorer, chrome, steam, Defender…) and **none** of those tools. Neither timing nor process
inspection betrays a VM.

**Live MAC — `vmspoofnet.exe`.** The last avenue. Whatever API a sample uses to read the
MAC (`GetAdaptersInfo`/`GetAdaptersAddresses`/NDIS), in the kernel it bottoms out in an
**NDIS OID query**. `MiniNt` now services `NtDeviceIoControlFile`: an
`IOCTL_NDIS_QUERY_GLOBAL_STATS` (0x00170002) carrying `OID_802_3_PERMANENT_ADDRESS`/
`CURRENT_ADDRESS` returns a **genuine Intel OUI** (`8C:16:45:3A:9D:71`) — never a VMware
(00:05:69/00:0C:29/00:50:56), VirtualBox (08:00:27), Hyper-V (00:15:5D), Xen (00:16:3E) or
QEMU (52:54:00) prefix. `\Device\Tcp`/`\Device\Nsi` are seeded present; the guest runs the
exact path (`NtCreateFile("\Device\Tcp")` → IOCTL) and the host confirms the OUI is in no VM
list. Together with the registry NIC identity, **the network side is fully covered.**

> These require the Windows hypervisor to be running. `av vmcheck` tells you if
> it is and, if not, exactly how to turn it on. (On AMD, note the hypervisor
> also needs **NX/DEP enabled in the BIOS**.)

## Quick test (proves detection works, 100% safe)

```
build\av.exe eicar build\test.txt
build\av.exe scan  build\test.txt      # -> [THREAT] EICAR-Test-File
build\av.exe clean build\test.txt      # -> detected + quarantined
build\av.exe quarantine                # -> shows the isolated entry
build\av.exe status                    # -> what the Security panel reads
```

The EICAR string is a standard test pattern recognized by all antivirus
engines; it is **not** malware and does nothing when run.

## Project layout

```
CMakeLists.txt               builds av.exe (CLI) + avui.exe (UI)
src/
  Common.h / Util.cpp        string + console + elevation helpers
  Sha256.*                   SHA-256 via Windows CNG (BCrypt)
  SignatureDatabase.*        load + match signatures (hash + content)
  Scanner.*                  recursive file/folder scanning (CLI)
  Quarantine.*               isolate/neutralize detected files
  RealTimeMonitor.*          ReadDirectoryChangesW folder watcher
  WscIntegration.*           Windows Security Center read + register
  main.cpp                   CLI entry point
  EngineFacade.*             threaded, callback-based scan wrapper (UI)
  Sandbox.*                  restricted-process sandbox + Windows Sandbox handoff
  sample_behavior.cpp        harmless test target for the sandbox
  ComPtr.h                   tiny COM smart pointer
  UiApp.cpp                  Direct2D/DirectWrite desktop UI (avui.exe)
signatures.txt               editable signature database
docs/                        UI screenshots
```

## Notes & ideas to extend

- Add more `sha256:`/`substr:` rules to `signatures.txt`.
- Run elevated (Administrator) for `monitor` over protected folders.
- Real engines add: heuristics, PE parsing, unpacking, YARA rules, cloud
  lookups, a minifilter driver (`fltmgr`) for kernel-level real-time scanning,
  and AMSI integration. The minifilter + ELAM path is also what an MVI product
  uses to legitimately register with Windows Security.
```

Phase 14.10G adds PCM/WAV playback and a WinMM/WASAPI wrapper in
`--audio` mode, with copied buffers and standard output routed through a restricted auxiliary device.
Capture and compressed media remain unavailable.
[Licenses and Limitations](docs/PHASE-14.10G.md).
