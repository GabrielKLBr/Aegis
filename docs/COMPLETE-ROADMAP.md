# Development plan for GuardianAV and the kernel itself

Reference date: 26/10/2026. Active base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.

**Windows infrastructure requested in 28/10/2026:** independent controls of the engines by real services, with analysis in restricted user processes. [Scope, use and validation](WINDOWS_ENGINE_SERVICES.md). This delivery is separate from the 14.11 frameworks and does not declare them completed.

**Current Windows revision:**Continuation from 12B to 13 passed in matrix
MSVC/WHP: 80 distinct tests Debug, with documented retests, and 80/80 Release
in a complete suite, without ignoring. [Corrections, evidence and limits](WINDOWS-PHASES12B-13-REVIEW-20261023.md).
12B PE/SMP integration and 12C specific NT references continue
open. Phase 14 was delivered to Windows on 23/10/2026; its Linux acceptance is
Partial. Subphases 14.1–14.14 below become the next priority of
compatibility, before phase 15. The implementation of 14.1 and its scope of
acceptance are registered in [PHASE-14.1.md](PHASE-14.1.md). A 14.2 enlarges the
external loader and volume of runtime: [scope and acceptance](PHASE-14.2.md).
14.3 adds native services and a limited matrix with own NTDLL and
original Win10: [implementation, validation and to-ed](PHASE-14.3.md).
The 14.4 implements the own Win32 matrix and reading of API schemas set:
[contracts, tests and limits](PHASE-14.4.md). The 14.5 adds the corpus C/C++
with original runtimes in the guest: [implementation and limits](PHASE-14.5.md).
The 14.6 adds persistent registration, tokens/ACLs and LocalService services with
self-life: [delimited delivery, testing and limits](PHASE-14.6.md).
14.7 has its own implementation and graphical acceptance, described in [PHASE-14.7.md](PHASE-14.7.md).
14.8 adds the delimited batch of COM/RPC, Shell, properties and controls
described in [PHASE-14.8.md](PHASE-14.8.md). A 14.9 adds contained network, HTTP, TLS and encryption according to [PHASE-14.9.md](PHASE-14.9.md). The 12F adds presentation in native windows as [PHASE-12F.md](PHASE-12F.md). A 14.10A adds the delimited native bridge of [PHASE-14.10A.md](PHASE-14.10A.md); 14.10B and C have deliveries bounded on [PHASE-14.10B.md](PHASE-14.10B.md) and [PHASE-14.10C.md](PHASE-14.10C.md); 14.10D has delivery bounded on [PHASE-14.10D.md](PHASE-14.10D.md); 14.10E has delivery bounded on [PHASE-14.10E.md](PHASE-14.10E.md); 14.10F has delivery bounded on [PHASE-14.10F.md](PHASE-14.10F.md); 14.10G has the clipping PCM/WinMM/WASAPI described in [PHASE-14.10G.md](PHASE-14.10G.md); compressed media and 14.11–14.14 the records below preserve the history of the
previous deliveries and their to-dos.

**Reviewed graphical priority:**12F has pixels drawn in MiniNT. The rendering of GUI operations by the actual Windows host APIs is planned for **14.10A–14.10E**, before DirectX in **14.10F** and media/audio in **14.10G**. At user request, a 14.10E prioritizes expanding GDI compatibility in host windows; DirectX and media/audio One step forward. 14.10A–14.10E have deliveries bounded in [PHASE-14.10A.md](PHASE-14.10A.md), [PHASE-14.10B.md](PHASE-14.10B.md) and [PHASE-14.10C.md](PHASE-14.10C.md) and [PHASE-14.10D.md](PHASE-14.10D.md) and [PHASE-14.10E.md](PHASE-14.10E.md); F–G have deliveries bounded on [PHASE-14.10F.md](PHASE-14.10F.md) and [PHASE-14.10G.md](PHASE-14.10G.md). Each batch requires own delivery and request; complete one does not authorize to start the following.

** Delivery history:**12A completed for the reference matrix, with validation Windows/WHP and Linux/KVM. [Results and limits](PHASE-12A.md). 12B is in implementation and validation: [resources, quotas and to-ed]](PHASE-12B.md). The self-contract `resources` passed the Linux matrix (47 Debug, 47 Release and 30 ASan/UBSan), with SMP, scalable volume and independent folder; Windows acceptance of revision and integration to existing profiles remain pending. 20/10](PHASE-12B-RESOURCES.md). 12C passed Linux validation on 21/10/2026 (50 Debug, 50 Release and 32 ASan/UBSan), with Windows references still pending: [matrix, delivery and differences](PHASE-12C.md). 12D was authorized in new request: sessions and interaction passed in 53 Debug tests, 53 Release and 33 ASan/UBSan in the stable review of 12D. [12D Register]](PHASE-12D.md). 12E was implemented and accepted on Linux on 22/10/2026: 56 Debug, 56 Release and 34 ASan/UBSan, with functional and reproducible synthetic profiles. [Delivery and limits](PHASE-12E.md). Native Windows acceptance and previous requirements remain pending. The phase 13 passed Linux acceptance on 22/10/2026: 59 Debug, 59 Release and 35 ASan/UBSan, with telemetry, policies and behavioral analysis. [Delivery and limits](PHASE-13.md). Windows acceptance and previous requirements remain open; phase 14 the historical records below preserve the results and to-ed of the time.

**Progress:** [phase 0](PHASE-0.md), [phase 1](PHASE-1.md), [phase 2](PHASE-2.md), [phase 3](PHASE-3.md), [phase 4](PHASE-4.md) and [phase 5](PHASE-5.md), [phase 6](PHASE-6.md) and [phase 7](PHASE-7.md) completed. The kernel itself executes code in CPL0 and CPL3, treats exceptions, meets syscalls, receives interruptions and manages memory, threads with preemption, processes, handles, objects and rights within the guest, as well as internal drivers and I/O asynchronous for console, messages and blocks, files and persistent registry in a volume of its own with overlays. Final validation: 784 volume checks, 32 the persistence in the host, 80 Phase boots 7, other 480 phase regression 1–6, 31 tests and seven examples by configuration in Debug and Release, without ignored tests. **Phase 8: implementation of own console matrix completed; native acceptance of pending Windows.** Previous milestones cover loader, runtime, threads, children, explicit closure and unwind virtual. 18/10 added order/continuation of exceptions in CPL3, unwind non-local, dynamic DLLs with TLS/references/rollback and the application behavior audit. The Linux matrix approved 132 Real boots and 60 CTest executions in Debug, Release and Sanitize. Evidence, limits and resume: [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).

The product remains Windows/MSVC/WHP. The Phase 8 is not formally closed: missing 40 CTests in each Windows configuration, including the WHP boot and 153 + 59 native comparisons of unwind. The last accepted milestone in Windows remains the fifth milestone, recorded in [PHASE-8-UNWIND.md](PHASE-8-UNWIND.md). The phase 9 it was subsequently authorised in 18/10, and passed Linux acceptance: [PHASE-9.md](PHASE-9.md). The phase 10 it was subsequently authorised, also in 18/10. Deployment and acceptance approved Linux; native Windows gate pending: [PHASE-10.md](PHASE-10.md). The phase 11 was authorized in new request and passed on Linux acceptance: [PHASE-11.md](PHASE-11.md).

** Linux Flow authorized in 18/10:** Scanner, hash, inspection, algorithms, guest recompilation and KVM executable work natively without Wine or QEMU. NX and KVM access were confirmed after the change in BIOS. Commands, validation and protection differences in [LINUX-NATIVE.md](LINUX-NATIVE.md) and [LINUX-KVM.md](LINUX-KVM.md).

This plan follows decisions after the original request: take advantage of this base, use WHP directly, do not use QEMU and prioritize the basic kernel before expanding compatibility and finalizing the antivirus interface.

## State of departure

The current MiniNt implements in the host heat, virtual regions with WHP permissions, typed handles, cooperative threads, events, mutexes and traffic lights. The last validation approved ten tests and seven examples in Debug and Release; the complete Release build produced 41 executables. These results cover the exercised cases, do not constitute a general Windows compatibility measurement.

There are also signature scanner, directory monitor, quarantine, Direct2D interface, executable loaders, disk templates and partial snapshots. Each component should be integrated and validated in the context of the product.

MiniNt still performs kernel logic in the host process. The tested foundations will be reference and, when appropriate, re-used code for a kernel that runs within the guest. Migration requires replacing host dependencies and adapting memory, interruptions, synchronization and useful life of objects.

## Destination Architecture

- ** Windows Host:** protection service, scanner, sandbox control, VMM WHP,
  product storage and WinUI 3 interface.
- **Invited:** Own kernel, processes, memory, drivers, files, record,
  compatible runtime and analyzed applications.
- **Front between both:**Virtual devices and explicit protocol,
  versioned, validated and with resource limits.

