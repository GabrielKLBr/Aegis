# Phase 4 — Threads and guest synchronization

Active base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. Image: `guardian-kernel-threads.gk`, version 0.4.0, x64, a vCPU, WHP. Status: completed in 16/10/2026. Phase 5 was not started.

## Implementation

`guest/Scheduler.cpp` adapts the `GuestScheduler.h` and `GuestSynchronization.h` rules for limited structures, without STL, host allocation or Windows APIs. The scheduler and objects run CPL0 in the guest. `ThreadKernel.cpp` connects this core to phase 3 memory and phase 2 interruptions and syscalls. The host injects the existing timer; it does not choose the next thread or emulates lock operations.

Each thread has its own context and stack of 64 KiB, with 60 KiB committed and a non-present bottom page. Incomplete creation unbooks, frames, thread input and completion object. The test includes lack of frames during this creation. IDs have generation, to refuse old references after recycling. Removing a completed thread releases its completion object; the batteries are recovered by the owner at the close.

### CPU Scanning and Status

- States: ready, running, waiting and finished; suspension has counter
  separate. A wait may conclude while the thread is suspended, but it will only run again after the resume.
- There are eight priorities. Among equal effective priorities, the order is FIFO with
  rotation. Ageing raises the effective priority of a ready thread to every eight logical clock advancements, limited to level seven. This gives an opportunity to run a low priority thread even with continuously ready competitors.
- The 5 ms timer causes CPL3 code preemption. A non-calculation thread
  need to call the kernel for another move forward. In the absence of ready threads, an idle context CPL3 awaits further interruptions; timeouts and timers can make them ready again.
- The kernel maintains masked interruptions while treating syscall or
  The switch happens on output, on the frame that will be restored by IRETQ. There are no preemptible kernel threads or SMP yet.
- The context contains the 15 general registers, RIP/RSP, RFLAGS, selectors,
  FS/GS and FXSAVE64 bases of 512 bytes: x87/MMX, XMM0–15 and MXCSR. The kernel floating point environment is initialized after saving the user's state. Return filters privileged flags and validates RIP and thread stack. The test also writes NT with POPFQ in CPL3. The SYSCALL input mask removes NT from the kernel's active state and the context selection clears the return copy, including after preemption, avoiding #GP in IRETQ. The intermediate attempt that revealed this case was preserved in `phase4-nt-first-failure-*`.
- OSXSAVE, AVX, PKU and FSGSBASE are not enabled.
  context to extended resources without corresponding save. The code checks this setting before starting threads.

### Waiting and objects

Manual events release all waiting; automatic events release one and consume the signal. Traffic lights validate initial count, limit and release; a complete wait consumes exactly one credit. Mutexes and critical sections have owner, recursion and release per owner. A thread that ends or suffers fault abandons the locks it has; the next acquisition receives the result of abandonment. For critical section, this result is an explicit project policy, without promise to reproduce indefinite Windows behavior.

SRW allows simultaneous readers or an exclusive writer, refusal to recur, implicit upgrade and release without property. Writers already waiting have preference over new readers. The output of a thread removes their property from reading/writing and allows others to advance. There is no fairness guarantee identical to Windows implementation.

Expects accept up to eight objects, with either one or all. Wait-any chooses the lowest available index. Wait-all validates all before consuming events, credits or ownership; duplicates are refused. Wait references keep the objects alive and prevent premature closing. Finished threads are awaitable for their completion objects.

The clock is monotonic. Backwards are refused. At the same time, waiting expiration is processed before the timer signal, setting a testable order for the race. Timeouts and cancellations do not consume features that arrived after. Timers may be manual or automatic, from an execution or journals; cancel future triggers without erasing a signal already delivered.

### Alerts and APCs

An alert waiting may return by alert or APC. A non-warning one remains waiting, preserving the pending alert/APC. Cancel a wait returns `Cancelled`; finishing a thread cancels its pending states and releases ownership. Alerts do not call user routines.

APCs have a limited queue and validated address on the user executable page. Upon delivery, the kernel retains the original context and enters the CPL3 dispatcher, which calls the routine with its argument. The return syscall restores the complete original context, including floating point. An active non-delivery return is refused. It waits alertly nested within an APC returns `NotSupported`; nested APCs are not announced in this ABI.

### Internal ABI and Host Border

The syscalls `0x20..0x2F` implements yeld, end, wait, unlock, signal, delay, APC return, multiple wait, suspension/resume, cancel, alert, APC queue, priority and creation. The `0x30` call is the controlled bootstrap of the first exchange. The interface uses registrers; the wait list is copied only after validating the entire interval in user memory. Kernel addresses, overflow, invalid flags, truncated IDs and incompatible entries do not receive temporary success. Creation returns the thread ID in RDX and the ID of its completion object in R8; the second can be used to await its completion. These internal IDs are not yet the handles and process rights provided in phase 5.

The phase 3 physical permissions device is reused, including for stack creation during execution. The threads report has its own `shared/guardian/Threads.h` contract: `0xEE` port, `0x10A000` GPA port, version, size, session and reserved fields validated. The host requires the timer unarmed and receives the report only once before the CPU report and the stop. No guest pointer is used as the host pointer.

## Validation

Criteria: `config/phase4-acceptance.json`.

The complete build and the 23 tests passed in Debug and Release, without ignored tests. The final matrix ran 80 boot of phase 4 and 240 regression of phase 1–3, in addition to the corresponding negative scenarios. The seven reference programs passed in each configuration. The Debug compilation in a new folder, without BUILD_TESTING, also passed and audited the four production images. The images have no Windows import, TLS, relocations or W+X sections.

