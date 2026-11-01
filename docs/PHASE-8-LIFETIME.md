# Phase 8 — Child and life cycle processes

Reference date: 17/10/2026. Status: **phase 8 in progress**. This is the fourth milestone; phase 9 was not initiated.

## Result

The guest runs a PE console itself that creates other PE processes, expects it to end and query the process codes and initial thread. The own DLL exports `CreateProcessA`, `GetExitCodeProcess`, `GetCommandLineA`, `ExitThread` and `ExitProcess`. The wait and closing APIs also accept process handles. The previous image of the runtime remains in the regression.

Each process has its own loader, address space, modules, relocations, TLS, PEB and startup lock indexes. The catalog is unchangeable in the kernel and contains only executables and project reference DLL. The child uses the same identity and session, with reduced integrity to 1 and no additional privileges. It does not inherit hands. This restriction is a guest policy, narrower than the common Windows creation.

Startup prepares the child fully before publishing his handles. Accepts immediate or suspended execution, with restarted by initial thread. Returned handsels belong to the father; the child cannot use them as his own. Query, wait and change continue subject to type, rights and identity. End objects survive as long as there are references, even after releasing the address space and reusing Case/thread. slots

The files/log service now checks the cleaning by PID. Finishing a child does not require the global handles table to be empty and does not close the father's files or keys. The test leaves the father's handles open during all of the children's endings to exercise this isolation.

## Closure

`ExitThread` redirects the execution to an output bootstrap in the guest. It abandons the user's call stack, executes thread output callbacks under the boot lock and signals its result. Stack, link and slot are released after returning to the kernel page table.

The initial thread can come out and leave another thread active. Heat, PEB, modules and process services remain available. Your bootstrap homepage, which also contains the PEB in this limited layout, is maintained until the process is over; the stack and thread slot are released. The end of the last thread executes the process completion and uses its code as a result of the process. The result of the initial thread already closed remains preserved in the old handle.

`ExitProcess` and the return of the input point use the process termination path. Bootstrap acquires the boot lock, closes the other threads without sending `DLL_THREAD_DETACH` to them, signals their results and calls the process callbacks with the end indicator. Finally, closes services, downloads the modules, closes handles and releases the process. It does not end children as a result of closing only the parent process.

In the reference DLL, the definitive end allows pending heap allocations as the entire process space will be destroyed. The previous workload keeps the strict verification of empty heap and balanced callback pairs. There is no execution of automatic C++ destructors in `ExitThread`, nor user unwind/SEH.

## Startup proofs

The new console first performs all previous runtime proof: files, persistent registry, heap, 31 workers, containment, private TLS and preservation of the full TLS block of DLL in the initial thread. Then performs 23 children:

| Case | Quantity | Process result / initial thread |
| --- | ---: | --- |
| Return normal | 4 | 11 / 11 |
| `ExitThread` ed single thread | 4 | 33 / 33 |
| Worker calls `ExitThread(44)` and the main returns | 4 | 45 / 45 |
| Main comes out with 55 and the worker continues in the heat | 4 | 66 / 55 |
| `ExitProcess(22)` with worker suspended and heap pending | 4 | 22 / 22 |
| Two children suspended simultaneously, taken up later | 2 | 11 / 11 |
| Creation without suspension | 1 | 11 / 11 |

The console verifies `STILL_ACTIVE`, timeout before resuming, types of handles, after waiting results, refusal of closed handle and preservation of old results during reuse. Two children coexist with the father; the attempt to create one more respect the quota. Heap buffers and parent TLS must remain intact, while the child's heap and TLS begin with their own values. The case of the main thread also closes the working handle still alive and depends on the abandonment of the mutex for the worker to continue. Children's callsbacks check the expected notifications, including output from the initial thread without thread.

Inherited requests, unsupported startup options, missing catalog name, invalid internal arguments and command line without terminator are rejected.

Before entering CPL3, each boot injects ten budget failures into creation: six handle limits and four objects. This reaches process preparation, thread, loader lock, and handles publication into the parent. In each failure, output handles are zeroed out, and pages, processes, threads, objects, handles, and references return to the previous count. An additional control prepares a suspended child and cleans it without performing its entry. The next creation must continue to work.

The `PE child creation rollback 10 fault budgets PASS` marker depends on this conference. `PE child processes explicit exits last-thread lifetime PASS` is only issued after the console's zero return and the complete cleaning of all PEs.

## Validation and preservation

The contract requires 36 tests per configuration in Debug and Release, without ignoring. The matrix adds 800 boot: 80 of the new life cycle and 720 of regression. The 80 new boot performs 1,840 children, 800 budget failures and 80 cleaning controls before entry. The 240 external scenarios of previous failure and runtime persistence pairs remain.

The native test preserves 904 life cycle checks and 693 processes, adding 17 on thread opening/permissions, obsolete ID, handle allocation failure and preserved result. The service maintains its 185 previous checks and adds ten on PID cleaning, fails with a new attempt and preservation of the handles of the other process.

The seven reference programs are executed by configuration, audits of the ten production images and CLI tests, including two persistent bootes of the new workload per configuration. An independent tree, `build-phase8-lifetime-clean`, recompiles and audits the ten images. The complete build contains 63 executables of tools/tests, 12 kernel images (ten production and two failure) and six reference PEs in each configuration.

The snapshot is `snapshots/codex-before-phase8-lifetime-20261017`, with 20 originals. New logs and disks use `phase8-lifetime-`. The 520 historical files of reports/logs and disks of the previous four milestones are checked by SHA-256. Boot, security and NX from Windows are not changed.

Contract: `config/phase8-lifetime-acceptance.json`.
Checker: `tools/validate_phase8_lifetime.py`.
Manifesto: `build-codex/phase8-lifetime-change-manifest.json`.

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild *> build-codex\phase8-lifetime-full-debug-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-lifetime-debug-tests-details.log
.\Build-Kernel.ps1 -Configuration Release -FullBuild *> build-codex\phase8-lifetime-full-release-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-lifetime-release-tests-details.log
cmake -S . -B build-phase8-lifetime-clean -G "Visual Studio 17 2022" -A x64 *> build-codex\phase8-lifetime-clean-configure.log
cmake --build build-phase8-lifetime-clean --config Debug --target guardian_guest --parallel 2 *> build-codex\phase8-lifetime-clean-debug-build.log
python tools\validate_phase8_lifetime.py --snapshot snapshots\codex-before-phase8-lifetime-20261017 --run-programs
```

## Limits and work remaining

There are up to three processes, four threads per process and a vCPU. Stack sizes, heap, I/O and strings keep the previous limits. `CreateProcessA` only accepts the name of the child in the catalog, ASCII command line up to 127 bytes, 104 byte startup with only `cb` set and flags 0 or `CREATE_SUSPENDED`. There is no search for PATH, external binary execution, handles inheritance, environment/directory itself, console redirection, advanced options or declared Unicode support.

There is no declared support for recursive output during DLL callbacks, complete cancellation of I/O by thread or arbitrary code. End validation with callback crashes, threads in all states and other boot errors continues in the expanded matrix. Missing exception and unwind x64 user, dynamic loading and download of DLLs and the final application audit to complete phase 8. Phase 9 can only start after this completion and the requested stop.

Reference contracts: [CreateProcessA](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa), [ExitThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread), [ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess) and [GetExitCodeProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess)].
