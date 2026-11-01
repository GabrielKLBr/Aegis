# MiniNt Threads and Waits — 16/10/2026

This step continues the existing implementation in `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`, with direct WHP, without QEMU. The core is still implemented in the host: it does not boot itself into hardware.

The next step, [](KERNEL-SYNCHRONIZATION.md) synchronization, adds mutexes and traffic lights to the simple waits. The results below preserve the original validation of this step; the current suite contains ten tests.

## Schedule implemented

The existing monitor maintains a single vCPU. `GuestScheduler.h` implements a cooperative queue: a thread executes until waiting, yielding execution, suspending or finishing. Another ready thread takes over vCPU and, when the previous one reruns, resumes the instruction following the syscall that suspended it.

The states are ready, running, waiting, and finishing. The suspension count is independent of the standby state. The queue uses the input order, not the numerical order of the IDs. The policy receives time as an argument, which allows testing races between signaling and timeout without using pauses in the drive tests. The limit is 64 threads records per instance, including the initial thread; completed records without handles or references are collected before a new creation.

`MicroVmMonitor` saves and restores general registers, segments, control loggers and debug, XMM, x87/MMX data and x87/MXCSR. controls The syscall path recognizes when the scheduler has restored another RIP and avoids advancing it again. The entry of a new thread starts with general registers and clean floating point, RCX argument and stack as the ABI x64.

This version requires existing user mode, with disabled interruptions and OSXSAVE disabled. AVX/XSAVE state is not silently discarded: a configuration that would require it is refused in activation. There is a single vCPU; there is no simultaneous execution of two threads or timer preemption.

## Syscalls and waits

| Operation | Behavior |
|---|---|
| `NtCreateThreadEx` | Creates a thread of the current process, ready or initially suspended, with typed handle and specific rights. |
| `NtSuspendThread` / `NtResumeThread` | They hold a suspension count, validate rights and write the previous count in exactly four bytes. |
| `NtYieldExecution` | Give vCPU another thread ready; inform `STATUS_NO_YIELD_PERFORMED` if there is no other thread. |
| `NtWaitForSingleObject` on event | Blocks the thread active until signaling or timeout, without preventing another thread from running. |
| `NtWaitForSingleObject` on thread | Retakes when the target thread ends; a thread already completed satisfies the wait immediately. |
| `NtDelayExecution` | Suspends until the deadline, without manufacturing early success; zero interval can yield execution. |
| `NtTerminateThread` | Ends the current thread or other authorized thread, releases its own resources and wakes up those waiting for it. |
| `NtQueryInformationThread` | Meets `ThreadBasicInformation` x64 of 48 bytes: output status, TEB, IDs, affinity and basic model priorities. |

Auto-reset events wake up a single waiter in the waiting order; manual-reset events all wake up. Timeout check occurs before signaling, so that a later signal does not transform an already expired wait into success. The suspension does not miss the conclusion: a wait can be satisfied while the thread remains prevented from running by its suspension count.

Negative relative timeouts and positive absolutes follow the NT representation in units of 100 ns. Internal timestamps use monotonic clock; absolute values are converted from the host's FILETIME UTC at the time of call. When no one is ready, the host waits at intervals of approximately 1 ms and checks deadlines and cancellation. There is still no single clock synchronizing these values with all synthetic fields of `KUSER_SHARED_DATA`.

If all threads are prevented from running and there is no pending deadline, the monitor closes execution with deadlock diagnosis. It does not return success to the waiter nor goes into endless wait in the host. Canceling during a wait without running vCPU is also recognized. After such a failure, the VM must be recreated; resuming contexts with uncertain status is refused.

## Batteries, TEBs and service life

The first creation activates the scheduler and records the thread that already executed. A wait with time or a delay can also activate it. An indefinite wait without active scheduler continues to return `STATUS_NOT_SUPPORTED`, as there is no other thread or external source of completion in that path.

Each new thread receives a 64 KiB stack reserve, with the bottom page inaccessible, and a minimum TEB of 8 KiB. The TEB contains self, stack limits, client ID, reference to the existing PEB and dislocation base. IDs stay in the host metadata; guest writing on your TEB does not change the identity maintained by the scheduler.

A process stub RX receives the return of the routine and calls `NtTerminateThread(NtCurrentThread(), status)`. The end releases the stack and the TEB itself even while a thread handle remains open for query. The initial TEB/stack, borrowed from the existing environment, are not released as if they belonged to the new allocater. The stub RX remains during the life of the VM.

Common memory syscalls refuse to release or change the regions that the core holds for batteries, TEBs and the return stub. Cleaning uses the internal path of the memory manager. This avoids later releasing an address already reused by another allocation. The guest continues to be able to read and write the RW parts of the memory itself, as per the WHP permissions.

Threads integrate the handles table of the previous step. Consulting requires `THREAD_QUERY_INFORMATION`, suspension/resume requires `THREAD_SUSPEND_RESUME`, finishing requires `THREAD_TERMINATE`, and waiting requires `SYNCHRONIZE`. pseudo-handle `NtCurrentThread()` is accepted in thread-specific syscalls; duplication of this pseudo-handle has not yet been implemented.

## Sub-assembly and limits

`NtCreateThreadEx` accepts the current process and flags zero or `CREATE_SUSPENDED`. `ZeroBits`, custom stack sizes, attribute list and other flags are refused. The input routine is called directly by the own environment: does not run `RtlUserThreadStart`, loader TLS startups or DLL callbacks. Minimal TEB does not constitute the entire startup of a Windows thread.

There is only simple waiting in events and threads. Alerts/APCs, multiple wait, mutexes, traffic lights, object timings, waits in files and asynchronous completion of I/O remain pending. Old thread-alert paths and some loader locks still have temporary answers. There are no dynamic priorities or preemption: a loop that does not syscalls remains subject to the existing watchdog.

There is no additional process nor SMP. `SnapshotManager` still needs to capture the contexts, queues, references and deadlines with RAM and permissions; restoring only RAM/registers does not restore `MiniNt`. Model A (`NtKernel`) remains separate. Full Windows compatibility and validated malware isolation remain out of what the tests of this step demonstrate.

## Compilation and testing

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

Builds are in `build-codex`. The script now runs eight tests: the previous six and the following:

- `microvm_scheduler`: queue, suspension, auto/manual-reset, deadlines, races between
  timeout and signal, delays, termination and capacity, with simulated time.
- `microvm_threads_guest`: actual execution in ring 3. Check handoff per event,
  join, standby creation, counts, rights, return of routine, thread queries, protection and cleaning of internal regions. Stack markers, GPR, XMM, value and control x87, MXCSR and GS/TEB are checked after real exchanges. Additional scenarios check relative timeout, absolute timeout, delay, end of other thread, deadlock and cancellation during idle.

The programs are generated in the test itself; external samples are not loaded. The WHP tests return 77 when the hypervisor is not available, in which case CTest records the execution as ignored.

The ten previous files modified in this step are preserved in `snapshots/codex-before-threads-20261016-003531/`.

## Verified results

Validation completed in 16/10/2026, with the hypervisor WHP present:

| Scan | Release | Debug |
|---|---:|---:|
| CTest tests approved | 8/8 | 8/8 |
| Tests ignored | 0 | 0 |
| Examples existing approved | 7/7 | 7/7 |

The verified examples were `vmntdll`, `vmntfile`, `vmntsect`, `vmntimage`, `vmntquery`, `vmntreg` and `vmwininit`. The complete Build in Release also ended successfully, with 39 executables. Debug compilation of this step did not issue notices or compiler errors.

Evidence maintained in `build-codex/`:

- `threads-release-tests.log`: result of the eight tests in Release.
- `threads-debug-script.log` and `threads-debug-tests-details.log`: compilation,
  result and detailed output of the tests in Debug.
- `threads-regressions.json`: captured output and output codes of the 14 examples.
- `threads-full-release-build.log`: Full Release compilation.
- `threads-whp-availability.json`: hypervisor availability consultation.
- `threads-change-manifest.json`: changes/created files, hashes SHA-256,
  location of the previous copy and validation counts.

## ABI References

- [Declarations of creation, termination and thread information in phnt](https://github.com/winsiderss/phnt/blob/master/ntpsapi.h)
- [NtWaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntwaitforsingleobject)
- [WHvGetVirtualProcessorRegisters](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetvirtualprocessorregisters)
