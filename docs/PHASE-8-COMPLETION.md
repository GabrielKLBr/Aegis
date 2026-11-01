# Phase 8 — Exceptions, dynamic DLLs and final acceptance

**Later continuation:** the phase 9 has been authorised by the user and is in validation on [PHASE-9.md](PHASE-9.md). Windows acceptance of this phase 8 The record below describes the delivery prior to this authorization.

Update: 18/10/2026. **Completed implementation for the console matrix itself; approved Linux acceptance; native acceptance on pending Windows.** Phase 8 is not yet closed on Windows product. Phase 9 has not been started.

## Results and evidence

Functional items missing were implemented: dispatch and continuation of x64 exceptions in user mode, non-local unwind with `__finally`, dynamic loading and unloading of DLLs, TLS by thread, rollback and failure tests at the beginning, during execution and at the end of processes. Previous consoles continue in regression. Windows/MSVC/MASM/WHP support remains in the tree; the execution of this delivery used ELF and KVM tools without Wine or QEMU.

Main evidence: `build-linux-kvm/validation-20261018-050639-287554/validation.json`. The `phase8-completion.json` file in the same folder explicitly records `linux_passed`, `awaiting_native_windows_acceptance` and `native_windows: not_run`. Manifestos include hashes from sources and artifacts. No test has been ignored.

| Verification executed | Result |
| --- | --- |
| Debug, Release and AddressSanitizer/UndefinedBehaviorSanitizer | 20 CTests in each configuration: 60 approved executions |
| 11 production images × Debug/Release × 16/32/64 MiB × two boot | 132 boots real approved |
| Fatal failure and double failure with STI in Debug/Release | Four executions correctly recusadas how success |
| Final console with exceptions and dynamic DLLs | 12 boots in the RAM/settings matrix, included in 132 |
| Non-local order/continuation/unwind engine | 415 verifications and 64 reading failure points per configuration |
| Loader PE | 1,651 verifications; 49 points of failure in dynamic load, in addition to 181 and 192 previous points |
| Search of handlers | 3.059 checks, 155 scenarios and 980 reading failures injected |
| Unwind portable anterior | 1,045 checks, 59 scenarios and 391 reading failures, plus seven-point control |
| Persistence | Second boot reuses the overlay, recovers the previous sequence and keeps the base intact |
| Windows/MSVC/WHP and comparisons with Microsoft APIs | Not executed in this session |

## Linux package delivered

`dist/guardianav-phase8-linux-x86_64-20261018.tar.gz` contains ELF executables, 13 kernel images, signatures, documentation and evidence. SHA-256: `4dfa540575a990d821d3a7360672b11ca7f815d88ca13852466916b39903c895`. The package was extracted in temporary folder; the 33 hashes were checked. Capabilities, scanner, hash and inspection worked from another folder. The `complete` image performed two additional bootes by the extracted package, recovering the sequence 0 → 3 → 6 on the same overlay, with intact base and zeroed living resources. Evidence: `package-validation.json` in the main validation folder above. Previous packages remain preserved.

## Behavior required by application

Success depends on the internal checks of each program and the kernel end counters. Returning without fail is not the only criterion. The `guardian-kernel-complete.gk` image incorporates the final console, the child, runtime and DLLs; the PE files themselves run within the guest.

| Application/component selected | Criteria exercitados and approved no Linux |
| --- | --- |
| Console start and DLL/TLS | Relocations, imports by name/ordinal, TLS, callbacks, DLL call, result and detach |
| Console of runtime | Files, log, console, heap, locks, permissions and recovery after restart; `PE RUNTIME OK` output |
| threads/lifetime Console, built into the final console | Batteries, private TEB/TLS, 31 previous proof workers, containment, children, explicit output and continuation without the main thread |
| `guardian-complete-console.exe` — exceptions | Real UD2 recovered with modified RIP/RAX, handle search, zero division, `__finally` execution, resume in `__except` and `RaiseException` with 7/11 arguments |
| `guardian-complete-child.exe` — faults | Seven modes confer exit codes for untreated exception, invalid context/MXCSR, nested exception, forbidden continuation, unwind destination forged and access to the kernel's private arena |
| `guardian-dynamic.dll` | Four cycles of load/discharge; exports by name/ordinal; references without reboot; TLS in preexisting thread and in new threads; 64 competing reference cycles by workers; unwind crossing DLL, `finally` and observable details |
| `guardian-failing.dll` | Eight refusals to boot with clean repetition; exception in callback of attach and in detach during output of the contained process; collected resources |
| Child with outstanding dynamic reference | Normal output drains references and runs detail; code 17 preserved |

