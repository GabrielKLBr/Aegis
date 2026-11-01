# Phase 11 Windows Corrections — 19/10/2026

**The eight failures have been corrected. The full suite approved 52/52 tests on Debug and 52/52 in Release, without ignoring, on Windows 11 x64/MSVC/WHP real.**

| Configuration | Before | After | Current failures | Final suite duration |
| --- | ---: | ---: | ---: | ---: |
| Debug | 44/52 | 52/52 | 0 | 446,15 s |
| Release | 44/52 | 52/52 | 0 | 297,43 s |

The official verifier returned `passed` / `awaiting_linux_acceptance`. The corrected revision is accepted on Windows; its Linux revalidation is still pending because source hashes have changed. Phase 12 has not started.

## Fixes

- **Network and graphics:**I/O commands now only use the transferred bits
  by instruction: 8, 16 or 32. RAX high bits saw no additional commands.
- **RAM of various VMs:** each VM has its own auxiliary process that maintains
  your memory. The monitor accesses a shared view and uses `WHvMapGpaRange2` with the correct process. The boot header is outside the RAM visible to the guest. Handles inherited by the helper are limited to the objects of this startup; the process ends with its owner.
- **Pending interruption in snapshot:**An external interruption with IF off
  or under the shadow of STI stays in the monitor queue. Snapshot keeps this state; delivery happens when CPU signals that you can receive it.
- **CPU reset:**Hyper-V internal activity summary is only
  reading. The loader validates this field and restores the recordable registers without trying to write the activity summary.
- **Publication of files:** transitional share/access conflicts
  receive limited attempts. The previous generation remains intact if the replacement fails, and the temporary file is collected.

They re-passed, in both configurations: `guest_session_snapshot`,
`vm_catalog`, `vm_manager`, `guest_network_boot`, `vm_network`,
`guest_graphics_boot`, `graphics_application` and `vm_graphics`.

## Checks

The criteria and deadlines of the 52 tests were preserved. The final compilation started from a new tree; Release was prepared while Debug was running, and the suites were sequential. No fonts changed during acceptance.

In addition to the previous matrix, I/O tests were added with non-zero high bits, RAM and independent permissions between VMs, unrestoreable CPU activity rejection, and permanently locked file replacement. Tests demonstrate preservation of old generation and recovery after release of the lock. Snapshot approves 234 configuration checks. Microsoft comparisons of 153 handlers and 59 unwind frames remained approved by configuration, failure/recovery tests, network and independent graphics oracles.

The network test performs four actual simultaneous guests; the manager exercises two VMs, worker drop and recovery. Memory auxiliaries were closed at the end. Three malformed auxiliary releases were also rejected with the expected codes.

## Requirements and limits

Windows programs that use the monitor need `guardian-whp-memory.exe` in the same folder. CMake already builds that dependency. The memory API used requires Windows 10 20H2 or more recent and WHP; the running of this round was on Windows 11, build 26300.

Accepted Windows checkpoints stop with executable CPU at supported profile borders. Snapshots with startup, Halt or idle suspension are explicitly refused; these states need an additional contract. The application boundaries, GUI by software, UDP and a vCPU by MV continue to be described in [PHASE-11.md](PHASE-11.md).

References to the contracts: [WHvMapGpaRange2](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange2), [CPU](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvsetvirtualprocessorregisters) registers and [read-only activity summary](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/datatypes/hv_register_name).

## Evidence and recovery

- [Acceptance official](../build-phase11/validation-20261019-122920-145a4969/validation.json).
- [Manifesto ed corrections](../build-phase11/validation-20261019-122920-145a4969/windows-fixes-summary.json).
- [Patch revisionable](../build-phase11/validation-20261019-122920-145a4969/changes.patch).
- [JUnit Debug](../build-phase11/validation-20261019-122920-145a4969/host/tests-Debug.xml) and [JUnit Release](../build-phase11/validation-20261019-122920-145a4969/host/tests-Release.xml).
- [Original diagnosis of the eight flaws](WINDOWS-PHASE11-VALIDATION-20261019.md), preserved.

Originals are in `snapshots/codex-before-windows-phase11-fixes-20261019/`. Hashes have been checked for 31 reports, logs and previous packages without changes. Execution can be repeated with `python tools/validate_phase11.py`. No boot, NX or host security settings have been changed.
