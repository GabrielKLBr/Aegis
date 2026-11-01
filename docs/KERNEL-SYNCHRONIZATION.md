# MiniNt Mutexes and traffic lights — 16/10/2026

This step continues the existing core in `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`, using WHP directly. Objects and queues live in MiniNt; guest handles do not become Windows host synchronization handsets.

## Operations implemented

| Operation | Behavior |
|---|---|
| `NtCreateSemaphore` | Creates a nameless traffic light, with initial and maximum valid counts. |
| `NtReleaseSemaphore` | Add a positive amount, check the maximum before changing the state and can return the previous count. |
| `NtQuerySemaphore` | Returns current and maximum counts in eight bytes. |
| `NtCreateMutant` | Creates a nameless mutex, free or initially belonging to the current thread. |
| `NtReleaseMutant` | Only a thread owner may release; acquisitions recursivas require releases correspondent. |
| `NtQueryMutant` | Returns count, property by the caller thread and abandonment status, in eight bytes. |
| `NtWaitForSingleObject` | It also waits in traffic lights and mutexes, with poll, timeout relative/absolute or blocking according to the existing scheduler. |

Creations validate output, attributes, rights, and counts before publishing a handle. Names, directories of objects and security descriptors remain outside this subset. `NtOpenSemaphore` and `NtOpenMutant` return `STATUS_NOT_SUPPORTED`; do not manufacture handles for named objects.

## Counts and transfer

A traffic light has a countdown between zero and its positive maximum, limited to `INT32_MAX`. A satisfied wait consumes a unit. A release that would exceed the maximum returns `STATUS_SEMAPHORE_LIMIT_EXCEEDED` without changing the count. Null or negative release values are refused.

When there are waiters, the release reserves a unit for each wait completed before allowing the caller thread to continue. Therefore, it cannot consume with one poll the drive already destined to another thread. A suspended thread may have its wait completed and its drive booked; its suspension continues preventing execution. The traffic light has no owner and the termination of a thread does not automatically return units already consumed.

The mutex keeps owner ID and recursion depth in the host metadata. The checked count is `1 - profundidade`: one when free, zero in the first acquisition and negative values in the following. The recursion cannot make this count exceed the lower limit of a `LONG`. In this subset, the limit is communicated by `STATUS_MUTANT_LIMIT_EXCEEDED` without injecting an NT exception into the guest.

In the last release, the mutex is assigned to the next eligible thread before completing its wait. Another thread cannot acquire it between the signage and the return of the chosen owner. The model uses the FIFO waiting order for these two objects; this is a choice of this implementation, not a guarantee of order of Windows APIs.

The new scheduler paths expire the deadlines before choosing a waiter. An already expired wait returns `STATUS_TIMEOUT` and does not consume the release or receive ownership later. The policy remains cooperative, in a single vCPU, without preemption.

## Abandonment and useful life

When a thread ends, all the mutexes it has are abandoned, regardless of the recursion depth. This goes for the natural return of the routine and for `NtTerminateThread` over another thread, including a owner who received property while suspended.

The next acquirer receives `STATUS_ABANDONED_WAIT_0` (`0x80`) and becomes the owner. The abandonment status is consumed in this acquisition; a next recursive acquisition returns normal success. If there is no waiter, the object remains free and marked as abandoned until the next acquisition. The result informs the loss of the previous owner; it does not repair the data it protected.

A wait keeps reference to the object even if its handle is closed by another thread. Owned mutexes also maintain property reference after closing the last handle, until release or termination of the owner. This list has its own limit of 4096 objects to create, acquire and close handles repeatedly does not allow unlimited growth. The termination of the process clears these references without trying to execute waiters of an already closed process.

## Rights and ABI

The new types use the same handles table, duplication with reduced rights and closing flags of the previous steps. Waiting requires `SYNCHRONIZE`; query requires the `0x1` query bit; releasing traffic lights requires `0x2`. Freeing mutex requires a handle of the correct type and property by the caller thread, including through an alias without additional rights.

Previous counts and returned lengths occupy exactly four bytes. Buffers, stack parameters, and timeouts go through RAM limit checks, WHP permissions, and virtual regions. Invalid outputs are refused before consuming or releasing resources. The basic query rejects overlap between the data buffer and its returned length.

## Tests and compilation

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

The suite goes from eight to ten tests:

- `microvm_synchronization` checks limits, overflow, recursion, abandonment,
  transfer after expiration, suspension and rights masks. Time is provided to politics; the recursion limit is reduced in this test so that its edge can be exercised without billions of acquisitions.
- `microvm_synchronization_guest` runs x64 programs generated by the test in
  ring 3. Checks output counts and canaries, reduced handles, two waiters, reserve units for suspended thread, closing of handle during waiting, real timeout, recursive release, rejection of another owner, transfer and abandonment by natural and forced termination.

VM scenarios run sequentially, each with its WHP instance. The results slots start with sentries to detect non-executed continuations. A wait whose thread is completed before resuming should preserve your sentinel. No malware samples are used.

## Remaining limits

Multiple waits, alerts/APCs, object timers, namespace of objects with name, ACLs and additional processes remain pending. An undefined wait in traffic light yet without active scheduler follows the already documented refusal; when the scheduler is active, deadlock without deadline interrupts the VM with diagnosis. Snapshots still do not restore the full state of MiniNt.

This step does not transform MiniNt into a complete or bootable NT kernel without host. The context limitations, loader and isolation described in [KERNEL-THREADS.md](KERNEL-THREADS.md) remain applicable.

## Verified results

Validation completed in 16/10/2026, with actual execution of the WHP tests:

| Scan | Release | Debug |
|---|---:|---:|
| CTest tests approved | 10/10 | 10/10 |
| Tests ignored | 0 | 0 |
| Examples existing approved | 7/7 | 7/7 |

The complete Release compilation generated the 41 expected executables. The final Release and Debug builds did not issue notices or compiler errors. The verified examples were `vmntdll`, `vmntfile`, `vmntsect`, `vmntimage`, `vmntquery`, `vmntreg` and `vmwininit`.

Previous files are preserved in `snapshots/codex-before-sync-20261016-144033/`. Evidence remains in `build-codex/`:

- `sync-full-release-script.log` and `sync-debug-script.log`: builds and ten tests
  in each configuration.
- `sync-release-tests-details.log` and `sync-debug-tests-details.log`: output
  detailed testing, including new scenarios.
- `sync-regressions.json`: output and return code of the 14 examples.
- `sync-whp-availability.json`: hypervisor availability consultation.
- `sync-change-manifest.json`: hashes SHA-256 of the changed and new files,
  location of the previous copy and validation counts.

## References

- [ NT Declarations and structures in phnt](https://github.com/winsiderss/phnt/blob/master/ntexapi.h)
- [Lights — Microsoft](https://learn.microsoft.com/en-us/windows/win32/sync/semaphore-objects)
- [Mutexes, recursion and abandonment — Microsoft](https://learn.microsoft.com/en-us/windows/win32/sync/mutex-objects)
- [Sync object rights — Microsoft](https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-object-security-and-access-rights)
- [Wait and limit of mutex — Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject)