The seven negative modes confer, respectively, `C000001D`, `C0000026`, `C000001D`, `C0000025`, `C0000026`, `C0000029` and `C0000005`. Callback failures close the affected process. The father checks the results and continues. In the final report, processes, objects, handles and live references are zero; there are no drivers, I/O requests, pins, volume handles or remaining watches. The free page difference remains the same as the previous fixed reserve (21).

Compulsory additional boot markers:

```text
PE x64 exception dispatch recovery nonlocal unwind PASS
PE dynamic DLL TLS references rollback unload PASS
```

## Implementation and limits

`shared/guardian/Exception.h` defines the x64 layouts of registration, context and dispatcher. `guest/ExceptionDispatch.cpp` looks for handlers and proves the frames and destination before starting unwind non-local. The RIP, RSP, flags, selectors and MXCSR valid restoration; the stack limits come from the PID/TID link in the kernel. Private state and original context are in the supervisor arena reserved in BootData (`0x110000` to `0x130000`). The private loader uses the next page. The matrix itself confirms that CPL3 cannot read this arena.

`guest/PeExceptions.cpp` publishes a limited copy on the thread stack and resumes the trampoline in CPL3. DLL Handlers and callbacks run in CPL3. The kernel checks thread, token and context in response. The restore preserves FS/GS and the state of segments under kernel control. There is no direct call to the application code in CPL0 or bytes patch of guest instructions.

Runtime offers the subset C SEH x64 version 1, `__C_specific_handler`, `RaiseException` and `RtlUnwindEx` required for the selected tests. There are 32 frames and 64 scope entries. `RtlUnwindEx` requires active dispatch, proven destination frame and continuation address within the target function. Nested exceptions, collided unwind, C++ EH, exit unwind for null frame and AVX/debug contexts are not part of the contract of this step.

`LoadLibraryA`, `FreeLibrary` and `GetProcAddress` use an explicit catalogue of four modules per process, without searching DLLs or host paths. There are up to 64 dynamic references per module, up to 128 KiB image and four TLS slots up to 256 bytes. Callbacks are serialized by the loader lock and attach/detach plans are updated to avoid downloaded module pointers. New maps, references, and TLS belong to the transaction; `DllMain` that refuses startup causes detach of the initialized prefix and rollback. Dependencies continue to be mapped as long as they are reachable by fixed modules or references. Failed to remove memory saves the property for a new cleaning attempt.

Data exports, forwarders, delay/bound imports, CRT and arbitrary DLLs are not declared compatible. Exceptions in startup/end callbacks are tested by the path that ends the process and collects its resources; recovering and continuing the same transaction after that exception is not supported.

KVM does not offer in this backend the independent physical execution permission per page used by the WHP: NX comes from the guest tables, and the report publishes `secondStageExecuteProtection: false`. Linux acceptance does not replace the protection/implementation of the Windows product. Details: [LINUX-KVM.md](LINUX-KVM.md).

## Repeat and close acceptance

On Linux, with available native KVM and LLVM:

```bash
python3 tools/validate_phase8_complete.py
```

On native Windows x64, with Visual Studio 2022 C++, Windows SDK, CMake 4+, Python and WHP running, run from project folder:

```powershell
python tools\validate_phase8_complete.py
```

The Windows command uses a new tree and requires **40 CTests in Debug and Release**, including the final console in WHP, dispatch tests and **153 unwind + 59 handler comparisons with `RtlVirtualUnwind`**. These results do not yet exist for this revision. Previous Windows history remains preserved. After this matrix passes, your manifests must be linked to this record before marking phase 8 closed. Phase 9 depends on new request. Contract: `config/phase8-completion-acceptance.json`.

Regression detected that old kernels received `InvalidHandle` in the consultation of pending DLLs during shutdown. The path now explicitly returns `Unsupported`, and runtime only treats the declared absence of this feature. Complete execution was repeated after correction. Previous attempt was preserved in `build-linux-kvm/validation-20261018-050301-157944/` with failure.

Original preserved and manifest: `snapshots/codex-before-phase8-completion-20261018/`. Previous milestone records remain valid for the revisions specified therein: [loader](PHASE-8.md), [runtime](PHASE-8-RUNTIME.md), [threads](PHASE-8-THREADS.md), [lifetime](PHASE-8-LIFETIME.md), [unwind](PHASE-8-UNWIND.md) and [preparation of handlers](PHASE-8-HANDLERS.md).

References to layouts and contracts consulted: [treatment of exceptions x64](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64), [CONTEXT x64](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-context), [RtlUnwindEx](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlunwindex) and [NTSTATUS](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55)].
