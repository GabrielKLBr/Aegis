# Phase 8 — PE Threads with private data and containment

Reference date: 17/10/2026. Phase State: ** in progress**. This third milestone continues the runtime of phase 8. Phase 9 was not initiated.

## Behavior implemented

The own DLL goes on to export `CreateThread`, `ResumeThread`, `GetExitCodeThread` and `SwitchToThread`. `WaitForSingleObject` and `CloseHandle` accept the handles of the new threads; `ReleaseMutex` continues to require mutex. Creation accepts immediate execution or `CREATE_SUSPENDED`. Identity, token, rights and process of origin are determined in the guest kernel.

Each worker receives a stack of 60 KiB committed and a first unmapped page, as well as a private page for TEB, TLS vector, two TLS blocks and startup. The PEB and heat are shared by the process. The initial thread continues using its private TEB on the bootstrap page. There are no host structures exposed to the application by these APIs.

Bootstrap calls TLS and DLL calls with `DLL_THREAD_ATTACH` before the thread and `DLL_THREAD_DETACH` function after their return. A process mutex serializes these calls and the process startup/completion; the lock is done by the scheduler. The callback code can yield execution without allowing another callback to enter at the same time. The first process attach uses the process itself without doubling the thread attach.

On return, the DWORD result signals the end object. In the kernel tabs, TEB, stack, process link, and scheduler slot are released. The end object survives as long as there are handles or waits: an old handle keeps its result after the slot receives another thread generation. Closing the last handle of a still-living thread does not eliminate its internal link. Query, wait, and modification permissions are independent.

Creation rollbacks if booking, commit or preparation of TLS fails. The four threads quota includes initial. Input addresses need to be in the RX sections of the loaded PEs; this restriction is narrower than the Windows API, which can accept an invalid entry and fail to execute later.

## Tests of behavior

The application itself creates three workers suspended per round, in ten rounds. Checks `STILL_ACTIVE`, timeout before resuming, previous suspension count, thread limit, result and wait repeated after termination. Keeps the handle of the first thread during all rounds to check the preservation of the old object. Handles closed and use thread as mutex are refused.

The 30 workers check for TEB, stack, identity, TLS and last independent error. They use the same heat and critical section, yielding execution both within the section and with battery blocks in use. There are 360 increments protected by boot, with preservation and zeroing of data after relocation. An additional thread comes out with an acquired mutex, exercising abandonment, waiting, acquisition and release by the other participant. The 31 pairs of thread callbacks need to be balanced before the program ends. The full TLS block of DLL in the initial thread needs to remain byte to byte equal throughout all rounds. DLL also gives up execution on its callbacks and concurrent reentry.

The native test maintains the previous 693 checks and adds **904** life cycle checks, including 64 reuses, pending wait, permissions, old result after reuse, refusal of reap of live thread, refusal of obsolete ID and closing of the handle before exit. The final count of pages, threads, handles, objects and references should return to the initial base.

A directed proof reproduced an improper writing of the old bootstrap FS/GS sentinels in the main thread TLS. The complete block test refused the previous version of the correction with status 51036, preserved in `build-codex/phase8-concurrency-tls-before-fix.json`. The path of creation now reserves these sentinels to the old workload. The final matrix requires the preservation of the entire block and zero return with correction.

The `PE private TEB TLS thread lifecycle contention PASS` marker depends on the zero return of the application and the complete cleaning provided by the kernel. The matrix uses only project reference PEs, in real CPL3 and WHP.

## Validation and preservation

The contract requires 35 tests per configuration in Debug and Release, without ignored, 720 positive boots (80 of this runtime and 640 regression), 240 external failure scenarios and persistent files/log pairs of the previous milestone. The 80 boot runtime add 2,480 workers and 28,800 protected increments.

The seven reference programs are executed by configuration, audits of the nine CLI images and tests. An independent tree, `build-phase8-concurrency-clean`, recompiles the nine production images and does its audits. The names of the production targets remain the same.

New evidence and disks use the `phase8-concurrency-` prefix. The snapshot of the original 17 is `snapshots/codex-before-phase8-concurrency-20261017`. SHA-256 checked the 457 historical evidence files and the disks of phase 7 and the two previous milestones of phase 8. No boot, security or Windows NX options are changed.

Contract: `config/phase8-concurrency-acceptance.json`. Checker: `tools/validate_phase8_concurrency.py`. Manifesto: `build-codex/phase8-concurrency-change-manifest.json`. The documents of earlier milestones retain the results and historical limits.

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild *> build-codex\phase8-concurrency-full-debug-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-concurrency-debug-tests-details.log
.\Build-Kernel.ps1 -Configuration Release -FullBuild *> build-codex\phase8-concurrency-full-release-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-concurrency-release-tests-details.log
cmake -S . -B build-phase8-concurrency-clean -G "Visual Studio 17 2022" -A x64 *> build-codex\phase8-concurrency-clean-configure.log
cmake --build build-phase8-concurrency-clean --config Debug --target guardian_guest --parallel 2 *> build-codex\phase8-concurrency-clean-debug-build.log
python tools\validate_phase8_concurrency.py --snapshot snapshots\codex-before-phase8-concurrency-20261017 --run-programs
```

## Limits and continuation

This milestone accepts four threads per process and a vCPU; it does not implement SMP. The reserve/commit stack is fixed; custom sizes, security attributes and other creation flags are rejected. The heat remains limited to 8 KiB and each module receives a maximum of 256 bytes of TLS. `SwitchToThread` informs if there was another participant ready and delivers the decision to the existing scheduler.

There is still a single PE process in the test. The creation of PE children and the full life of principal/last thread, including `ExitThread` explicit, remain pending. The workers of this milestone end up returning from the function; the main application waits for everyone before returning. The termination with still active workers needs the next life cycle matrix. There is no declared support for arbitrary code, user SEH handlers, unwind x64 in running or dynamic loading and unloading of DLLs. The final validation per application remains pending.

Contract references: [CreateThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread),
[GetExitCodeThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodethread),
[DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain) and
[SwitchToThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-switchtothread).
