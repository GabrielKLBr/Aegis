# Phase 1 — First boot of the kernel itself

Date: 16/10/2026. Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. **State: phase 1 completed. Phase 2 not started.**

## Deliverable

The `build-codex/guest/<Config>/guardian-kernel.gk` artifact is a unique PE32+ x64 image, loaded into the RAM of a WHP partition. The existing VMM prepares long mode and delivers the execution to the code of that image in CPL0. The image is not opened by the Windows process loader. The existing MiniNt continues as a regression reference in the host.

The entry point receives `BootInfo` in RCX, validates header, map and devices, checks initialized data, zeroed area, stack, function call and read/ write from globals. Sends Hello, diagnostic and StopResult by contract 1.0. Host only reports success with CPL0, valid sequence, PASS autotest and zero status. HLT isolated is not successful. The console identifies kernel as 0.1.0.

Fonts main:

- `guest/Kernel.cpp` and `guest/Memory.cpp`: entry, autotest, transport and
  primitive local memory, without CRT, imports, RTTI or C++ exceptions.
- `src/microvm/boot/KernelImage.*`: reading and validation of the PE image.
- `src/microvm/boot/KernelBoot.*`: layout, permissions, boot, watchdog and report.
- `src/microvm/boot/BootChannel.*`: protocol status and limits.
- `src/microvm/boot/BootMain.cpp`: tool `guardian_boot`.

## Initial format and layout

The loader accepts only PE32+ x64 Native fixed base `0x200000`, with up to 1 MiB image, 4 MiB file and 16 sections. Validate offsets, sizes, alignments, overlays and input within executable code. DLL refusal, imports, TLS, relocations, unsupported directories and extra data at the end. Only unwind and debug optional metadata are allowed; they are not executed or used to treat exceptions at this stage. Sections are copied and the rest of the image, including BSS, is zeroed.

| GPA | Usage |
|---|---|
| `0 .. 0xFFFFF` | Private Bootstrap, including VMM pagination |
| `0x100000` | BootInfo, read-only |
| `0x101000` | Memory map, read-only |
| `0x102000` | Devices, read-only |
| `0x103000` | Message Buffer, 4096 bytes, reading/writing without execution |
| `0x104000` | #UD Trap for unexpected input return, RX |
| `0x200000` | Image; R headers, RX code, RW data without execution |
| `0x400000 .. 0x40FFFF` | 64 KiB stack, first page without mapping |

The input uses RSP at the top minus `0x28`: x64 alignment, return address and shadow space. Interruptions are disabled. VMM provides initial address mapping; kernel does not yet manage pages on its own. Execution and writing protections are applied by the WHP to the guest.

The linker can send LNK4281 to the base below 4 GiB. This base is deliberate: the image has fixed address within the GiB home map of 1 microVM and is not a Windows process with ASLR. Imports or Runtime dependencies are not solved by the host. The new C++ targets compile with `/W4 /WX`.

## Border and faults

The transport accepts only OUT scalar from a byte: `0xE9` to console and `0xEA` to signal the fixed buffer. OUT value does not provide host pointer. IN, wide operations, string/REP and unknown ports are refused. Host copies the package while vCPU is stopped and limits header and payload to buffer. Version, boot ID and sequence are required. Hello is unique; diagnosis and stop depend on it. Errors are permanent in session.

Session limits: 128 messages, 16 KiB accumulated diagnostics, 1024 bytes per diagnosis, 4096 bytes of console and 10000 service outputs. Text is ASCII limited. The stop closes Run before performing the next invited instruction. The new TotalExitCount counter also includes terminal outputs; ExitCount legacy keeps the previous semantics for the existing examples.

Watchdog cancels vCPU after three seconds of execution. Interceptable exceptions are consulted via WHvGetCapability; #UD/#GP/#PF are required. Unsupported bits are not requested. The report records protocol failure, exception and RIP, memory failure and GPA, cancellation, kernel status and limited console. Lack of WHP returns 77; tests mark it as ignored, not as approved boot.

## Play

```powershell
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
#  Inspection without starting VM:
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel.gk
#  16 MiB Boot with explicit report:
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel.gk --ram-mib 16 --repeat 20 --report .\build-codex\manual-boots.json
```

The Phase 1 tool allows 16 or 64 MiB, a vCPU and up to 100 sequential bootes. The `guardian_guest` group audits contract and final image. The `guardian_reference` group includes the tool, new tests and existing references. The script uses the local SDK installation; it does not change Windows startup or security settings.

## Validation

The suite adds three tests to the existing 13:

- `guest_kernel_image`: real image, mapped/BSS content, writing limits
  and cases of truncated, corrupted image or with unsupported dependencies.
