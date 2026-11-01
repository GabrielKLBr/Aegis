# Phase 9 — Full Snapshots and VM Management

Started in 18/10/2026 by explicit user request. **End of implementation for own matrix; approved Linux acceptance; native acceptance of pending Windows.** The Windows pending of phase 8 remains registered. Phase 10 was later authorized in 18/10; its implementation and evidence are in [PHASE-10.md](PHASE-10.md). The numbers below belong to the historical acceptance of phase 9.

## 18/10/2026 result

Evidence final:
`build-phase9/validation-20261018-103055-f78507d1/validation.json`.
Status registered: `passed` / `awaiting_native_windows_acceptance`.

| Matrix executed no Linux | Result |
| --- | --- |
| Debug, Release and ASan/UBSan | 23 CTests per configuration, 69 approved executions, no ignored tests |
| Phase 1–8 regression | 132 real boot with 11 images, two configurations and 16/32/64 MiB |
| Fatal/IST | Four executions correctly recusadas how success |
| Full console snapshot | 81 resumes/boots completed and 27 pauses conferidas, ed three settings |
| Applications for manager | 24 boot completed, including simultaneous copies and failure recovery |
| Snapshot and status arquitetural | 221 configuration checks, including CPUID and pending interruption |
| Catalog | 64 transactions interrupted by configuration and reading during atomic publication |
| First commit do volume | 589 configuration checks, including 52 writing/flush prefixes |
| Preparation of source code Windows | 11 modules compiled for PE/COFF objects by native LLVM; no Windows running |
| Acceptance Windows/MSVC/WHP | **Pending**: 43 CTests by configuration and native comparisons |

The 23 Debug CTests were executed in the new tree `validation-20261018-102029-2fe9ca3a`. Then, the full Debug boot of 64 MiB reached the five seconds of regression. The deadline of this matrix passed to 20 seconds. The verifier checked that only the acceptance script/contract changed, validated hashes of the binary/reports and reused the approved Debug tests. All 132 boots were repeated; Release and Sanitize were compiled and tested on new trees. The manifest records this provenance. There was no subsequent change in the compiled sources during final acceptance.

The first attempt, `validation-20261018-094050-657b339e`, identified a transitory status query during file replacement in the mounted volume. Reading now repeats in a limited way absence queries, keeping the file type check open. A concurrent publication/reading. test has been added Previous attempts and errors remain preserved.

## Deliverable

The new `guardian-vm` creates, starts, pauses, resumes, restarts, shuts down, clones and deletes VMs. Configures RAM and runtime, imports own images `.gk` and disk bases of 64 KiB and keeps snapshots named. Uses separate native processes, a vCPU by VM and own directories, with property controls and mutual deletion. Windows uses MSVC/WHP; Linux uses executables ELF/KVM. O `SnapshotManager` old MiniNt continues as historical reference; the new flow uses `SessionSnapshot` and the kernel itself of the phases 1–8.

A snapshot includes a consistent generation of:

| Status | Capture/restoration |
| --- | --- |
| CPU | General Registers, Control, segments/tables, Debugging, Virtual CPUID Profile, MSRs used by kernel, XSAVE/x87/XMM and pending interruptions |
| Memory | Full RAM, page tables and physical permissions per page |
| Kernel | Scheduler, batteries, processes, objects, handles, tokens, loaders, TEB/TLS, heat, files and record, stored in RAM |
| Clock | Accumulated virtual time, next interruption, counters, timer confirmation and interruptibility status |
| Protocols/devices | Sequences, reports, console, channel queue, RAM block and device failure status |
| Disk | Overlay's full and synchronized prefix, including its unchangeable base reference |

The capture occurs with vCPU stopped and without a device change running. In KVM, a pending I/O output is completed before the logger reading, without letting the guest execute new instructions. Host callbacks and hands are rebuilt; they are not serialized. Time paused is out of the virtual clock, and the saved TSC is reset before resuming execution.

