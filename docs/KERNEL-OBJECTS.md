# Handles and articles of MiniNt — 16/10/2026

This step continues the basis on `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. The core continues to serve NT syscalls in the host and running the guest by the existing WHP without QEMU. The focus was to make the state of the objects and their rights verifiable before expanding compatibility.

**Next step:** [KERNEL-THREADS.md](KERNEL-THREADS.md) adds threads, cooperative scaling and blocking waits in events/threads. The results of six tests below record this step of objects; the current script performs eight tests. Simple wait limitations have been expanded according to the new documentation, while APCs, files and multiple waits remain pending.

## Corrected Problems

Files, sections, events, and keys used separate counters. Their intervals could overlap, so that the same number could identify different types. In addition, sections kept only the file handle number: closing the file before mapping the section could cause the mapping to lose its origin.

`GuestObjects.h` now provides a unique table by `MiniNt`, with type, rights and attributes unique to each handle. Objects use shared references; close a handle removes that entry, and the object remains alive as long as there is another reference. Guest handle values are never used as host Windows handles.

## Current behavior

| Resource | Implementation |
|---|---|
| Types | File, section, event and registration key in a common table. |
| Identification | Multiples of four, no collisions between types. Closures do not reuse numbers in this instance, avoiding reviving old handles. |
| Rights | Generic rights are converted to specific masks of the type; operations check the required rights. There is no silent concession of `MAXIMUM_ALLOWED`. |
| Duplication | `NtDuplicateObject` shares the same object, preserves or reduces rights and supports `DUPLICATE_SAME_ACCESS` and `DUPLICATE_SAME_ATTRIBUTES`. |
| Closure | `NtClose` refusal handles invalids, closing duplicate and handles protected against closing. |
| Information | `NtQueryObject` meets basic information of 56 bytes and 2 bytes handle flags; `NtSetInformationObject` changes inheritance and closing protection. |
| Limits | Up to 4,096 handles open per instance; exhaustion returns error without publishing a new handle. |

The duplication is restricted to the current process, with `HANDLE(-1)` in both process arguments. Raise the rights of the home handle is refused. `DUPLICATE_CLOSE_SOURCE` is refused at this stage, without effects on the source handle; its special semantics on error paths still need to be implemented.

Type checks distinguish non-existent handle from other type. Flags belong to the handle input: protecting a handle from closing does not change its aliases. Inheritance is stored and consulted; creating child processes and inheriting the table has not yet been implemented.

## Files, sections and views

`MiniNtObjects.cpp` meets object syscalls. Guest files continue inside `VirtualFs` in memory. The file object has an internal `VirtualFs` handle, automatically closed when its last reference is gone. Duplicated share the file position; independent openings have independent positions.

`NtReadFile` and `NtWriteFile` check rights, type, buffers and `IO_STATUS_BLOCK`. Reading informs EOF; a rejected writing does not change the data or advance the position. The implemented operations are synchronous and use the current position: explicit offsets, APCs, completion events and asynchronous operations are rejected. There is a limit of 16 MiB per call and 64 MiB for writing growth of a file in this path.

The section maintains a reference to the object of the file; each view maintains a reference to the section. Therefore, it is possible to close all the handles of the file, map the section, close all the handles of the section, and continue reading the view. Disampling the last view releases the last reference when there are no other users. The test also checks the effective closure of the internal handle of the `VirtualFs` using `OpenHandleCount()`.

Data mappings check the rights of the section handle. The maximum protection of the view considers both the section and the rights used to map it; a read-only handle cannot win writing by a call after `NtProtectVirtualMemory`.

The data model continues to copy the bytes for each view. Consistency between views, file persistence and copy-on-write are still pending. `SEC_IMAGE` images maintain the previous RWX entire image policy without individual permissions per PE section; the old PE parser also remains out of a full audit.

## Events and registration

An event aliases share its state. Synchronization events (`auto-reset`) consume the signal in a satisfied wait; notification events (`manual-reset`) remain signaled until reset. Signaling and reset require `EVENT_MODIFY_STATE`; waiting requires `SYNCHRONIZE`. The previous state output uses exactly four bytes. `NtClearEvent` follows its ABI from an argument; the second argument logger is not interpreted as pointer.

A zero timeout query on unmarked event returns `STATUS_TIMEOUT`. Expects that would require blocking thread return `STATUS_NOT_SUPPORTED`; no longer manufactured success. APC delivery and scheduler remain pending. Waiting on files is still an unsupported operation in this template.

Keys also use the single table. Refer values requires `KEY_QUERY_VALUE`, and change them requires `KEY_SET_VALUE`. Duplicate Handles preserve key identity. Reading names and buffers of this path are now checked against RAM and guest permissions. Previous virtual namespace is still permissive when opening keys without values; it does not represent all the semantics of Windows registry. `NtCreateKey` distinguishes a new key from an existing key on the `Disposition` output and requires `KEY_CREATE_SUB_KEY` to create relative to a parent key handle.

## Tests and compilation

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

The script continues using `build-codex` and now runs six tests. The four virtual heap/memory tests remain included. The two new are:

- `microvm_objects`: rights, duplication, flags per handle, faults per limit,
  closing and destruction in the last reference; creates 1,000 handles of alternating types to check collisions and invalidation of old handles.
- `microvm_objects_guest`: programs generated by the test itself run on
  ring 3 by the actual dispatcher of `MiniNt`. Verify aliases, rights, file position, events, closing protection, sections/views useful life, protection limits, registration and canaries of the output fields.

Guest tests share `MicroVmTestGuest.h`, extracted from the existing virtual memory test. They use watchdog, virtual RAM, and small locally generated programs. They return 77 if the WHP is not available, a situation that CTest records as ignored. They do not use malware samples.

## Validation executed

Windows x64 with available WHP, MSVC 19.44 and Windows SDK 10.0.26100.0:

| Scan | Debug | Release |
|---|---|---|
| Six CTest tests | 6 passed, no ignored | 6 passed, no ignored |
| `vmntdll` | It's over. | It's over. |
| `vmntfile` | It's over. | It's over. |
| `vmntsect` | It's over. | It's over. |
| `vmntimage` including running and relocating the test DLL | It's over. | It's over. |
| `vmntquery`, including event sinalizado | It's over. | It's over. |
| `vmntreg` | It's over. | It's over. |
| `vmwininit`, with own heap | It's over. | It's over. |
| Compilation of all project targets | Not executed | It's over., 37 executable |

The guest tests actually ran on the WHP. Compiling the targets of the Debug script ended without notice. The full Release build maintains notices of conversion into previous code from model A and other modules.

Evidence ed `build-codex`:

- `objects-release-tests.log`: the six tests in Release.
- `objects-debug-script.log`: compilation and six tests in Debug.
- `objects-regressions.json`: complete output of the seven examples in each configuration.
- `objects-full-release-build.log`: compilation complete Release.
- `objects-change-manifest.json`: changed files, hashes and backup location.

The nine existing files modified in this step were preserved before `snapshots/codex-before-objects-20261016-000830/` changes. Previous builds and snapshots remain available.

## Limits and continuation

The rights of this step are controls of each handle within the model. Tokens, ACLs, impersonation, privileges, named event/section objects and tables of other processes have not yet been implemented. Sharing files between openings (`ShareAccess` other than zero) is refused in this path. Directory openings, additional attributes and part of the native options also remain outside the supported subset.

`NtKernel` (model A) remains separate. Snapshot integration should still include object metadata, file position, regions, and WHP permissions; restoring only RAM and loggers does not restore the state of `MiniNt`. This step does not provide an independent bootable kernel or full Windows compatibility or validated malware isolation.

The next block is the staggering of threads and waits, replacing provisional responses with state transitions and actual completion events.

## ABI and semantics references

- [ZwClose and outstanding references](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwclose)
- [ZwDuplicateObject](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwduplicateobject)
- [NtQueryObject](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject)
- [ZwCreateSection](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatesection)
- [ZwCreateKey](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatekey)
- [Object Manager flags and structures statements in phnt](https://github.com/winsiderss/phnt/blob/master/ntobapi.h)
- [Event statements in phnt](https://github.com/winsiderss/phnt/blob/master/ntexapi.h)