- `guest_boot_protocol`: order, version, session, replay, capabilities, text,
  sizes, reserved fields, accumulated limits and terminal state.
- `guest_kernel_boot`: 20 boots in 16 MiB and 20 in 64 MiB per configuration,
  with CPL0, zero status, diagnosis and total time observed below 3 s. It also exercises 22 controlled faults/injections and six refusals of admission before allocating RAM. It only executes the kernel and tiny programs of this test.

Cases include invalid parameters and tables, unexpected return, #UD, HLT without result, infinite loop, invalid I/O, excessive package, written in code/header, data execution, guard page, autotest with failure and proof that RequestStop prevents the next instruction.

JSONs record configuration, compiler, workload, RAM, time since the start of preparation, total outputs of vCPU, private memory of the host process measured before the destruction of VM, CPL, exception mask and result. Private memory includes the entire process, not just VM. Numbers are observations of this machine, not promises of performance or compatibility.

### Final results

| Scan | Debug | Release |
|---|---:|---:|
| Build complete ed C++20 | 46 executable + kernel | 46 executable + kernel |
| CTest | 16/16 | 16/16 |
| Tests ignored | 0 | 0 |
| Examples of reference | 7/7 | 7/7 |
| Real boots: 20 out of 16 MiB + 20 out of 64 MiB | 40/40 | 40/40 |
| Scenes negative no guest | 22/22 | 22/22 |
| Invalid admissions refused before allocating VM | 6/6 | 6/6 |
| Images invalids recusadas | 56/56 | 56/56 |
| Imports Windows / TLS / relocations | 0 / 0 / 0 | 0 / 0 / 0 |
| Memory image size | 24576 bytes | 20480 bytes |
| CLI av and guardian_boot: help | It's over. | It's over. |

They are **80 positive boots** in the final matrix. All observed CPL0, zero status, PASS autotest and 25 vCPU outputs. Observed time, including preparation:

| Configuration / RAM | Minimum | Maximum |
|---|---:|---:|
| Debug / 16 MiB | 1,548 ms | 4,269 ms |
| Debug / 64 MiB | 3,005 ms | 7,586 ms |
| Release / 16 MiB | 1,449 ms | 2,575 ms |
| Release / 64 MiB | 2,743 ms | 4,170 ms |

The full suite took 4.07 s on Debug and 3.26 s on Release in this run. Environment: Windows 11 Pro 10.0.26300 x64, WHP available, MSVC 19.44, SDK 10.0.26100.0. Other Windows versions still need self-validation. C4244 warnings of legacy targets remain recorded in logs. The new C++ targets have passed with warnings treated as errors; the LNK4281 linker warning from the fixed base is explained above.

An independent configuration in `build-codex-phase1-clean`, with disabled tests, also compiled and audited Debug contract and image. The inspection of the generated projects confirmed the separation of host and guest options. MSVC dumpbin inspected both images without imported dependencies.

Evidence preserved in `build-codex/`:

- `phase1-full-debug-script.log` and `phase1-full-release-script.log`.
- `phase1-debug-tests-details.log` and `phase1-release-tests-details.log`.
- `phase1-boots-Debug.json` and `phase1-boots-Release.json`; files
  corresponding `.negative.json` record negative scenarios.
- `phase1-regressions.json` and `phase1-host-smoke.json`.
- `phase1-debug-pe-audit.log`, `phase1-release-pe-audit.log` and
  `phase1-build-boundaries.json`.
- `phase1-clean-configure.log` and `phase1-clean-debug-build.log`.
- `phase1-change-manifest.json`: source hashes, backup, environment and summary
  Checked results. Criteria in `config/phase1-acceptance.json`.

The seven previous files changed were preserved in `snapshots/codex-before-phase1-20261016-155528/`. Logs from the initial attempts were also maintained. No Windows security settings have been changed.

## Delivery Limits

This is the first bootable kernel itself inside the sandbox. There is still no guest IDT/ISR, interrupt timer, syscalls, processes, managed memory, filesystem, Windows drivers or loading Windows applications into this new kernel. These tasks continue in the next steps of roadmap. There is also no physical startup by BIOS/UEFI on this delivery.

The execution ends at the end of phase 1. Phase 2 will only start after further request, as agreed by the agreed stop rule.

## References

- [Format PE — Microsoft](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [Entry explicit /ENTRY](https://learn.microsoft.com/en-us/cpp/build/reference/entry-entry-point-symbol)
- [No libraries default /NODEFAULTLIB](https://learn.microsoft.com/en-us/cpp/build/reference/nodefaultlib-ignore-libraries)
- [WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetcapability) Capacities]