The `.gvs` file has version, size limits, and checksum. Restore validates backend, CPU, configuration, image, and disk in a new candidate session. The previous session is only replaced after this preparation. Mixing RAM from a snapshot with another generation's disk is refused. Changing the configured RAM can make previous snapshots incompatible; refusal keeps the current VM.

## Operations and persistence

| Operation | Behavior |
| --- | --- |
| `create` | Validates and copies image/base to a new VM; refuses existing name or invalid image |
| `start` | New Boot with new session identity; `--paused` prepares VM without executing guest |
| `pause` / `resume` | Stops vCPU, saves a consistent checkpoint and resumes that state |
| `snapshot` | Saves a named point; if the VM was running, it resumes after capture |
| `restore` | Prepare saved point and leave the VM paused for inspection/resume |
| `restart` | Discard CPU/RAM from the previous boot and boot again, respecting disk policy |
| `shutdown` | For vCPU and syncs the disk; it is not a cooperative output request for each application |
| `clone` | Creates own identity and files; VM active/pausada is cloned by checkpoint, VM turned off by disk |
| `configure` | Change RAM and deadline of an off VM; invalidate your previous internal checkpoint |
| `delete` | Requires no active executable process; removes only the identified VM and its own copies |

In **persistent** mode, shutdown/restart saves the disk. If the executable process falls, the manager recovers the last full checkpoint and also saves the valid records from the latest disk. `resume` chooses the full checkpoint state, including the disk from that point; `restart` makes a new boot using the recovered persistent disk. This distinction prevents combining an old RAM with later writings. Incomplete tail records are recovered by the existing overlay format.

In **disposable** mode, each boot new part of the base; changes from the previous boot are not promoted to the persistent disk. Resume/restore a snapshot continues to preserve the full generation of that point. The base is never written.

File postings use temporary file, synchronization and atomic exchange. Creating, cloning and deleting directories uses durable intent: a aborted operation is collected without adopting an incomplete VM or reaching other directories. A lease distinguishes an active executable from a status left after a failure. Commands have confirmation and deadline; a still pending operation is indicated, not silently replaced by another request.

Reboot validation found and corrected an old failure of the first volume commit: partial data before the first valid superblock were rejected in the next boot. A durable marker in the reserved sector of offset 3072 now identifies incomplete boot. It only allows restarting the formatting when there is no image header yet; after commit is removed. Corrupted volumes without valid bookmark remain rejected. base/overlay format and existing volumes sequences remain compatible.

## Build and use

On Linux, with available native KVM and LLVM:

```bash
python3 tools/build_guest_linux.py --config Release --output build-phase9/guest-Release
GUARDIAN_LINUX_BUILD_DIR="$PWD/build-phase9/Release" ./Build-Linux.sh Release
```

Example of the Linux package extracted from your folder:

```bash
./guardian-vm --root "$PWD/vms" create demo kernels/guardian-kernel-complete.gk storage --ram-mib 32 --persistent
./guardian-vm --root "$PWD/vms" start demo --paused
./guardian-vm --root "$PWD/vms" snapshot demo inicial
./guardian-vm --root "$PWD/vms" resume demo
./guardian-vm --root "$PWD/vms" pause demo
./guardian-vm --root "$PWD/vms" clone demo copia
./guardian-vm --root "$PWD/vms" restore demo inicial
./guardian-vm --root "$PWD/vms" shutdown demo
```

Other commands: `status <id>`, `list`, `drop-snapshot <id> <nome>`, `configure <id> --ram-mib N --timeout-ms N` and `delete <id>`. The catalog must be new/empty or already belong to the manager. VM names and snapshots start with lowercase letter, have up to 32 characters and use letters, numbers, `_` and `-`; device names and paths are rejected.