The WHP remains the host virtualization mechanism. Kernel itself in the guest does not require to rewrite the hypervisor as well. The first platform will be x64, a vCPU and virtual devices defined by the project. Physical hardware booting constitutes an additional platform, with firmware and own drivers; it is not required to run the kernel within this sandbox.

## How to complete a phase

A phase is only completed when its implemented behavior is demonstrated by tests, the relevant previous tests continue to pass, the integration compiles in Debug and Release and its limitations are documented. The test should exercise the final behavior, including failures and lack of resources.

Buttons, stubs, calls that only return success and compiled executable numbers are not functional completion criteria. Unsupported functionality should produce explicit and consistent results.

Each phase ends with revisionable code, validation and copying records or recoverable versioning of previous files. Historical evidence remains identified by step.

## Stepped execution and mandatory stop

When completing **each phase**, deliver changes, tests, results, limitations and relevant files; then **stop and close the turn**. Only start the next phase after new user request. The rule also applies for each subphase 12A–12F and 14.1–14.14 below. The authorization of the current phase allows you to complete your tasks and corrections, but does not allow you to proceed automatically to the next.

A single approved test does not close a phase whose remainder is pending. If there is real lock, record the exact point and do not declare conclusion. This rule was registered before implementation. The progress of each phase is informed at the beginning of this document; it does not start a next phase automatically.

**Authorized version in 24/10/2026:** upon completion of each new phase or subphase, review, make a commit identified by the step and send it to `GabrielKLBr/Aegis` using `tools/aegis_git.py` without further confirmation. Check the remote commit before closing the delivery. The permanent rule is in [AGENTS.md](../AGENTS.md); builds remain outside the public repository.

## Environment Realism and Sandbox Detection Assessment

Added objective: to increase the fidelity of the environment and reduce observable differences by applications, preserving isolation. Hardware, drivers, features, time, files and registration should describe the same environment.

Profiles can take as reference non-sensitive host models and capabilities. Unique identifiers will be replaced by stable virtual values per profile. Identity will be displayed exclusively in guest: initially in MiniNt and, after migration, also in kernel and guest devices. All these paths must agree. Do not change the identity of Windows host or expose your serial numbers, credentials or personal files.

A CPU/GPU identity does not implement instructions, firmware, commands or device interfaces. Separate configurable identity, implemented device and validated driver. Do not promise support for any undetectable driver or sandbox. The necessary structures will be provided in the early stages, with implementation and validation in subphases 12A–12E.

## Phase 0 — Consolidate architecture, scope and compilation

** Work:** inventory existing modules and their dependencies; formally separate host, VMM and guest; set boot ABI, devices and messages. Prepare distinct targets for Windows code and kernel freestanding code. Plan the migration of the applicable native code from C++17 to C++20 and register kernel runtime restrictions. Set reference applications, target versions and CPU measurements, memory, disk and performance that will be used for acceptance.

**Conclusion:** registered architecture and initial matrix; reproducible builds; preserved current suite; any feature identified as existing, partial or pending. Results of this step: [PHASE-0.md](PHASE-0.md).

## Phase 1 — Starting the kernel itself in the guest

**Work:** Generate kernel image; establish input point, memory map, start stack and parameter pass; load and start this image by the existing VMM. Implement diagnostic console, version identification, controlled stop and crash report. Not depending on Windows runtime within the guest kernel.

**Conclusion:** Repeatedly start your own kernel in WHP, perform a self-test routine and terminate with host observable result. Invalid images and parameters must be rejected in a controlled manner.

**Result:** completed on 16/10/2026. Implementation, evidence, commands and limitations: [PHASE-1.md](PHASE-1.md). The execution was terminated at this stage.

## Phase 2 — CPU, exceptions, interruptions and clock

**Work:**implement descriptors and CPU states, break tables, failure treatment and transition between kernel and user. Prepare syscalls entry and return, virtual timer and monotonic clock. Register CPU status and cause of failure without relying on simulated host success.

**Conclusion:** a user program calls the kernel and returns; CPU defaults have set destination; the timer generates verifiable interruptions; time frames and readings are consistent. An application failure cannot silently drop the kernel.

**Result:** completed on 16/10/2026. Implementation, evidence, commands and limitations: [PHASE-2.md](PHASE-2.md). The execution was terminated at this stage.

## Phase 3 — Memory managed by the guest kernel

**Work:**implement allocation of physical pages and tables of guest pages; separate kernel and user spaces; implement decommit, release, query and protection. Adapt existing allocators, add guard pages, page failure processing and resource accounting. Keep separate guest page permissions and WHP protections.

**Conclusion:** invalid access is blocked, kernel memory is not user accessible, allocation failures preserve the state and allocation/release cycles leave no leaks. Tests of the current model serve as semantic reference.

**Result:** completed on 16/10/2026. Implementation, evidence, commands and limitations: [PHASE-3.md](PHASE-3.md). The execution was terminated at this stage.

## Phase 4 — Threads, guest pre-emption and synchronization

** Work:** port creation and termination of threads, context exchange and standby states. Implement preemption in a vCPU, basic priorities, suspension, events, mutexes and traffic lights. Complete multiple waits, timers, alerts/APCs, critical sections and SRW locks; withdraw provisional responses from these paths. Preserve all CPU state required by the announced capabilities.

**Conclusion:** Several threads progress even when one does not syscalls; waits and abandonment have correct results; races, timeouts and cancellations are tested. No lock required for the application can remain a no-op.

**Result:** completed on 16/10/2026. Implementation, evidence, commands and limitations: [PHASE-4.md](PHASE-4.md). The execution was terminated at this stage.

## Phase 5 — Processes, objects and safety model

**Work:**create and finish processes with independent memory spaces; integrate handles, duplication, inheritance and references.Implement tokens, identities, access rights and security descriptors.Add namespace of objects with name, process limits and cleaning of incomplete processes.

**Conclusion:** processes do not access memory or objects other than others without authorization; rights are applied; failure or termination of a process releases its resources without corrupting others.Children processes work with controlled inheritance.

**Result:** completed on 16/10/2026. Implementation, evidence, commands and limitations: [PHASE-5.md](PHASE-5.md). The execution was terminated at this stage.

## Phase 6 — I/O infrastructure, devices and drivers

**Work:**define bus and virtual device contracts; prepare I/O manager, requests, asynchronous completion, cancellation, loading and unloading of own drivers. Implement console, host/guest channel and storage initially. Validate queues, sizes and addresses on both sides of the border.

**Conclusion:** own drivers meet real operations and survive invalid entries, cancellation and device failures. An incompatible driver produces diagnostics. Support for third party Windows drivers also depends on phase 12.

**Result:** completed on 16/10/2026 with versioned internal drivers and volatile storage. Implementation, evidence, commands and limitations: [PHASE-6.md](PHASE-6.md). The execution was terminated at this stage.

## Phase 7 — Disk, file system and persistent registry

**Work:**integrate persistent virtual storage and overlays; implement files, directories, reading, writing, renaming, deletion, sharing, permissions, flush and recovery. Complete virtual registry with types, roots, enumeration, deletion, notifications, and permissions. Treat paths, links, and separation between guest volumes and host storage.

**Conclusion:** data persists after restart; denied operations do not change state; write failures have defined behavior; changes only reach the guest's authorized storage. No path received from an application becomes arbitrary access to the host file system.

**Result:** completed on 16/10/2026 with limited own format, persistent overlays and alternating snapshots for recovery. Implementation, evidence, commands and limitations: [PHASE-7.md](PHASE-7.md). The execution was terminated at this stage.

## Phase 8 — Loader, runtime and console applications

**State: in progress.** First milestone implemented: loader in guest and own console with DLL/TLS; validation and limits on [PHASE-8.md](PHASE-8.md). The second milestone adds file APIs, log and console, heap and locks; validation and limits on [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md). The third milestone implements creation and termination by return of PE threads, with TEB/TLS private and containment; criteria and limits on [PHASE-8-THREADS.md](PHASE-8-THREADS.md). The fourth milestone adds PE children and explicit closure of processes and threads, including the continuation without the main thread. Criteria and limits on [PHASE-8-LIFETIME.md](PHASE-8-LIFETIME.md). The fifth milestone executes unwind virtual v1 and captures calls in private stacks; does not call handlers nor resume contexts. Criteria and limits in [PHASE-8-UNWIND.md](PHASE-8-UNWIND.md). The continuation implements order/return of exceptions, unwind non-local, dynamic DLLs and the failure matrix. Linux acceptance has been approved; formal conclusion depends on the native Windows matrix of this review. Criteria and evidence in [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md). The phase 9 was subsequently authorised; see [PHASE-9.md](PHASE-9.md).

**Work:**Complete PE loading, relocations, imports, DLLs, TLS, process startup and thread, PEB/TEB, exceptions and unwind. Implement the NT/Win32 set required for the initial matrix and integrate real heat and lock. Remove dependencies from temporary patches on the declared supported paths.

**Conclusion:** Selected console applications start and end correctly, load DLLs, raise children and threads, treat exceptions and use files and record. Each application should have behavior criteria, not just return without crash.

## Phase 9 — Full Snapshots and VM Management

**State in 18/10/2026: implementation and Linux acceptance completed; pending Windows acceptance.** Started by new explicit user request, preserving the pending Windows acceptance of the phase 8. Includes snapshots, life cycle, cloning, persistence, recovery and simultaneous MVs. Criteria and current evidence: [PHASE-9.md](PHASE-9.md). Do not start the phase 10 automatically.

** Work:** Capture and restore CPU, memory, mappings, scheduler, objects, clocks, devices, files, record and disk consistently. Implement create, start, pause, restart, shut down, clone and delete VMs; configure features and import supported images. Distinguish disposable and persistent environments and recover interrupted operations.

**Conclusion:** Restore a snapshot returns the features to the same logical state; disposable environment does not change their base; cleaning and cloning do not reach out-of-town VMs. Simultaneous execution of multiple VMs respects their limits and identities.

## Phase 10 — Virtual network and containment

**State in 18/10/2026:** implementation and acceptance approved Linux, with 81 executions CTest and 132 Boots; native Windows gate pending. Profile and limits on [PHASE-10.md](PHASE-10.md). The phase 11 was later authorized in a new request.

**Work:**implement guest network device and stack, networkless mode, simulated services and laboratory isolated network. Add traffic capture, DNS and VM policies. External proxy access should be an explicit option, with defined destinations and permissions.

**Conclusion:** Each profile allows exactly declared traffic; LAN and private host services remain blocked by default; events and failures are recorded. Tests also check IPv6 and other supported output paths.

## Phase 11 — Graphics, input and application windows

**State in 18/10/2026:** explicitly authorized by the user after Linux acceptance of the phase 10. Implementation of the approved Linux matrix and acceptance: 96 executions CTest and 132 Regression boots; native Windows gate pending. Evidence and limits on [PHASE-11.md](PHASE-11.md). The phase 12 It wasn't initiated.

**Work:**implement virtual output and input, window manager and graphical APIs of the initial matrix. Download validated pixels and metadata to the host, forward keyboard/mouse and address focus, cursors, dialogues, DPI and multiple windows. Introduce acceleration only as actually implemented capabilities.

**Conclusion:** Selected graphical applications draw and respond to interaction; their events do not interfere with actual windows outside the authorized channel. Clipboard, drag files, printers, USB and shared drives have independent permissions, initially disabled.

## Phase 12 — Extended compatibility, SMP and additional devices

** Work:** Expand APIs and profile behavior, including the desired differences of XP, View, 7, 8, 8.1, 10 and 11. Develop and validate x86 support when needed; a JSON profile does not replace actual architecture support. Add multiple vCPUs and SMP synchronization, firmware/platforms predicted, audio, additional classes of devices and interfaces required for selected Windows drivers. Treat TPM and GPU requirements by effective capacity.

**Conclusion:** matrix by version, architecture, application, driver and device with approved states, partial or unsupported. Installing drivers in the guest does not install drivers in the host. Announced compatibility matches the evidence; there is no presumption that any Windows application or driver will work.

### Additional matrix of Phase 12 applications

Flappy Window 1.5 provided by the user becomes a candidate application for an explicit batch of compatibility. The evaluated file has SHA-256 `f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6`. Its initial test in the microsandbox did not open the game: there were 167 unimplemented imports and exception #UD during startup, whose exact cause remains to be investigated.

The corresponding batch needs to integrate loading and booting of PEs external to the kernel itself, runtime C, dependencies, exceptions, threads and USER32/GDI/DWM operations actually used. Counting recognized functions or returning stubs success does not prove compatibility. The game criterion requires opening the window, drawing, receiving guest entry exclusively, performing the match, reading your settings on the virtual disk and closing them playable. This batch is not completed by the first 12A delivery.

**Advance in 23/10/2026:** the new experimental contract receives a separate PE from the kernel and connects a subset of USER32 to the guest composer. The own reference does not close Flappy's acceptance: image boundaries, runtime and APIs remain pending. [Matrix, testing and evaluation of 12F](PHASE-12-GUI.md).

### Subphase 12A — Consistent profiles of hardware and drivers

**State in 19/10/2026:**completed for the reference matrix. Identity in CPUID, firmware and registration, import INF and actual execution of the driver itself in the guest, including failures and snapshots, have been validated in both backends. Commercial drivers, CAT authentication and general Windows compatibility are not declared supported. [Matrix, tests and limits](PHASE-12A.md). Stop performed before 12B.

**Dependencies:**phase 0 and infrastructure of phases 6, 8, 11 and 12.

**Work:** offer host-based templates/capabilities profile and CPU, GPU, disk, BIOS and virtual motherboard editor. Allow custom models to be registered, identifying what is implemented. Centralize information presented by device enumeration, virtual firmware, registration and invited MiniNt/kernel, with synthetic identifiers persisted in the profile.

Predict import of selected driver packages from the host or provided by the user for exclusive use in the guest. Check architecture, NT version, dependencies and virtual device required. Register separately imported package, installed driver, initialized, functional or incompatible.

A profile called GT 1030 will only announce operation with your driver after the required interfaces and behavior are implemented and tested. Until then, show partial support. Selecting the model or importing the package does not guarantee installation or operation. Apply the same criterion to other devices.

**Conclusion:**Consistent inventory between the paths implemented, absence of real unique host identifiers and tests of the drivers declared supported.Incompatible profiles have diagnosis.Stop at completion.

### Subphase 12B — RAM, cores and virtual disk with quota

** Dependencies:** Phases 3, 7 and 9; Phase 12 PMS when requested.

**Work:** allow you to choose RAM and vCPUs, including a configuration that uses the largest available portion of PC resources. Apply budget by VM and aggregate, maintaining operating reservation for Windows, VMM and service. Display requested, admitted and provided values. Announced resources need to have implemented behavior; do not promise all physical RAM while the host also needs it.

Allow to choose folder and virtual disk growth limit. Your profile can use host disk template, manufacturer and reference capability without accessing physical disk or its files. Separate logical capacity, virtual volume free space, available to the backup file's physical quota and occupancy. Also consider real space in host drive, overlays and snapshots.

When reaching the quota, availability for new recordings must reach zero and operations must return appropriate space/quota error. Queries and writings must agree, including after deletion, restart and restore. Do not show unlimited usable space while operations fail by exhausted quota.

**Conclusion:**Regarded budgets and reserves, limited growth and recoverable treatment of lack of space, without silently exhausting the host.

### Subphase 12C — System behaviour loyalty

**State in 21/10/2026:** Implementation and validation of Linux approved in the original HD folder: 50 Debug, 50 Release, 32 ASan/UBSan and 1,590 invited observations. There are no native Windows references per version for formal closure. 12B maintains its Windows acceptance tokens and integration to PE profiles. [Matrix, evidence, delivery and differences](PHASE-12C.md). Do not start 12D by this authorization.

** Dependencies:** Phases 2–8 and subphases 12A–12B.

** Work:** Create compliance tests for time, exceptions, synchronization, memory, handles, I/O and API results. Compare with reference environments authorized by architecture and NT version. Prioritize differences that change application decisions and remove provisional responses from supported paths. Check consistency between announced capabilities, executed instructions, topology, devices, registration and queries.

Wear consistent watches for waiting, time elapsed, and shared data. Each corrected divergence receives regression test; intentional differences are documented.

**Conclusion:**Replicable conformity matrix, with corrected or declared differences and without stubs in the behaviors announced as complete. Stop at completion.

### Subphase 12D — Long sessions, resets and interaction

**Continuation authorized in 21/10/2026:** sessions, suspension, scripts and manual interaction implemented and accepted on Linux: 139 CTest plays Passed (53 Debug, 53 Release, 33 ASan/UBSan). Native Windows acceptance and previous to-ed remain open. [Results and limits](PHASE-12D.md).

** Dependencies:** Phases 2, 7, 9 and 11.

**Work:** allow configurable duration, multi-session continuity and restarts, preserving the application status and persistence predicted in the profile. Provide manual interaction and mouse/keyboard scripts sent exclusively to the guest windows.

Keep real time as initial reference. Virtual time experimental modes should be configurable, recorded and consistent with available timers and clocks. Do not modify the host clock or complete waits in advance without semantics set. Measure the effect of these modes on the analysis.

**Conclusion:** harmless programs that wait, resume after restart and depend on interaction follow the expected script; sessions are restoreable and entry does not reach host applications. Stop at completion.

### Subphase 12E — Environment for use and synthetic data

**Implemented and accepted on Linux on 22/10/2026:** 146 CTest approved plays without Wine; documents opened by the reader, six-pair reproduction NT/seed, persistent evolution and privacy testing. Native Windows acceptance and previous to-ed remain open. [Register, delivery and limits](PHASE-12E.md). Phase 13 has not been started.

** Dependencies:** Phases 7, 8, 9 and 11.

** Work:** Create profiles versioned with accounts, installed programs, test documents and history generated within the guest. Files, shortcuts, associations, registration and dates must agree to actually supported operations. Prepare scenarios by NT version, with reproducible seed and possibility of preserving VM evolution.

Documents and histories will be synthetic, without automatically copying accounts, credentials, navigation, or personal files from the host. Explicit imports follow the channel and transfer permissions. Documents must open and programs declared installed must work in the corresponding profile.

**Conclusion:** reproducible and functional profile, consistent information and test of absence of personal data from the host. Stop at completion.

### Subphase 12F — Windows desktop integrated windows

**Delimited delivery on 26/10/2026:** [PHASE-12F.md](PHASE-12F.md). Windows main/dialogues corpus Win10 x64 appear separately on Windows, with program, DLLs and callbacks in MiniNT. Images by window, validated protocol, input per session, modality, geometry and virtualized DPI. Acceptance requires 139 Release tests, 64 Debug and validated extracted package. The unique viewer continues as an alternative. The content is rasterized by MiniNT and presented as images; this delivery does not implement the forwarding of drawing operations to native Windows APIs. The requested evolution of native rendering is in **14.10A–14.10E**, separated from the acceptance of this 12F.

** Dependencies:** Graphical output and phase 11, loading and booting of external PEs into the kernel itself and the USER32/GDI batch of phase 12 required for the chosen applications. Zoom DWM and other APIs according to the matrix effectively used, without assuming general compatibility.

**Work:** offer an optional mode in which each window of a guest application appears as an independent native window on the Windows desktop. The application and its libraries continue to run on the guest. A presentation component in the host creates and keeps the corresponding windows, receives pixels and typed and validated graphics commands and returns keyboard/mouse events only to the session and the associated guest window.

The bridge translates window identifiers, coordinates, focus, size, titles, DPI, closing and kinship/mode. Real host identifiers and pointers do not cross the border. There will be arbitrary referral of USER32/GDI calls, Windows messages, callbacks or guest code for execution in the host. Operations outside the contract return explicit error.

Apply window quantity limits, dimensions, memory, commands and event rate; also validate text, images and state transitions. Restrict operations to windows belonging to that session without reaching other applications, injecting global input or installing global hooks. Clipboarding, dragging files and other transfers remain subject to independent permissions, initially disabled. Predict visual indication of origin in the sandbox and wipe windows in failure, closing or restoring the session.

**Conclusion:** a proper application and at least an external PE of the selected matrix open independent windows, draw and receive input; validate multiple windows, focus, resizing, DPI, modal dialogue and closing. Tests with malformed commands, identifiers of another session, excess features and unexpected closure must prove refusal or contained termination, without input or changes to external windows. Register support by application and operation and preserve the current viewer as an alternative. Do not declare GPU acceleration or GPU passthrough without implementation and validation. Stop when completing, before starting another batch or phase.

## Phase 13 — Telemetry, policies and behavioral analysis

**Implemented and accepted on Linux in 22/10/2026:** 153 CTest plays approved, flawless or ignored. Nine domains collected, policies and real effects on the guest, rules, export and status differences. Native Windows acceptance and previous to-ed remain open. [Register, delivery and limits](PHASE-13.md). Phase 14 has not started.

** Work:** Gather processes, threads, modules, files, registry, services, drivers, network and attempts blocked in a timeline. Create correlation and versioned rules for persistence, ransomware and other behaviors; distinguish legitimate, suspicious, confirmed and inconclusive. Integrate policy to allow, deny or simulate coherent effects exclusively in the guest state. Export JSON/HTML reports and state differences.

**Conclusion:** own test programs generate expected events and ratings, with traceable authorship and motive. Failures or loss of telemetry are visible. Results received from MV are treated as unreliable inputs.

## Phase 14 — Scanner, rules and threat intelligence

**23/10/2026 delivery:**implementation and own tests described in [PHASE-14.md](PHASE-14.md); Windows acceptance: 83/83 Release and 3/3 Debug of the scanner, without ignored. Linux Debug: scanner/pipeline 2/2 and CLI 1/1, with partial acceptance. The `unknown` result does not release files, and instaladores/shapes without support remain inconclusive. Phase 15 awaits further authorization.

**Work:**Evolve existing scanner: SHA-256 and auxiliary hashes, signatures, YARA rules with verified license, PE analysis, scripts, installers and compressed files. Integrate configurable connectors for MalwareBazaar, VirusTotal and CIRCL hashlookup according to their APIs and current conditions. Implement SQLite, cache with validity, request limits, offline mode and justified rating. Forward unknown to sandbox by policy.

**Conclusion:** harmless positive cases and clean corpus have reproducible results; errors, unsupported formats and limits are not classified as safe file. Queries do not send complete files without explicit consent. Compressed files have expansion limits, depth and time.

## Phases 14.x — Compatibility of DLLs and Windows services by NT version

**Priority defined in 23/10/2026:** perform this compatibility sequence before phase 15, taking advantage of the kernel, runtime, profiles and the existing GUI prototype. 14.1 was implemented by further request: [contracts, inventory, tests and limitations](PHASE-14.1.md). 14.2 was implemented by new request: [external loader, tests and limits](PHASE-14.2.md). Each subphase requires request and ends with delivery and stop. The pending phases 8–13 remain recorded; the batches below integrate and expand these paths without declaring their completion retroactively.

### Chosen architecture and common criteria

**Hybrid approach:** implement in MiniNT the compatible services and contracts required and gradually reuse original user-mode DLLs when the loader and its dependencies are proven supported. Keeping your own DLLs for already implemented contracts and as a test reference. The selection of each implementation will be explicit in the VM manifest, without silent substitutions. The objective is to expand functional compatibility, with the possibility of differential tests between own and original implementations.

Applications run in separate processes within the guest in user mode, with their DLLs loaded into the respective process and attended by the MiniNT kernel. The management process in the host controls the VM; it does not freely execute program imports as local Windows calls. The graphical bridge planned in **14.10A–14.10E** will have an explicit list of GUI operations translated into Windows APIs in a restricted auxiliary process. EXE, imported DLLs and application callsbacks will continue in the guest; it is not about loading your code into the host or forwarding general syscalls. Original DLLs do not replace kernel, graphical subsystem or other processes services they depend on. Reuse existing components before creating parallel implementations.

DLL packages will have origin, architecture, file version, build/revision reference and registered hashes provided from a user-authorized installation or media. Their validation and execution will occur in the guest; do not load these DLLs into the host manager to discover dependencies. Avoid mixing components from different installations; mixed own/original packages will only be accepted with explicit contracts and tests. Repository and project builds will not automatically include imported Windows binary.

**Meta of coverage:** seek broad execution of Windows desktop programs, starting with x64. The names of DLL below represent priority families, not a guarantee that your presence, or all of your exporteds solved, will make most of the programs work. Each batch will keep inventory by DLL/symbol/ordinal/contract, NT version and behavior: absent, partial or validated. An implementation will only be called complete for an explicitly delimited and tested scope; unsupported functions will not return fictitious success. The full coverage of each family continues as a long-term goal, with new batches for the remaining items.

All batches integrate the new effects to existing policies and telemetry, include invalid entries, resource failures, competition, cleanup, relevant regressions and acceptance Windows Debug/Release. Native comparisons use harmless test programs in target version references; external applications are executed exclusively on guest. Evidence identifies what was just inventoried, loaded, initialized or actually executed until they meet their usage criteria.

### Subphase 14.1 — Versified contracts and compatibility matrix

**Implementation of 23/10/2026:**Persistent manifest with SHA-256, three x64 targets, meeting/snapshot link and observed static/dynamic inventory of dependencies. Version queries use their own contract without stating complete ABI Windows. Native references of exact builds and new Windows services remain pending. [Acceptance and Continuity](PHASE-14.1.md). Stop at this subphase; the 14.2 requires new request.

** Work:** Consolidate the existing inventory of 8/12 and external applications phases; register static, dynamic, ordinal imports, data exports and transitive dependencies. Define the unique manifest of the NT profile and runtime package, its persistence and the test matrix before expanding functions.

Use initially **Windows 10 x64, NT 10.0.19045**, as the main reference; zoom with **Windows 7 x64, NT 6.1.7601**, and **Windows 11 x64, NT 10.0.22631**, already present in the profiles. Also fix revision of the components used. Absent references are pending without inferring compliance from another version. Windows 10 and 11 share NT 10.0; build, revision and capabilities are part of the selection, not only major/minor.

The profile should govern availability and API signatures, API sets resolution, syscall tables, used layouts, information classes, NTSTATUS/LastError, times, synchronization, memory, files and graphical rules. Check PEB/TEB, shared data, registry and version queries, including documented differences of GetVersionEx/VerifyVersionInfo as per application manifest. Do not force all queries to return the same string when Windows itself differentiates contracts.

Fixing the profile during the session; switching version requires restarting with coherent package. Snapshots and reports link profile, contract version and hashes of modules. Refuse unsupported combinations. XP, View, 8 and 8.1 identity profiles do not have binary compatibility as they already exist in the inventory; their promotion depends on batches of compliance and 14.12.

**Conclusion:** Revisable matrix and manifest, explicit references and gaps, order of symbols by dependency and tests that distinguish profiles, including refusal of incompatible combinations.

### Subphase 14.2 — External charger, modules and scalable memory

**Implementation of 24/10/2026:** External PE up to 8 MiB, catalog up to 16 modules, unchangeable guest volume with controlled search, forwarders, ordinais/data, early delay imports, numerical resources and manifests/contexts delimited. Tests with own DLLs and three NT profiles; services of original Windows DLLs remain pending. [Contract, acceptance and limits](PHASE-14.2.md). Stop at this subphase; the 14.3 requires new request.

**Dependencies:** 14.1 and basis of memory/PE phases 3/8/12B.

**Work:**overcome the limits of 128 file/image prototype KiB, four modules and private page budget; scale metadata, batteries, TLS and quotas together. Load EXE and DLLs of the guest volume with controlled search, forwarders, API sets per profile, imports by name/ordinal and data, delay imports, features, manifestations and activation contexts according to the matrix. Complete LoadLibrary/FreeLibrary/GetProcAddress, DllMain/TLS order, references, loader lock, rollback and section protection.

**Conclusion:** Larger external applications and libraries, with dependencies already met by the existing runtime, start at CPL3; boot failures or limits release resources. Test the loader with independent libraries; acceptance of original Windows DLLs that require new services belongs to the following subphases. It is not enough to map the EP. Stop when you finish.

### Subphase 14.3 — NTDLL and native MiniNT services

**State 24/10/2026:**implemented for the cutout documented in [PHASE-14.3.md](PHASE-14.3.md): 48 native services with limited arguments, own RTL/Ldr and trial of an original NTDLL Win10. Full NTDLL initialization, all of its exportations and exact native references per version remain pending; do not declare general Windows compatibility. Stop at this subphase.

**Dependencies:**14.2; reuse current processes, memory, objects and runtime.

**Work:** Extend the Nt/Zw, Rtl and Ldr contracts required by `ntdll.dll`: Virtual memory and sections, objects/handles, cases/threads, waits/timers/APCs, information query, files/log, heap, TLS/FLS, exceptions and unwind. Connect native calls to the actual state of the guest kernel; tables and architecture/build-dependent structures belong to the validated profile, without reusing the host table. Own and original libraries should reach the same invited services. Validate pointers, sizes, rights and transitions in a way.

**Conclusion:**Initiation and native matrix routines work with results compared to the reference, including exceptions and effects on memory/objects; unknown calls have explicit results and telemetry.

### 14.4 subphase — KERNEL32, KERNELBASE and basic contracts Win32

**State in 24/10/2026:**implemented for own matrix documented in [PHASE-14.4.md](PHASE-14.4.md): console, threads/pool, isolated external children, shared FS, mappings, I/O asynchronous, environment, features, Unicode and API sets v2/v6. KERNEL32/KERNELBASE original, general Windows compatibility, full NLS and larger file quotas remain pending. Stop at this subphase; the 14.5 requires new request.

**Dependencies:** 14.3.

**Work:**implement and/or enable `kernel32.dll`, `kernelbase.dll` and its contracts `api-ms-win-core-*`/`ext-ms-*` effectively present in the profile. Cover heap/memory, processes and children, threads/thread pool, synchronization, console, files/mapeamentos/I/O asynchronous, environment, paths, resources, time, Unicode/NLS, modules, error handling and system information. Solve API contracts set by the version schema, without assuming a physical file for each imported name.

**Conclusion:**external console programs use threads, children, files and end-to-end exceptions; aliases and forwarders preserve the behavior and errors of the target version. Stop when completing.

### Subphase 14.5 — Runtime C/C++ and application startup

**Delimited delivery:** [PHASE-14.5.md](PHASE-14.5.md), MSVC 19.44 `/MD`, FH3/FH4, original CRT Win10/VC143 and documented refusals. It does not mean general CRT compatibility; rethrow/nested C++ remain pending.

** Dependencies:** 14.4 and 14.3. exceptions support

** Work:** Meet `msvcrt.dll`, `ucrtbase.dll`, `api-ms-win-crt-*`, `vcruntime140*.dll`, `msvcp140*.dll` and additional versions required by the corpus. Cover data exports, startup/termination, argv/environment, stdio/files, allocation, locale/Unicode, mathematics, initializers, TLS, C++ exceptions, and integration with unwind/locks. Distinguish runtime versions of the NT version and check redistributible requirements before accepting your package.

**Conclusion:** External torques of selected toolchains start, use CRT and C++, treat errors/exceptions and end with correct results and resources; reuse of original runtime is validated in the guest. Stop at completion.

### Subphase 14.6 — ADVAPI32, security, registration and services

**Delimited delivery:** [PHASE-14.6.md](PHASE-14.6.md). own ADVAPI32/SECHOST/USERENV DLLs in the Win10 x64 profile, reopened settings in another session, tokens retained by the kernel, canonical DACLs and test service under LocalService. SCM preserves its process after the creator's output and collects resources after failures. The matrix does not match complete Windows DLLs; logon, impersonation, general ACLs, PRC, autostart and other profiles remain pending.

** Dependencies:** 14.4; integrate persistence and safety of 5/7. phases

**Work:**Enlarge `advapi32.dll` and, when applicable, `sechost.dll`, `userenv.dll` and related contracts: Registration, tokens/SIDs/ACLs, permissions, account profiles, event service and guest service manager.Implement IPC dependencies necessary for these scenarios; service processes have their own identity and service life within the VM.

**Conclusion:**external applications persist settings and run a test service on the guest with real authorization and isolation between accounts; handles references and features survive faults consistently. Stop when completing.

### Subphase 14.7 — USER32, GDI and external graphical compatibility

** Windows Delivery:** own graphic runtime, single viewer and acceptance corpus with the original Flappy, MSVC applications, persistent settings and input physical/text Acceptance requires separate 116 Regressions Release and 41 Debug checks, in addition to the validated package. [Implementation, evidence and limits](PHASE-14.7.md). The 12F uses this basis in the delimited delivery described in [PHASE-12F.md](PHASE-12F.md).

**Reliances:** 14.2–14.6 and graphs/entry existing; promote the to-ed of the phase 12 plot for this delivery, preserving its history.

**Work:**Enlarge `user32.dll`, `gdi32.dll`, `gdi32full.dll`, `win32u.dll` where there are in the profile, and `dwmapi.dll`.Implement the corresponding support in the MiniNT graphic subsystem: creation/hierarchy of windows, styles, movement/redimensionamento, coordinates/DPI, focus, synchronous and asynchronous messages, painting/invalidation, source code/text, GDI objects and timers. Input will have physical keys pressed/loose, modifiers, text and mouse with distinct contracts. Integrate settings and files to the guest disk; enlarge dimensions/quotas of the viewer as per application requirements.

**Conclusion:** Flappy Window 1.5 provided by the user opens, draws text, receives the commands required to play, moves its windows, read/record settings when applicable and ends in playable mode, as well as other applications independent of the matrix. Check regressions and isolation; do not confuse resolution of the 178 imports inventoried with functional execution. Reevaluate and register the prerequisites of **12F**, which remains a separate delivery, request-dependent. Stop when you finish.

### 14.8 Subphase — COM, RPC, Shell and Desktop Controls

**Delimited delivery:** [PHASE-14.8.md](PHASE-14.8.md). Factories in DLL and separate process, STA/MTA, IDispatch marshalling, automation, local PRC, properties, controls and own dialogues. The matrix requires Release regression, Debug acceptance and package execution in both settings. It does not represent complete COM/NDR or activate host services. 12F remains separate.

**Dependencies:** 14.6–14.7.

** Work:** Meet `ole32.dll`, `combase.dll`, `oleaut32.dll`, `rpcrt4.dll`, `shell32.dll`, `shlwapi.dll`, `comctl32.dll`, `comdlg32.dll`, `uxtheme.dll` and property families required by applications. Implement With, marshaling/IPC startups, classes/servers activation in guest, automation, controls, dialogues and Shell operations in your namespace. Auxiliary services run on VM; no activation is delegated to the host COM/Shell.

**Conclusion:**external applications with controls/dialogues and COM components in process and outside of it fulfill complete usage scenarios, with errors and closure of servers tested by profile. Stop at completion.

### 14.9 Subphase — Network, HTTP, encryption and certificate

**Delimited delivery:** [PHASE-14.9.md](PHASE-14.9.md). Ten own DLLs, limited layer 4 transport, HTTP/WinINet, TLS 1.2 and keys/certificates virtual for corpus Win10 x64. Offline network by default; no TLS, DNS or personal credentials of host. Required Matrix: 133 Release and 58 Debug, with stable evidence in private build.

** Dependencies:** 14.4/14.6/14.8 and contained network of phase 10.

** Work:** Meet `ws2_32.dll`, `mswsock.dll`, `dnsapi.dll`, `iphlpapi.dll`, `winhttp.dll`, `wininet.dll`, `bcrypt.dll`, `ncrypt.dll`, `crypt32.dll` and `secur32.dll` as per corpus. Cover sockets/DNS/HTTP, asynchronous operations, TLS, key storage, certificates and virtual credentials. Maintain offline/simulated/network authorized modes with documented behavior and policy applied on the virtual channel; do not use personal host certificates, keys or credentials.

**Conclusion:**Test clients make real communication and cryptographic validation in controlled scenarios, with policy negatives, invalid certificates, timeouts and observable cancellations. Stop on completion.

### 14.10 subphase — GUI native to host, DirectX, media and audio

**State:** 14.10A with delimited implementation on [PHASE-14.10A.md](PHASE-14.10A.md); 14.10B with delivery bounded on [PHASE-14.10B.md](PHASE-14.10B.md); 14.10C with delivery bounded on [PHASE-14.10C.md](PHASE-14.10C.md); 14.10D with delivery bounded on [PHASE-14.10D.md](PHASE-14.10D.md); 14.10E–14.10G with delimited deliveries in their phase documents. 26/10/2026 prioritizes making Windows host perform GUI creation and design operations while the application remains in MiniNT. 12F provides session, input and window infrastructure, but its pixel presentation does not meet this requirement.

**Dependencies:** 14.7–14.9, the basis of 12F and contracts of the 6/12A phases according to the device. Reuse the existing bridge; reverse the new contract and declare the backend chosen in the manifest without silently converting the current session.

The lots below have their own criteria. **Stop after each batch**, run your checks, register limits and publish your delivery through the project flow. The delivery of 14.10A does not declare the B–G operations implemented.

#### 14.10A — Native GUI protocol and auxiliary process

**Delimited delivery:** [PHASE-14.10A.md](PHASE-14.10A.md). Own Gateway, window/brush/DC, Real FillRect/GetPixel in aid with restricted token, low integrity, limited IPC and supervised Job. Acceptance requires 143 Release tests, 68 Debug and extracted package. General USER32 is on 14.10B.

**Work:**define commands/replies typed and versioned for specific GUI operations, with session, sequence, authorship and opaque resource IDs. Translate validated structures and copy data; HWNDs/HDCs/HGDIOBJs, real host address and function pointers do not cross the border. Do not offer a generic operation to call DLL, syscall or send arbitrary Windows message.

Perform mediation in an auxiliary process with privileges and inheritance of restricted handles, IPC limited to the session, resources/commands/data quotas and supervised closure. Windows, DCs and GDI objects can only belong to the session itself; refuse expired IDs, incompatible types and external references. Register the native operation executed, result and cleaning in telemetry.

**Conclusion:** a minimal application in MiniNT requires a simple drawing window and operation that the helper performs by the actual Windows API. Prove the native path without requiring an image already rasterized by MiniNT. Test protocol, two sessions, faults/fall of the helper and release of resources. Stop when finished.

#### 14.10B — USER32, native windows and controls

**Delimited delivery:** [PHASE-14.10B.md](PHASE-14.10B.md). SDK Imports USER32, actual windows and BUTTON/STATIC/EDIT, messages and timers, callbacks in MiniNT and restricted auxiliary. Criteria: 147 Release, 72 Debug and extracted package. GDI of general applications is in 14.10C; complete USER32 is not declared.

**Dependence:** 14.10A.

**Work:** forward the declared set of creation/destruction, hierarchy, geometry, styles, focus, DPI, invalidation and timers to the host USER32. Create real class controls allowed, starting with BUTTON, STATIC and EDIT, with translated status and notifications. Do not accept classes, modules or arbitrary guest procedures as executable code in the host.

Maintain application WNDPROCs and callbacks in MiniNT. Set upload/post, synchronous returns, sorting, reentry, timeout and thread closure, including messages containing structures. Do not indefinitely block the host interface thread waiting for a guest response.

**Conclusion:**Independent programs create real Win32 daughter windows and controls, receive cliques/edition/notifications and close with correct order and resources. Check mode, failures, competition and isolation between sessions. Stop at completion.

#### 14.10C — GDI, text, fonts and images designed by Windows

**Delimited delivery:** [PHASE-14.10C.md](PHASE-14.10C.md). DCs and own objects, clipping, shapes, text, metrics, DIBs and copies by actual GDI of the auxiliary, with native comparison. Criteria: 152 Release, 77 Debug and extracted package. Full USER32/GDI are not declared; the 14.10D has its own scope described below.

**Dependencies:** 14.10A–14.10B.

**Work:**Translate the set of DC matrix, selection/release of objects, clipping, fill, lines, text, fonts and bitmaps for real GDI calls such as FillRect, TextOutW/DrawTextW and supported BitBlt/StretchDIBits operations. Data from an application bitmap can be transferred; the full window should not depend on a previous rasterization by MiniNT to be considered native.

Confine DCs and reading/drawing operations to session surfaces and features; do not provide desktop DC, capture other windows or access to external devices. Control allowed fonts, sizes, modes, and quotas. Return metrics, results, and errors by contract. Register Windows host and target NT profile versions: use host GDI does not automatically prove other NT behavior. Differences in fonts, metrics, DPIs, and APIs need comparison and support declared by profile.

**Conclusion:** the same test application runs in MiniNT and a native reference, with text, images, clipping, resizing and compared metrics. Evidence identifies the host's GDI calls; tests include invalid objects, selection/restoration, partial failures and leaks. Stop when finished.

#### 14.10D — Additional controls, dialogues and acceptance of the native GUI

**Delimited delivery:** [PHASE-14.10D.md](PHASE-14.10D.md). Native controls and menus, dialogues in the invited namespace and original Flappy with GDI in the host, without fallback. Criterion: 158 Release, 83 Debug and extracted package. The 14.10E GDI expansion remains pending.

**Dependencies:** 14.8 and 14.10B–14.10C.

**Work:** amplify common/menus controls and dialogs according to selected applications, using controls designed by Windows. Files, registry, processes, COM and Shell remain in the guest namespace. A file selector should work with this namespace; not delegate navigation to the personal disk or activate components/services host arbitrarys. Transfers, clipboard and drag/drop require their own contracts and policies, do not implicitly enter the adoption of native controls.

**Conclusion:** Accepting the original Flappy and independent programs with native GDI and controls/dialogues designs, including input, text, modality, DPI, competition, failures and cleanup. Distinguishing native operations of 12F's unsupported operations and pixel mode in the reports; fallback does not count as approval of the native requirement. Performing relevant regressions, acceptance of Windows Release/Debug and test of the extracted package. Posting the scope effectively accepted; not announcing complete USER32/GDI. Stop when completing.

#### 14.10E — Extended GDI compatibility in Host windows

**Delimited delivery:** [PHASE-14.10E.md](PHASE-14.10E.md), with API matrix, independent ANSI/Unicode corpus and native comparison. Pendencies and restrictions remain explicit; no universal GDI is declared. The 14.10F requires a new request.

**Dependencies:** 14.10A–14.10D.

** Work:** Extend the USER32/GDI bridge as measured gaps in independent programs before starting DirectX. Maintain a versioned inventory of functions, ANSI/Unicode variants and dependencies, indicating for each one: implemented, implemented with restrictions, rejected by policy or pending. Export inventory does not count as implementation.

Expand in batches states and graphical contexts, regions/clipping, paths, curves and transformations, bitmap operations, formats and raster modes, in addition to text, Unicode, glyphs, spacing, metrics and additional sources by controlled catalog. Prioritise the families used by the corpus and publish accepted formats, flags and combinations; do not promise universal GDI.

Creation, presentation and drawing of windows use real USER32/GDI in the host helper, including text and fonts designed by Windows. Application code, DLLs and callbacks remain in MiniNT. Application bitmaps can be transferred by limited contract; a previously rastered window in MiniNT does not satisfy native acceptance.

Each allowed operation specifies arguments, types and authorship of objects, permissions, quotas, errors and life cycle. Validate data on both sides of the bridge, copy buffers and translate own IDs without forwarding hands, callbacks or actual host handles. Desktop, capture of other windows, arbitrary paths of fonts, devices, printing and driver escapes are not implicitly released. A list of names does not guarantee absence of escape.

**Conclusion:** Expand the corpus of 14.10D with independent external programs and publish the API matrix. Compare the same scenarios in MiniNT and native Windows: pixels, text, fonts, metrics, DPI, resizing, competition, and announced NT profiles. Require invalid entries, objects from other sessions, partial failures, quotas and leaks, Windows Release/Debug regressions, and extracted package test. Post approved applications, coverage, differences and gaps; pixel fallback does not count as native approval. Stop when completing and awaiting request for 14.10F.

#### 14.10F — DirectX, 2D drawing and image codes

