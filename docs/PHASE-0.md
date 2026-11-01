# Phase 0 — Architecture and build border

Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.
Data: 16/10/2026.

## Scope of this delivery

Consolidate the architecture over existing modules, migrate the compilation to C++20 and establish verifiable contracts for the future kernel in the guest. The freestanding target of this phase contains contract validators and has no entry point, bootable image, device implementation or kernel loader. Phase 1 remains a later delivery, subject to user request. At the end of phase 0, stop.

## Architecture decisions

1. WHP continues on Windows host as a virtualization engine. VMM itself
   controls partition, RAM, vCPU and devices without QEMU.
2. The target kernel will run in ring 0 in the guest. Windows APIs, allocators
   host process, physical files and host pointers are not part of your ABI.
3. MiniNt (model B) remains functional reference for semantics and regression.
   NtKernel (model A) remains separate. None of these models already constitute the desired guest kernel.
4. Preserve current organization and add build borders; do not move
   modules or delete examples to look like a completed architecture.
5. The initial platform is x64, little-endian, a vCPU, RAM from 16 MiB to 1 GiB.
   This initial limit follows the boot mapping of the VMM, does not define the final limit of the product. SMP, advanced quotas and hardware profiles stay in the stages foreseen in the roadmap.
6. Shared contracts use fixed width integers, GPas and sizes;
   do not contain hands, Windows handles, STL strings, bools or virtual objects. Reserved need to be zero. Unknown versions are refused.
7. Kernel migration will preserve algorithms and tests when possible.
   Current managers who use `std::map`, `shared_ptr`, Windows exceptions or services will require adaptation to their own runtime; copying headers is not enough.

## Inventory of modules and dependencies

| Component existing | Status verifiable and dependencies | Destination / work posterior |
|---|---|---|
| `Scanner`, `SignatureDatabase`, `Sha256` | hashes/content Scanner; Windows and CNG files. | Host; Enlarge detection, formats and connectors in phase 14. |
| `Quarantine` | XOR storage and metadata in files. | Host; integrity, transactions and recovery in phase 15. |
| `RealTimeMonitor` | Directory monitor by Windows API. | Host; service, queues, event losses and IPC in phase 16. |
| `EngineFacade`, `UiApp` | Scanner ed background and UI Direct2D/DirectWrite. | Host; integration WinUI 3 and languages in phase 17. |
| `Sandbox` | Restricted running on the host and forwarding to Windows Sandbox. | Legacy paths separated from the kernel itself; do not equal the new guest. |
| `WscIntegration` | Integration/consultation Windows Security Center. | Host; validate status and product deployment requirements. |
| `Common`, `Util`, `ComPtr` | Helpers of types, console and COM/Win32. | Host; do not include in the freestanding kernel. |
| `Preflight` | Diagnosis of virtualization availability. | Host; preserve reading without changing safety/boot from Windows. |
| `MicroVmMonitor` | WHP, RAM, a vCPU, I/O, registers and permissions. | VMM host; reuse and amplify in phases 1–12. |
| `PvConsole`, `PvDisk` | Port console and write template in differential file. | VMM; legacy protocols without claiming full bus/storage. |
| `MiniNt`, `MiniNtObjects` | Syscalls and objects attended no host. | Reference; adapt parts and complete semantics no guest. |
| `GuestHeap`, `GuestVirtualMemory`, `GuestMemory` | Allocation and validation tested, with metadata/backends host. | Reference for phase 3 memory management. |
| `GuestObjects`, `GuestScheduler`, threads and sync | Rights, references and scaling cooperative tested. | Phase 4–5 reference; still no preemption/SMP. |
| `NtKernel` | Model A synthetic; distinct management of MiniNt. | Legacy of regression; do not mix your handles/status with model B. |
| `PeLoader`, `WinLoader`, `ProcessEnv` | Partial loading; RAM host dependencies and runtime patches. | Phase 8 reference; validate parsers and withdraw provisional responses. |
| `VirtualFs`, `VirtualRegistry`, MiniNt registry | Memory state, different interfaces between models. | Persistence, security and semantic unification in phases 7–8. |
| `SnapshotManager` | Copy of RAM and subconjunto of registers. | Phase 9; does not restore kernel, objects, devices and permissions alone. |
| `ReportBuilder`, `MicroSandbox` | Reports and facade of model A. | Host; integrate telemetry and phase 13 policies. |
| Examples `vm*` and ten existing tests | Specific regression cases, not general Windows applications. | Preserved; matrix start below. |
| Service/IPC, SQLite, connectors, YARA, WinUI 3 | Integrated implementation still pending on this basis. | Phases 14–18. |
| Kernel no guest, drivers, network and GUI guests | Not yet implemented as their own operating system. | Phases 1–12. |