| Configuration | RAM | Phase 4 Boots | Time by boot observed |
|---|---|---|---|
| Debug | 16 MiB | 20 | 195,613–208,190 ms |
| Debug | 64 MiB | 20 | 198,599–206,015 ms |
| Release | 16 MiB | 20 | 196,721–213,795 ms |
| Release | 64 MiB | 20 | 196,140–258,631 ms |

The times include preparation and self-testing on this machine. They do not represent real-time host guarantee. The final suite took 27.70 s in Debug and 23.80 s in Release. The thread kernel occupies 479232 bytes in Debug memory and 462848 in Release. Each configuration produced 52 executables and six kernel images, four of which were production and two negatives in Phase 2. The audit of the generated projects verified 20 target/configuration combinations, keeping the runtime checks of the host Debug separated from the freestanding guest.

`guest_scheduler_core` runs the same freestanding code used in the guest. It is also executed in CPL0 for each boot of the new image. Its checks cover rotation, priorities, aging, suspension, signaling, atomic consumption of wait-all, recursion, abandonment, SRW, timers, alerts, APCs, queues and limits. They include 128 signal interleavings, timeout and cancellation, 256 cycles of creation/end/removal and exhaustion of thread tables and objects. There are 983 execution checks.

`guest_threads` runs 20 boot with 16 MiB and 20 with 64 MiB per configuration. Each boot creates nine user threads in two rounds:

- Two workers increase counters without syscalls in the loop.
  General registers, DF, XMM0–15, x87 state, rounding controls and FS/GS during interruptions and exchanges.
- Two workers compete with mutex, critical section, exclusive SRW and traffic lights.
  Each section reads the counter, gives in to the CPU still holding the lock and then writes. The result must be eight increments preserved by type of lock, 32 in total. A lock that only returns success would lose updates in this test.
- A thread receives a user mode APC and then waits for a timer. Routine
  changes floating point state on purpose; the return must restore it.
- One thread suffers #UD holding a mutex; another must receive `Abandoned`.
  The controller also tests an invalid and wait-all pointer by syscall.
- In the second round, a thread raises a daughter suspended by syscall, exercises
  the suspension/resume counters and awaits a deadline. The timer must also interrupt the idle context and resume waiting. The mother awaits the object of completion of the daughter before starting her delay.

The result requires eight normal endings, one thread failure contained, one APC delivered, progress of the workers without syscalls, interruption during idleness and recovery of all stacks. 21 frames of active memory space inherited from the phase 3. The final mask is 32767. There's still 12 negative external scenarios by configuration, including invalid report, replay, IN/OUT wide, incorrect order and no timer limited by watchdog.

## Build and execute

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-threads.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-threads.gk --threads --ram-mib 64 --repeat 2 --report .\build-codex\phase4-cli-Release.json
```

`--threads` selects this contract, separated from `--memory` and CPU image. Previous images continue to be compiled and tested. WHP is used directly, without QEMU and without changing Windows host settings.

## Evidence and recovery

New reports exclusively use the `phase4-` prefix, including the regression ones. The 134 historical evidence files from the previous phases keep their hashes. The backup of the previous ten files changed is in `snapshots/codex-before-phase4-20261016-193923/`, with the historical hashes in `historical-evidence.json`. There was no removal of files from the project.

Evidence ed `build-codex/`:

- `phase4-full-debug-script.log`, `phase4-full-release-script.log` and o
  correspondent `phase4-*-tests-details.log`.
- `phase4-boots-Debug.json`, `phase4-boots-Release.json`, your `.negative.json`
  and `phase4-regression-phase1-*`, `phase4-regression-phase2-*` and
  `phase4-regression-phase3-*`.
- `phase4-reference-programs.json`, `phase4-smoke.json` and
  `phase4-cli-Debug.json` / `phase4-cli-Release.json`.
- `phase4-image-audit.json`, `phase4-build-boundaries.json`,
  `phase4-clean-configure.log` and `phase4-clean-debug-build.log`.
- `phase4-change-manifest.json`: hashes, new/changed fonts, backup and
  verified matrix. The verifier can be repeated without running programs: `python tools/validate_phase4.py --snapshot snapshots/codex-before-phase4-20261016-193923`.

The LNK4281 warning stems from the kernel fixed base in 2 MiB, as per the loader contract. Intermediate attempts logs remain identified, including corrected and tested NT failure in the final delivery.

## Limits and Stop

Up to 16 threads, 64 objects, eight objects per wait, four APCs pending by thread, eight priorities and maximum recursion of 127. Stacks are fixed size. Kernel operations are serialized in the single vCPU. There is no preemption within the kernel, SMP, kernel APC or AVX/XSAVE state support in this step.

This is an internal kernel ABI of its own. Rtl/Win32 exports, loader structures and general compatibility with Windows applications remain in the corresponding phases. MiniNt legacy continues as a cooperative host reference; its compatibility shortcuts are not used by the thread image. The locks and waits required for the program of this image have real implementation without no-ops. Processes, handles, rights and tokens are the subject of phase 5.

At the end of phase 4, stop. Phase 5 only begins after further request.

## References

- [Intel: system programming, interruptions and CPU](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) status
- [Intel: bases FS/GS and context exchange](https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/best-practices/guidance-enabling-fsgsbase.html)
- [Microsoft: waiting functions](https://learn.microsoft.com/en-us/windows/win32/sync/wait-functions)
- [Microsoft: waits and APCs](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/waits-and-apcs)
- [Microsoft: objects of timer](https://learn.microsoft.com/en-us/windows/win32/sync/waitable-timer-objects)