In Windows, `Build-Kernel.ps1` includes `guardian-vm.exe` in `guardian_reference`. The executable is in `build-codex\Release\`; the images are in `build-codex\guest\Release\`. The same commands accept Windows paths and Unicode directory names. The full verifier uses a new tree and does not replace historical evidence of `build-codex`.

## Acceptance

```bash
python3 tools/validate_phase9.py
```

On native Windows, use `python tools\validate_phase9.py` with Visual Studio 2022 C++ x64, Windows SDK, CMake 4+, Python and WHP working.

The contract is `config/phase9-acceptance.json`. The matrix requires 23 CTests in each Linux configuration (Debug, Release and ASan/UBSan), or 43 CTests in each Windows configuration (Debug/Release), without failure or ignored tests. It also requires 153 + 59 native unwind comparisons in Windows. Complete regression uses 20 seconds per boot, including Debug images.

Criteria per application/scenario:

| Proof | Behavior required |
| --- | --- |
| Probe x64 itself | Retakes 16 GPRs, XMM0/XMM15, x87/MXCSR, FS/GS/kernel-GS, CPUID, flags, tables and permissions in another VM; delivers exactly one pending interruption |
| Full snapshot console | 16/32/64 MiB × three I/O points × two independent resumes and a new disk boot; repeats the application criteria of phase 8 |
| Snapshot/restoration | RAM, metadata, permissions and disk prefix match saved point; corrupted/incompatible files are refused |
| Manager | Two VMs run simultaneously; cloning and restoration of one do not alter neighbor disco/report |
| Disk | Persistent restart recovers sequence; two disposable bootes start from scratch; the imported base remains intact |
| Executor failed | Real interruption saves checkpoint and recent records; retrace checkpoint forehead on a new copy and boot of the recovered disk |
| Initialization do volume | 52 writing/flush prefixes; after failure there is empty state or complete file and log commit, never a mixture |
| Catalog | 64 aborted create/clone transactions, recovery of intentions, invalid names/links and preservation of external files |
| Limits | Four resident MVs; fifth refusal; resource changes and exclusion of active MV refused |
| Regression | 132 bootes of the 11 previous images and four fatal/IST kernel refusals on Linux |

The evidence is in `build-phase9/validation-<data>-<id>/validation.json`, with JUnit, logs, hashes, boot reports and confirmations of operations. The verifier only marks the full phase when it finds Linux and Windows acceptance for the same hashes of the sources. Result of one platform is not presented as validation of the other.

## Verified Linux Package

`dist/guardianav-phase9-linux-x86_64-20261018.tar.gz` includes `guardianav`, `guardian-kvm`, `guardian-vm`, signatures, 13 images, documentation and evidence. SHA-256: `a789fd35e145eecda30bfa3d5446aafb49cc062248611cf17cb52da9c5df491c`.

The 78 files were extracted and checked by hash. The manager worked from another folder, created a paused VM, saved a snapshot, cloned and executed the two copies to the expected result. The bases were intact, process features were zeroed out and the test catalog was deleted. The scanner CLI also worked outside the package folder. No call to Wine or QEMU. Evidence: `package-validation.json` in the final folder above. Previous packages and logs remain preserved.

## Limits

One vCPU per VM; 16–64 MiB of RAM; 16 registered VMs, four residents and 256 MiB of invited RAM per catalog; eight snapshots named by VM; 100–30,000 ms per execution stretch. Restore of the watch in this profile accepts less than one accumulated virtual hour. Snapshot space is limited by these numbers and the maximum size of the state; it is not a general quota of the host process heat. Overlay maintains the previous limit of 4,096 records.

CPU snapshots are backend-specific, CPU/configuration and supported revision. There is no KVM/WHP conversion or promise of arbitrary migration between machines. Import covers the `.gk` own kernels and the 64 KiB base; it is neither import of VHD/VMDK/ISO nor execution of arbitrary PEs in the host. KVM continues publishing `secondStageExecuteProtection: false`; it is the development backend, with NX in the guest tables. Windows product continues using the physical permissions of WHP.

Original preserved: `snapshots/codex-before-phase9-20261018/manifest.json`. Technical references: [conclusion of I/O and CPU status in KVM](https://docs.kernel.org/virt/kvm/api.html), [registrers WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetvirtualprocessorregisters) and [XSAVE in WHP](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetvirtualprocessorxsavestate).
