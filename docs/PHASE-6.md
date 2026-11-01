# Phase 6 — I/O, own devices and drivers

Active base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. Image: `guardian-kernel-io.gk`, version 0.6.0, x64, a vCPU, WHP. Status: completed in 16/10/2026. Phase 7 was not initiated.

## Implementation

The kernel now registers, starts, meets requests, and downloads three internal drivers: console, message channel, and block disk. The I/O manager, drivers callbacks, queue, cancellation, and completion run on CPL0 in the guest. The host implements virtual devices in memory and validates the protocol. No driver is installed in Windows host.

`guest/IoManager.cpp` contains the freestanding core. `IoKernel.cpp` links this core to the processes, handles and rights of phase 5, to the scheduler timer and waits. The I/O image reuses the execution of phase 5 processes and adds real operations of I/O by syscall to the parent's user program. The other images continue using their previous contracts.

### Bus and border host/guest

`shared/guardian/Io.h` defines a bus versioned with three descriptors: identifier, class, version, maximum size, capacity, operations and reserved. The kernel validates all fields and memory map coverage. The enumeration is read-only in the WHP and remains supervisor in the guest tables. It is a separate extension from the initial console/control table of the boot contract.

| Area fixed | Address physical | Usage |
|---|---|---|
| Package | `0x10C000` | request and reply of up to 584 bytes |
| Report | `0x10D000` | result of execution |
| Bus | `0x10E000` | enumeration immutable of 152 bytes |
| Port | `0xF0` | OUT of a byte: transaction or report publication |

Requests load version, size, boot session, strictly increasing sequence, request ID, device, operation, offset and length. They are copied from fixed address before validating and executing. The backend does not receive host pointers or user chosen paths. Replies repeat the order identity, inform status and quantity transferred; the driver checks these fields before accepting data or completing the operation.

IN, OUT wide, unknown commands, out-of-run transaction, wrong session, replay and reserved fields produce diagnostics. Enumeration, packages and report use distinct areas. The report is only accepted once, after the process report and before the final CPU report, with the timer unarmed. Malformed requests do not advance the sequence or change the devices. Valid operating failures return limited status.

### Loading and unloading

Each driver has internal contract with version, size, architecture, route and callbacks of start, stop and dispatch. Incompatible Version/architecture/size/flags or missing callbacks are refused with `Incompatible`. Failed start does not publish an active instance. instance IDs have generation to refuse old references after reload.

The start validates the bus and activates the driver path. The stop checks the active instance and disables the route. Downloading is refused while there are handles, external references or requests not yet collected. Invalid device or response failure causes the instance to fail; subsequent orders do not continue sending operations along this route. Downloading and reloading creates a new instance after recovery of pending resources.

At this stage, loading means registering and starting compiled drivers within the image itself. There is not yet a `.sys`, ABI WDM, PnP complete file loader, DMA, third-party Windows driver or hardware passer. The general loader belongs to phase 8; the expanded compatibility to phase 12.

### Asynchronous requests and cancellation

Send a request validates the contract, applies quotas and reserves a completion object before posting the order in the queue. The call returns `Pending`; the backend has not yet been triggered. Writings copy the input data. Changing the user buffer after upload does not change the already submitted bytes.

The existing timer calls the I/O service in CPL0. Orders are withdrawn in order FIFO, dispatched by drivers and completed exactly once. Each request has its own manual event. A thread can wait for this event without occupying the CPU; the completion makes it ready again. Collect the result before completing returns `Pending`.

Cancel an order still in the queue concludes with `Cancelled` and does not perform its operation on the device. A late cancellation returns `TooLate`, preserving the existing result. The actual test accepts the legitimate race with completion and repeats the cancellation limitedly until you exercise the cancelled path. Upon leaving a process, your orders are cancelled/collected and your references are released. Order IDs with generation and process identity prevent you from consulting, canceling or collecting requests from another process or from a previous generation.

Queue and drivers operate in the single vCPU, with masked interruptions within the kernel. Backend dispatch and completion are serialized; there is no concurrent cancellation within callback, SMP, preemptible DPC or dedicated device interruption at this stage. An order cannot be reused.

### Buffer Permissions and Lifetime

The devices are objects of the 5. Open handles table passes through DACL and integrity. Reading requires `ReadMemory`, writing/flush requires `WriteMemory` and download control requires `Modify`. A read handlet does not authorize writing. The identities remain internal to the guest; they are not host credentials.

Each request maintains a reference to the device object until it is collected or removed at the process output. Closing the handle does not invalidate the already submitted request. This is an explicit rule of the ABI itself, without promise to play the files/IRPs semantics of Windows. Completion events, slots and references are released in recovery.

Readings remain in the internal buffer until the application collects the result. The entire destination is validated for writing at this time, respecting the current memory space. An invalid destination is refused without partially copying or consuming the result; the application may try again with valid destination. Kernel buffers and supervisor pages are not accepted as CPL3 buffers.

### Devices implemented

- Console: ASCII writings, with full validation and total budget of 4 KiB.
  Invalid byte writing does not produce partial output. Output is captured by the device and included in boot report.
- Channel: FIFO up to eight messages, each with up to 512 bytes. Writing
  queue; read returns the next message. Small Buffer preserves the message; empty/full channel returns `Busy`. No forwarding to network, shell, files or Windows services.
- Blocks: RAM device with 64 KiB and sectors of 512 bytes. Reading and
  writing transfers real sectors of the array isolated; limits and alignment are validated on both sides. Flush is a sorting point of the queue in memory. It does not represent file persistence or writing on the physical disk.

## Acceptance and regressions

`IoChecks.cpp` performs the same core of 630 verifications natively and in CPL0 in each boot of I/O. Covers incompatible contracts, failure at first, busy download, copying of input, Wait./conclusion, cancellation, quota by owner/global, FIFO, old ID, invalid owner, invalid response, instance quarantine, event exhaustion and 128 cycles with recovery. `GuestIoTransportTests.cpp` adds 95 border checks of the host: framing, sequence, limits, replay, console atomity, ability and preservation of messages, block data and device failure.

In each real boot, the CPL3 program:

1. Opens the disk, confirms that unloading with open handle is refused,
   Sends a script and closes the handle before completion.
2. Sets the original buffer; after waiting for completion, reads the sector by
   another handle and check that the submitted data were preserved.
3. Confirms that written by read handle, offset off disk and destination
   supervisor are refused. Collect reading using a valid destination.
4. Sends and receives an eight-byte message, writes `IO OK\n` on the console and
   cancels a block writing still in line.
5. The scenario of phase 5: rearing the restricted child, controlled inheritance,
   authorized copies and default contained in the opposing process.

At the end, the three drivers were stopped and downloaded; there are no orders, pins, handles or process objects left. Only the 21 frames of the kernel/ociosidade. Bytes space counted by the drivers must match the backend.

The I/O matrix requires 20 bootes with 16 MiB and 20 with 64 MiB per configuration, plus 22 negative external cases: 12 invalid envelopes, five invalid reports, wrong order, wide IN/OUT, removed device and timer missing with watchdog. The disk injected failure produces diagnosed closure without block writing, kernel default or host corruption. Criteria: `config/phase6-acceptance.json`.

## Check result

| Scan | Result |
|---|---|
| Build complete Debug / Release | approved / approved |
| CTest by configuration | 28 approved, zero failures and zero ignored |
| Core of I/O | 630 native checks and on each boot |
| Border do host | 95 verifications by configuration |
| I/O Positive Boots | 80 |
| Phase 1–5 positive regression boots | 400 |
| Cases external negative | 204 (44 I/O, 160 regression) |
| Examples reliable do project | seven per configuration, 14 approved executions |
| Build Debug clean independent | approved, six pictures auditadas |
| Full build artifacts | 57 executables and eight images per configuration |
| Borders freestanding verified | 28 combinations of target/configuration |
| Evidence historical preserved | 237 files with unchanged SHA-256 |

The matrix I/O boots took 164,976–185.445 ms in Debug and 133,972–155,998 ms in Release on this machine; these numbers are not guarantee of time. The image occupies 552,960 bytes of virtual space in Debug and 528,384 in Release, within the limit of 1 MiB of the loader. The six production images of each configuration passed the audits of imports/TLS/relocations/W+X. The two additional CLI bootes by configuration are not added to the 480 bootes of the matrix.

## Build and execute

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-io.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-io.gk --io --ram-mib 64 --repeat 2 --report .\build-codex\phase6-cli-Release.json
```

`--io` selects the extension of processes with phase 6 devices. Incompatible contracts are refused before creating VM. The WHP is used directly, without QEMU and without changes in Windows boot or security settings. Only own programs and the seven reliable examples of the project are executed in validation.

## Evidence and recovery

New results use prefix `phase6-`, including the regressions. 237 historical files from previous phases is in backup `snapshots/codex-before-phase6-20261016-204825/`, along with the twelve previous files changed. 14 new implementation files, tests, configuration and documentation.

In `build-codex/`, the `phase6-full-*-script.log` and `phase6-*-tests-details.log` logs record compilation and CTest. `phase6-boots-*` and `phase6-regression-phase1-*` up to `phase6-regression-phase5-*` save the matrix and its negatives. `phase6-reference-programs.json`, `phase6-smoke.json` and `phase6-cli-*` record examples and CLI. `phase6-image-audit.json` and `phase6-build-boundaries.json` check images and absence of runtime host in the guest. The clean build is in `build-codex-phase6-clean/` and its logs have the prefix `phase6-clean-`.

`phase6-change-manifest.json` records SHA-256 from sources, backups, executables, images and evidence. Revalidate without running programs: `python tools/validate_phase6.py --snapshot snapshots/codex-before-phase6-20261016-204825`. Intermediate attempts have been preserved, including initial correction of reading the OUT command: only the byte transferred is used, following the port contract. LNK4281 remains the expected warning of fixed base of 2 MiB required by the loader.

## Limits and Stop

Up to three internal drivers, eight global orders and four per process, 512 bytes per transfer and one vCPU. IDs and counters are limited; exhausted generations are not recycled silently. Storage of this step is volatile and there is no new file system or persistent registry.

Phase 5 limitations remain valid. This delivery does not declare general compatibility with Windows drivers or applications, nor completion of the antivirus or sandbox invisibility. Persistence, overlays, files and registration will be handled in phase 7.

At the end of phase 6, stop. Phase 7 does not start until further request.

## Semantic references

- [Microsoft: Request queues](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/queuing-and-dequeuing-irps)
- [Microsoft: completion of requests](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/when-to-complete-an-irp)
- [Microsoft: cancellation sync](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/synchronizing-irp-cancellation)
- [Microsoft: Downloading Conditions](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/unload-routine-environment)

They are references to project decisions; the contract implemented is the ABI itself described in this document, not a complete WDM implementation.