**Delimited delivery:** [PHASE-14.10F.md](PHASE-14.10F.md). Direct2D by software, DirectWrite text, PNG/BMP/JPEG and D3D11 WARP texture operations with DXGI presentation by copy in own window. The same SDK EXE and an independent sample are compared to native Windows. Shaders, Draw, 3D pipeline, shared resources, physical acceleration and complete DirectX remain pending. D3D9 has no backend; D3D12/OpenGL wait for later batches. The COM matrix explicitly records the refused methods. Stop after validating and publishing this delivery; wait request for G.

** Dependencies:** 14.10A–14.10E and device contracts required.

**Work:**Priorize `dxgi.dll`, `d3d9.dll`, `d3d11.dll`, `d2d1.dll`, `dwrite.dll` and `windowscodecs.dll` as corpus. Set backend by software or virtual device with features, formats, synchronization, quotas and limited protocol. Add D3D12/OpenGL and other backends in batches as measured requirements; do not confuse GPU identity with acceleration or driver support.

**Conclusion:**Independent applications design and decode images in the announced backends, with resource tests, formats, synchronization and failures. Absent backend generates explicit limitation. Stop at completion.

#### 14.10G — Media, audio and application devices

**Delimited delivery:** [PHASE-14.10G.md](PHASE-14.10G.md), PCM/WAV, WinMM and WASAPI shared. Media Foundation, video, compressed media, capture and notifications remain pending; no universal audio is declared. Stop after valid/publish this clipping.

** Dependencies:** 14.10A and, according to the application, 14.10F.

** Work:** Meet `winmm.dll`, `mmdevapi.dll` and matrix audio/media families with buffers, formats, watches, synchronization, cancellation and recovery of devices. Keep files and data in the guest and mediate devices by specific contracts; playback support does not automatically grant capture of microphone, camera or other personal data from the host.

**Conclusion:**Independent applications reproduce formats and use the declared devices, with competition tests, limits, failures and resource release. Stop at completion.

### Subphase 14.11 — Frameworks and installation of applications

**Delimited initial delivery:** [PHASE-14.11.md](PHASE-14.11.md), own console CIL interpreter, WinRT contracts scalars, version features and portable installation on guest. Full CLR/CoreCLR, WinForms complete/WPF, WinUI, Windows App SDK and installers MSI/SetupAPI remain pending from this broad subphase; this clipping does not declare them completed. Stop after valid/publish the batch.

**Continuation of the managed base:**objects, fields, builders, booters, arrays, boxing and selected String operations implemented and validated on the same 14.11 (56/56 Release and Debug).This does not end the broad subphase: there are still no GC/EH/dispatch, BCL and GUI/runtime contracts required by the target applications of the frameworks.