The `VmSpoof*` paths and existing personalities are legacy examples; this phase does not expand identity changes or use their results as a measure of realism or isolation.

## Borders and compilation targets

| Target | Content |
|---|---|
| `guardian_host` | CLI `av` and UI `avui`, connected at libraries Windows. |
| `guardian_vmm` | Tools WHP `vm*` and `microsandbox`; Keep going no host. |
| `guardian_reference` | Seven reference examples and tests, including contracts when enabled. |
| `guardian_abi` | Layout and validation heads without dependency on Windows APIs. |
| `guardian_guest_contract` | Object x64 with built-in validators in kernel subset. |
| `guardian_guest_audit` / `guardian_guest` | COFF audit of this object and guest grouping. |

Groups do not create new running processes or services. The guest object is connected to the host test to verify the same validators compiled with their restrictions; this test does not constitute kernel execution on a VM.

Current requirements: CMake 4.0+, Visual Studio 2022 C++ x64, Windows SDK and Python 3.9+ for auditing. There are no dependencies downloaded by configure. C++20 has been enabled on native targets; C++ named modules are not used. The guest target requires MSVC x64; alternative hosts builds can shut it down by `GUARDIAN_BUILD_GUEST_CONTRACT=OFF`, failing to validate this border.

For the guest: no RTTI, unwinding C++, default automatic libraries, runtime instrumentation or Just My Code. Restrictive options apply to the guest target; host keeps runtime, Debug checks and stack protection. Build checks the actual COFF: x64 machine, absence of unsolved external symbols and directives that request external libraries or symbols. This demonstrates independence from that object; does not prove no bugs, security of a future kernel or compatibility with Windows drivers.

## Boot contract v1.0

Executable definitions: `shared/guardian/Abi.h` and `Validate.h`.

- Early image format: PE32+ x64 freestanding, preferred base
  `0x200000`, without Windows import, dynamic booters or external runtime. Phase 1 will build the image and validate sections, input and addresses before loading it; this implementation is not part of phase 0.
- Long entry mode/ring 0, identity-mapped pagination, interruptions and direction
  flag disabled. The current VMM reference keeps OSXSAVE and SCE disabled; the kernel will only change these states when implementing its exception paths and syscall. The initial contract does not announce AVX/XSAVE.
- Convention x64: RCX points to `BootInfo`; stack 16 bytes with shadow space and
  input alignment corresponding to ABI. The input should not return; phase 1 will treat unexpected return as observable failure.
- `BootInfo`: 160 bytes, alignment 8. Contains version, machine, real size of
  RAM, image/entry, stack, tables, channel, a vCPU and non-secret session ID.
- All declared areas should fit in RAM, without overflow or overlap.
  The first MiB is reserved for bootstrap, including the existing tables in `0x70000..0x73FFF`. Boot data and other buffers start after that region.
- Memory map: up to 128 entries of 32 bytes, sorted and contiguous, covering
  all RAM. Sorts reserved, available, boot, image and stack data. Validation confers ownership of areas; does not replace MMU/WHP. protection
- Test Reference Layout: `0x100000` boot, `0x101000` map, devices
  `0x102000`, `0x103000` channel, `0x200000` image, `0x400000` stack. These addresses are a reference configuration, not host pointers.

The transporter must read/copy the header and the tables only after validating their intervals in the real RAM. Validators receive structures already accessible to the caller; they do not make an arbitrary C++ pointer safe to dereference.

## Devices and messages v1.0

Table of devices: up to eight 32 bytes descriptors. The initial contract requires `0xE9` port diagnostic console and `0xEA` control channel without device interruption. Its types and versions are explicit and do not implicitly reuse the protocol of the legacy `0x70/0x71` disk.

The future implementation of the channel uses a GPA buffer fixed on the boot, of 4096 bytes. A notification cannot cause the host to follow arbitrary address provided by the guest. The host should copy the message while vCPU is stopped, validate the envelope and then interpret payload in its own storage. Doorbell is an eight-bit script in `0xEA`; the value of the byte is neither command nor address and is ignored. The operation type comes from the envelope in the fixed buffer.

Message Header: 40 bytes, magic/version, operation, sizes, sequence and boot ID. Flags and reserved fields need to be zero. The package must be exact size; another session, repetition, unknown version or excessive payload are denied. Boot ID and sequence avoid session confusion; they do not constitute cryptographic authentication of an unreliable guest.

Initial guest only operations → host:

