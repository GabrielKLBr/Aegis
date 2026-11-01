# Phase 8 — console runtime, files, record, heap and locks

Active base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. State in 17/10/2026: second milestone implemented and validated; **phase 8 in progress**. Phase 9 remains closed. The first milestone is in [PHASE-8.md](PHASE-8.md).

## Behavior implemented

The new `guardian-runtime-console.exe` runs on CPL3, using its own DLL, `guardian-runtime-api.dll`. They are compiled PEs, no CRT or Windows DLLs. The loader, TLS, PEB/TEB initial and protections continue running on the guest. The loader also validates unwind metadata chained by MSVC in Release: references should point to previously verified entries with compatible frame. Cycles, future references and truncated trailers are refused. This does not yet execute the unwind of an exception. The program imports functions with Win32 subset signatures and behavior described below. The DLL converts these calls to a kernel-enabled ABI; it does not send calls to Windows host.

`guest/PeServices.cpp` keeps the file and key handles in the kernel, with process identity, type, granted rights and file position. One process cannot use another's handles, transform key into file, write by a read-only handle, or reuse a closed handle. File and log operations use the volume and permissions of phase 7. The paths refer only to the virtual disk and guest log.

The console uses the existing I/O queue, driver, and protocol. The kernel opens the device with the right to write and keeps its reference until you complete the request. The output observed by the host is `PE RUNTIME OK` followed by `IO OK`, both with new line. Bytes outside the permitted ASCII subset are refused before reaching the device protocol.

The user heap uses the same kernel algorithm, extracted for `Heap.h`: 16-byte alignment, block division and coalescence, zeroing and realloc that keeps the old allocation if there is no space. A real kernel mutex serializes this heap. Mutexes and critical sections use the scheduler and table of existing objects; they are not implemented as empty functions.

`GetLastError` and `SetLastError` use the TEB thread field. Log operations return their code directly, preserving this field. File operations convert volume errors to Win32 subset.

## APIs present in own DLL

| Group | Functions |
|---|---|
| Identity and error | `GetCurrentProcessId`, `GetCurrentThreadId`, `GetLastError`, `SetLastError` |
| Memory | `GetProcessHeap`, `HeapAlloc`, `HeapReAlloc`, `HeapFree` |
| Console | `GetStdHandle`, `WriteFile` for Standard exit/error |
| Files | `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointerEx`, `GetFileSizeEx`, `FlushFileBuffers`, `CloseHandle` |
| Registration | `RegCreateKeyExA`, `RegOpenKeyExA`, `RegSetValueExA`, `RegQueryValueExA`, `RegCloseKey` |
| Mutex | `CreateMutexA`, `WaitForSingleObject`, `ReleaseMutex`, `CloseHandle` |
| Section critical | `InitializeCriticalSectionEx`, `EnterCriticalSection`, `TryEnterCriticalSection`, `LeaveCriticalSection`, `DeleteCriticalSection` |

The volume also received a sized query operation per handle, without changing the size of the previous structures or reporting contracts.

## Criteria verified within the guest

The valid application TLS and identity, error preservation, alignment and zeroing of the heap, growth with data preservation, memory loss, double release, mutex recursion and critical section, release without property and use of closed mutex.

In file, check creation/opening, read, write, size, positions related to the beginning and the end, EOF, attempt of negative position, sharing conflicts, unique creation of existing file, use after closing and flush. In the record, check creation/opening, DWORD, query of size, insufficient buffer without overwriting and refusal of writing by read-only key. Types of record without implemented conversion return explicit error.

The program maintains a counter in `C:\runtime.bin` and `HKCU\Runtime\Counter`. On the second startup, it checks that the two survived and have the same value before increasing them. Closes the commonly used handles and leaves two more open on purpose for the cleaning test at the end. The kernel checks the absence of handles and the return of all the proof pages. Unmounting the DLL also requires the heat to be empty.

Requests with kernel address or invalid version are rejected without causing kernel default. The `PE runtime services heap locks PASS` message is only issued after the program has finished with the expected result and cleaning is verified.

## Validation and preservation

The contract requires 35 tests per configuration in Debug and Release, without ignored. The matrix adds 720 boot: 80 of the new runtime, forming 40 persistent pairs, and 640 of regression, including the first PE console. The 240 external failure scenarios of the previous phases remain.

The native test of the new service/heap covers 185 conditions, including isolation of handles, rights, EOF, quotas before side effects, backend failure without advance of position and closing failure with new cleaning attempt. Previous tests maintain loader checks, volume, persistence, I/O, processes, scheduler and memory. There is still a native proof of unwind chain in multiple levels, six invalid variations and failure injected in each backend operation of this load.

A new tree, `build-phase8-runtime-clean`, compiles the nine production images and performs its audits. The seven reference examples are executed by configuration, along with CLI audits and tests. The complete build contains 63 executables of tools/tests, 11 kernel images (nine production and two CPU failure) and four PEs of console/DLL itself by configuration.

New evidence files and disks use `phase8-runtime-` prefix. The 401 historical files registered in the snapshot, phase 7 disks and phase 8 first milestone disks remain checked by SHA-256. The snapshot of the originals is `snapshots\codex-before-phase8-runtime-20261017-010738`.

The verifier and the contract of this milestone are `tools/validate_phase8_runtime.py` and `config/phase8-runtime-acceptance.json`. The result is `build-codex/phase8-runtime-change-manifest.json`, with state of the `in_progress` and `next_phase_started: false` phase.

To repeat the current validation, saving logs by configuration:

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild *> build-codex\phase8-runtime-full-debug-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-runtime-debug-tests-details.log
.\Build-Kernel.ps1 -Configuration Release -FullBuild *> build-codex\phase8-runtime-full-release-script.log
Copy-Item build-codex\Testing\Temporary\LastTest.log build-codex\phase8-runtime-release-tests-details.log
python tools\validate_phase8_runtime.py --snapshot "snapshots\codex-before-phase8-runtime-20261017-010738" --run-programs
```

## Current limits and continuation of phase 8

This matrix uses a thread by PE process. There is still a lack of child rearing and threads by these APIs, TEB/TLS private by thread and runtime containment tests with multiple threads. The scheduler has its independent proofs, but this still does not validate the combination with this runtime PE.

The user heat has 8 KiB. There are up to 16 service handles, eight per process, beyond the previous limits of the table of objects. Files and data operations maintain the limit of 512 bytes of the volume. Names/paths are ASCII and functions are synchronous, without overlapped I/O. The record supports NONE, BINARY, DWORD and QWORD; the conversion ANSI/Unicode of strings remains pending.

There is output and default error; there is no console readout. Mutexes of this subset are anonymous and start without owner. Critical section startup uses the `Ex` function that can return failure; SEH failure processing does not yet exist. The limit of 127 recursive acquisitions of the existing scheduler applies. APIs refuse flags and options without implementation, including security attributes, file templates, registration options, and mutex with name.

Dispatch of user exceptions and unwind x64, dynamic loading and unloading of DLLs, the startup/end failure matrix and the final audit per application remain pending. This milestone does not declare general compatibility with Windows programs. Phase 9 can only start after completing phase 8 and respect the stop requested by the user.

Contract references: [CreateFileA](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea), [GetStdHandle](https://learn.microsoft.com/en-us/windows/console/getstdhandle) and [RegSetValueExA](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexa). Unwind metadata: [Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64)'s x64 contract.