**First chosen graphical target:** .NET Framework 4 x64 contracts in the NT profile 10.0.19045, independent example Rosetta Code Window creation (C#), fixed by hash, and own acceptance controls. Selected Form/Button/Label/TextBox use Windows windows and controls; CIL and events remain in MiniNT. The matrix includes opening, receiving input, closing, installing, resuming and removing, without mouse or desktop keyboard. This clipping does not load the Microsoft CLR, does not implement full WinForms and does not complete WPF/WinUI. View playback and limits in the 14.11 document; valid/publish this batch and stop before the next continuation.

** Dependencies:** 14.5–14.10, according to each framework.

**Work:**validate selected runtimes .NET/CLR, WinForms/WPF, WinRT, Windows App SDK/WinUI and third-party matrix frameworks.Implement activation contracts, metadata and additional components each one requires; import runtime does not replace these dependencies.Include `mscoree.dll`, `clr.dll`, `coreclr.dll`, `msi.dll`, `version.dll`, `setupapi.dll` and other modules according to target package.Installation/extraction and auxiliary services occur in guest; installers requiring drivers remain dependent on specific support.

**Conclusion:** at least one external application per framework declared supported installs/start, executes its scenario and closes; cases without support have diagnosis. Do not announce all .NET/WinRT by an approved example. Stop when finishing.

### 14.12 Subphase — x86 applications and additional NT profiles

**First batch 14.12A in 29/10/2026:**PE32 in x86 compatibility mode, 32-bit own DLLs, TEB/PEB32, search for native SysWOW64 and USER32 with callbacks stdcall in MiniNT to NT 10.0.19045. The external console is independent; the GUI validated in this batch is unique. [Implementation, proofs and limits](PHASE-14.12.md). **The wide phase remains open:**Independent GUI corpus, redirection of files/log, TLS/SEH x86, plus APIs and acceptance of additional profiles.

** Dependencies:** 14.1–14.8; other families according to the application.

**Work:** add PE32, x86 execution and compatibility layer equivalent to the purpose of WOW64, with ABI, structures, DLLs, exceptions and redirection of 32-bit files/log. x64 DLLs do not meet x86 process imports. Extend View/8/8.1 references and legacy profiles on demand, with separate acceptance by architecture and build; XP 5.1.2600 is x86 profile, while XP x64 uses NT 5.2 and requires own identification. Fix profile validation before allowing these combinations, without promoting the current inventory to binary support.

**Conclusion:**external x86 console and GUI applications work with separation of architectures and each additional profile has its references and regressions approved. 16-bit programs and ARM stay out of this batch. Stop at completion.

### Subphase 14.13 — Unified Runtime, Analysis and Recovery

**Delimited delivery of 29/10/2026:** common execution input, legacy fallback queue, packages by SHA-256, author of effects and actual recovery of basic USER32 x86/x64 in the NT profile 10.0.19045. [Implementation and acceptance matrix](PHASE-14.13.md). **Continuation 14.13A:** automatic preparation with local models, progress, save/resume in the app and services effectively updated, and real corpus with incompatibilities recorded in [PHASE-14.13A.md](PHASE-14.13A.md). Prioritise explicit auxiliary files and CRT locks, contracts, ordinals, console and dialogues observed before expanding the corpus. Continuity x86/GDI ed 14.12 and the broad acceptance of the 14.14 They're still open. GDI/DirectX/audio live, timers/sends pending and connected transportation remain outside the checkpoint contract; other families follow the limits already stated.

** Dependencies:** batches of compatibility used and 9/13/14. phases

**Work:**integrate external loading and new DLLs to the same kernel, storage, devices, policies, telemetry and life cycle of the analysis. Replace legacy PE routing of the scanner by the validated path in the MiniNT, preserving inconclusive result in the face of failure or incompatibility. Register authorship of the effects and native calls, profile and modules; functions performed entirely within DLLs require specific instrumentation if included in the scope of observation, as they do not necessarily generate a syscall. Complete snapshots/restoration with external modules, processes, GUI, quotas and persistent state; link all artifacts to the profile and hashes of runtime.

**Conclusion:** an external EXE sent by the scanner scans the kernel itself, generates report with assigned effects and supports recovery tested; undue exchange of package/profile or snapshot is refused. Stop at completion.

### Subphase 14.14 — Acceptance of broad compatibility by NT version

**Evaluation delivered in 29/10/2026; wide acceptance goals not achieved.** 32 applications frozen corpus, 13 x86/18 x64/1 AnyCPU. In Release and Debug, the automatic flow Win10 had 1 functional script, 1 partial and 30 blocked; the Win7/Win11 profiles are not offered by this flow and were not evaluated. [Method, limitations and readiness for phase 15](PHASE-14.14.md). The development of the quarantine may proceed on request, with inert files and static verification; this does not approve the product for launch.

** Dependencies:** all batches required for the announced application classes.

**Work:** keep corpus versioned from independent and harmless programs, with sampling set before evaluation, covering console, classic GUI, utilities, installers, network, COM, frameworks, media and x86/x64. Register version/hash, NT profile, dependencies, script and result per program: starts, complies with predicted, partial or incompatible use. Test different versions of the same program without counting each internal test as an independent application.

Measure the proportion of applications that complete the predicted use by category, architecture and profile, performance, failures and differences in relation to references. Separate implemented calls from compatible applications; compatibility does not measure malware detection effectiveness. The expression "majority" will only be used for an identified corpus in which more than half meet the criteria, with published results and limitations; do not automatically extrapolate to the entire ecosystem. Define in 14.1 the composition and acceptance goal of this corpus before testing.

**Conclusion:**Replicable matrix and auditable results, without hiding faulty scenarios; gaps have become subsequent batches.Reevaluate the to-ed of phases 8–13/12F and the readiness of phase 15.Stop when completing and waiting for a new request.

## Phase 15 — Quarantine and Threat Management

** Delimited delivery implemented:** user-by-user safe, AES-GCM/DPAPI, transactions, explicit restoration, deletion, authenticated trail and integration with engine/UI. [Contract, acceptance and limits](PHASE-15.md). It does not include safe protected against processes that already control the same account or automatic legacy import.

** Work:** Replace simple storage with consistent format, access control, and metadata. Implement isolation, listing, restoration, destination conflicts, deletion, and track of actions. Treat blocked files, links, power interruption, and lack of space without silent loss of the original or accidental release of the threat.

**Conclusion:**Insulation and restoration preserve the correct bytes and the predicted permissions; failures leave recoverable status; restoration does not allow writing out of authorized destinations. The UI receives the actual result of the operation.

## Phase 16 — Protection service, IPC and real-time

**Delivery implemented:** [service, post-change monitor, queue, schedule, warnings and safe protected ](PHASE-16.md), with administrative permissions and restricted workers. Minifilter/PPL are not included; limitations and evidence are explicit in delivery.

**Continuation of quarantine:**broker and protected storage of the service to resist tampering by processes of the same account; preserve the user's authorization on the paths of origin and destination and the restricted execution of the workers.

**Work:**moving privileged tasks to Windows service with minimum privileges. Implement authenticated IPC, administrative permissions, queues, cancellation and failure recovery. Evolve file monitoring, fast/complete/custom scans, deletions, scheduling and notifications. Add optional minifilter only when needed, with supported deployment and signature. Integrate the actual state of interface protection.

**Conclusion:**Protection continues without open UI, resists restarts and load, does not lose events silently and does not allow unauthorised client commands. The installation does not disable Windows protections or depends on VM to provide basic file verification.

## Phase 17 — Application WinUI 3 and internationalisation

**Work:**integrating the real services in C++/WinRT and WinUI 3, preserving the existing flows during migration.Implement dashboard, verification, protection, sandbox, VMs, behavior, quarantine, history and settings. Apply Fluent Design, themes, accessibility, DPI, keyboard navigation, progress and cancellation. Validate and incorporate JSON from pt-BR, English and Spanish into build, with fallback, parameters, pluralization and regional formatting.

**Conclusion:** all product matrix flows work from end to end, without privileged operations or code analysed within the UI. Languages work without external JSON and can be exchanged as per application support. Resource states unavailable and failure are understandable and correspond to the backend.

## Phase 18 — Updates, Installation and Distribution

**Work:**check integrity and authenticity of rules, bases and binary; implement recoverable updates, data migration and rollback. Prepare EXE/MSI, chosen WinUI runtime, installation of the service and directory permissions. Detect virtualization prerequisites, allow use without sandbox, treat update/repair and uninstall. Review licenses and distribution of components and profiles.

**Conclusion:** Install, update, repair and remove on clean test machines; adulterated packages are refused; interruptions do not leave an installation without recovery. Preserved or removed data follow the user's explicit choice. Windows components are not distributed without proper authorization.

## Phase 19 — Launch, performance and maintenance validation

**Work:**consolidate automated testing, parser fuzzing and protocols, specialized review of isolation and hostile input resistance testing. Run compatibility matrix, false positives, performance and failure recovery; measure CPU/RAM/I/O and optimize queues, cache and incremental scan. Verify multiple VMs, lack of features, restart, update and loss of connectivity. Prepare diagnostic, documentation and correction process.

Also evaluate the five groups of subphases 12A–12E with a versioned suite, initially of harmless own programs. Registering observed signs, differences in the reference and effects on the behavior of applications. Each finding should point to a known correction or limitation. Any proportion of samples that recognize or not the environment requires corpus, method, versions and published limits. Some examples do not support stating that most viruses do not identify the sandbox.

**Conclusion:** measurable criteria defined in phase 0 met, treated critical problems, published limits and complete isolation review for the announced uses.Safety tests, review and performance also happen in the previous phases; this is the integrated acceptance of release.

## Dependencies and milestones

The numerical sequence is the main execution order. Unrequired kernel component can be prepared within an explicitly requested phase, keeping the core priority. Mandatory stop prevents moving to another phase or subphase without further request. Phase 12 compatibility is expanded by batches; it does not indefinitely block the first product with a clearly stated subset.

**Reviewed priority in 23/10/2026:** after delivery of the phase 14, the subphases 14.1–14.14 organize the next expansion of compatibility and precede the phase 15. Start with 14.1 and stop at the end of each subphase. 14.7 feeds the decision to start the 12F; its implementation remains separate and requires request. This order does not transform old pending phases into completed.

| Request of realism | Subfase | Validation |
|---|---|---|
| CPU/GPU, BIOS, plate-motherboard, devices and drivers | 12A | Inventory consistent, interfaces and drivers functional. |
| RAM, cores and host-like disk with limit | 12B | Provisionamento, budget and quotas coherent. |
| Time, exceptions, memory and system operations | 12C | Differential tests by NT profile. |
| Long waits, restarts and entry activity | 12D | Sessions persistent and scripts no guest. |
| Documents, accounts, programs and history | 12E | Profiles synthetic functional and playable. |

| Marco | Evidence required |
|---|---|
| Kernel start | Phases 0–2: own image starts and meets a user call. |
| Kernel multitask | Phases 3–5: isolated memory, preemption, processes and rights. |
| Console Environment | Phases 6–8: selected devices, persistence and applications. |
| Sandbox manageable | Phases 9–10: consistent snapshots, VMs and contained network. |
| Applications graphics | Phase 11 and corresponding batch of compatibility of phase 12. |
| Independent windows on Windows desktop | Subphase 12F: validated presentation bridge, guest application and session restricted entry. |
| Windows host windows, controls and drawing | 14.10A–14.10E, delimited deliveries and planned expansion: mediated GUI operations, callbacks in MiniNT and specific acceptance of the native backend. |
| DLLs Windows no MiniNT | 14.1–14.6: profiles NT, charger and services natives/Win32/CRT proved no guest. |
| Applications external of desktop | 14.7–14.12: GUI, COM, network, graphics, frameworks and x86 according to the matrix per profile. |
| Broad compatibility measured | 14.13–14.14: integrated analysis, recovery and independent corpus with results per NT version. |
| Integrated analysis | Phases 13–16: evidence, scanner, quarantine and service. |
| Product installable | Phases 17–18: interface, languages, installation and update. |
| Version of release | Phase 19 and support matrix approved for this version. |

## What it means to be complete

The product will be complete for a version when it meets the declared functional requirements and compatibility limits, with evidence for each item. Universal compatibility with all versions, APIs, applications and Windows drivers does not receive a promise or a percentage in this plan. The work will be measured by approved criteria, not by the number of functions with the same name.

** Current delivery:** phase 14 accepted on Windows, with partial Linux validation; external phase graphic prototype 12 accepted for its own reference, with limited acceptance of Flappy and windows in the deliveries 14.7/12F. A 14.1 implements contracts, inventory and profile link. 14.2 implements the external loader and volume of runtime, with its own scope and acceptance; 14.3 implements the limited native matrix described in PHASE-14.3.md. A 14.4 adds the Win32 runtime itself described in PHASE-14.4.md. A 14.5 implements the original CRT corpus delimited in PHASE-14.5.md. Rethrow/nested C++ and general CRT compatibility remain open. 14.6 implements the delimited matrix of safety/log/services described in PHASE-14.6.md and PHASE-14.7.md. A 14.8 delivery the delimited matrix of components of PHASE-14.8.md. A 14.9 delivers the network corpus and encryption of PHASE-14.9.md. 12F adds the delimited native presentation of PHASE-12F.md. A 14.10A delivers the clipping of PHASE-14.10A.md; 14.10B and C deliver the clippings of PHASE-14.10B.md and PHASE-14.10C.md; 14.10D delivers the clipping of PHASE-14.10D.md; 14.10E delivers the clipping of PHASE-14.10E.md; 14.10F delivers the clipping of PHASE-14.10F.md; 14.10G delivers the clipping of PHASE-14.10G.md; compressed media and broad acceptance of 14.11–14.14 They remain open. 14.11 the limited contracts of PHASE-14.11.md; a 14.12A delivers the x86 core of PHASE-14.12.mdThe historical results and previous to-ed remain identified in the respective documents.

## Technical references for the new requirements

- [Memory of guest in WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange)
- [Character components and interfaces of graphics drivers WDDM](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture)
- [Free space and space available with quotas](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespaceexa)
- [Ap API Resolution Sets by Charger](https://learn.microsoft.com/en-us/windows/win32/apiindex/api-set-loader-operation)
- [ NT versions including Windows] 10/11 and XP x64](https://learn.microsoft.com/en-us/windows/win32/sysinfo/operating-system-version)
- [GetVersionEx and compatibility manifest](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexa)
- [Execution x86 on Windows x64 and separation of DLLs](https://learn.microsoft.com/en-us/windows/win32/winprog64/running-32-bit-applications)
- [Win32 windows and control classes ](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw)
- [Messages, parameter structures, sending and queues](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues)
- [GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/device-contexts)’s DCs and Graphic Objects

## Documentation of the implemented state

- [Heap and continuation of base](KERNEL-CONTINUATION.md)
- [Virtual memory](VIRTUAL-MEMORY.md)
- [Objects and handles](KERNEL-OBJECTS.md)
- [Threads](KERNEL-THREADS.md)
- [Mutexes and traffic lights](KERNEL-SYNCHRONIZATION.md)

Previous documents record results of the respective steps. This file is a work plan; its pending phases are not completed implementations. The historical architecture of MicroVM must be read with the current decisions recorded here, including the availability of WHP already validated on this machine and the change of priority to a kernel itself in the guest.