| Operation | Payload and use planned |
|---|---|
| `Hello` | 16 bytes: architecture, vCPUs and capabilities implemented. |
| `Diagnostic` | 1–1024 diagnostic bytes; do not execute as commands or format. |
| `StopResult` | 8 bytes: result of autotest and field reserved zero. |

Phase 1 transport status must require initial Hello, monotonic sequence and terminal stop, validate payload fields and limit diagnostic volume, time and number of outputs. Validators of this phase confer envelope/layout; do not implement the transport or all that state machine. No message grants general access to privileged host APIs.

## Initial matrix and acceptance measures

Versioned Settings: `config/phase0-acceptance.json`. Observed reference environment: Windows 11 Pro x64 `10.0.26300`, MSVC 19.44, SDK 10.0.26100.0. Other builds and Windows 10 still need to run on the respective machine. The use of local ntdll validates a subset of this reference; does not validate XP profiles–11. ISO Windows is not required to build the kernel itself.

Existing references: `vmntdll`, `vmntfile`, `vmntsect`, `vmntimage`, `vmntquery`, `vmntreg`, `vmwininit`; expected output zero, timeout 20 seconds per run. The WHP tests use small programs generated in the project and watchdog. An execution ignored by lack of WHP does not count as real validation.

New planned references: kernel with autotest (phase 1), preemption scenarios (4), own console application (8), snapshot consistency (9) and own GUI application (11). JSON explicitly marks that they do not yet exist. Do not use success of syscall examples as proof that these future applications already work.

First phase 1 gate: 20 sequential bootes per test RAM size (16/64 MiB), 3-second timeout, Hello and explicit stop, as well as invalid denied entries. Register wall time, vCPU outputs, RAM, private host memory and result. Timeout is functional limit, no promise of performance. Other workloads have measures defined in JSON; final performance limits will be set from measured baselines when they exist.

Identity profiles, features, clock and usage will have centralized configuration and versions. Host identifiers do not enter the boot contract. Resource budget and capabilities announcement need to reflect real implementation; these admission policies will be implemented in the corresponding subphases.

## Play validation

```powershell
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
#  Only border freestanding and new tests:
cmake --build build-codex --config Debug --target guardian_guest guest_contract_tests
ctest --test-dir build-codex -C Debug -R '^guest_' --output-on-failure
```

The three new tests check the contract using the freestanding object, audit your COFF and exercise auditor refusals with truncated or runtime dependent objects. The previous ten tests remain in the suite.

## Outcome and closure of the phase

**Phase 0 completed in 16/10/2026. Phase 1 not initiated.**

| Scan | Debug | Release |
|---|---:|---:|
| Build complete ed C++20 | 42 executable | 42 executable |
| CTest Tests | 13/13 | 13/13 |
| Tests ignored | 0 | 0 |
| Examples of reference | 7/7 | 7/7 |
| CLI without arguments: help and zero output | It's over. | It's over. |
| Freestanding Object: Unsolved external symbols | 0 | 0 |
| Freestanding Object: requested default libraries | 0 | 0 |
| Build and audit the contract in new directory | It's over. | It's over. |

The CTest suite took approximately 2.85 seconds in Debug and 2.41 in Release in this execution. They are observations of this machine, not product performance limits. The WHP tests actually performed.

The new contract targets have been compiled with warnings treated as errors. The complete build records C4244 warnings on legacy targets using model A: Enum conversions to integer and `wchar_t` to `char`. These paths have not been rewritten at this stage; the text conversion limitation remains registered for implementation evolution. There are no errors in the final builds.

Evidence ed `build-codex/`:

- `phase0-full-release-script.log` and `phase0-full-debug-script.log`.
- `phase0-release-tests-details.log` and `phase0-debug-tests-details.log`.
- `phase0-regressions.json` and `phase0-host-smoke.json`.
- `phase0-clean-configure.log`, `phase0-clean-debug-build.log` and
  `phase0-clean-release-build.log`.
- `phase0-build-boundaries.json`: options generated for host and guest.
- `phase0-change-manifest.json`: hashes, backup, environment and validation.

The independent directory `build-codex-phase0-clean` was maintained for inspection. Previous files of this phase: `snapshots/codex-before-phase0-20261016-153611/`.

According to the execution rule, the work for here. The next implementation is the kernel image and its boot in phase 1, only after new user request.

## Technical references

- [ABI x64 — Microsoft](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [Standard library omission with /Zl](https://learn.microsoft.com/en-us/cpp/build/reference/zl-omit-default-library-name)
- [Copy /kernel subset](https://learn.microsoft.com/en-us/cpp/build/reference/kernel-create-kernel-mode-binary)
- [Property MSVC_RUNTIME_CHECKS — CMake](https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_CHECKS.html)
