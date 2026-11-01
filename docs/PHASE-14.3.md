# Phase 14.3 — NTDLL and native MiniNT services

Windows x64 delivery of 24/10/2026. This subphase adds a limited array of native calls to the kernel itself of 14.2. The loader and CPL3 execution continue within the WHP guest. Delivery does not declare general compatibility with Windows applications, Flappy or with full startup of NTDLL. 14.4 requires another request.

## Call Path

`guardian-kernel-native-nt.gk` Reuse memory, processes, objects, scheduler, TLS, PE charger and treatment of existing exceptions. Native services operate on this real state: reserve/compromise/protect memory, share pages, lock/wake up threads and maintain handles references produce effects on the guest. There is no referral of syscalls to Windows host.

NTDLL itself exports 48 Nt entries and its 48 Zw aliases. It uses a 104 byte version package, accepted only when the call part of this guest module. Unimplemented services return `STATUS_NOT_IMPLEMENTED` and generate telemetry. Arguments, pointers, sizes, permissions, quotas and process property are checked before accessing user data.

For an original NTDLL legally provided, `native_nt.py` reads the stubs and version feature of the same immutable set of bytes. Generates a map of service numbers for internal operations linked to DLL and profile. The kernel receives arguments by ABI x64 from the selected DLL; does not reuse the host syscalls table. Unknown entries, including numbers matching the old private protocol, do not enter that protocol.

The profile determines version, build and service pack in the invited structures. There is a separate PB, TEB by thread and a data page shared in `0x7FFE0000`, read-only and unexecuted for CPL3, with clocks consistent with the source published by the host. They are partial structures for this matrix; `PEB_LDR_DATA`, complete process parameters and `LdrInitializeThunk` are not implemented.

## Implemented matrix

| Family | Cut functional |
|---|---|
| Memory | NtAllocate/Free/Protect/QueryVirtualMemory in the process itself; reserve, commit, decommit, release and rounding of pages |
| Sections | NtCreateSection/MapViewOfSection/UnmapViewOfSection anonymous; two views share o same frames; views They survive. ed closing do handle |
| Handles | NtClose, NtDuplicateObject in the same process with SAME_ACCESS, NtQueryObject/BasicInformation; count and rights preserved |
| Cases/threads | NtOpenProcess/Thread, basic information from Case/thread, NtCreateThreadEx, NtResumeThread, end of the current chamador/Case |
| Synchronization | Events manuals/automatic, mutants, traffic lights and timers; Wait. simple/multiple; time limits absolute/relative |
| APC and time | NtQueueApcThread with three arguments, NtTestAlert, NtDelayExecution alertable, time and counter; return restored after callback |
| Files | NtCreate/Open/Read/WriteFile and QueryInformationFile/StandardInformation, I/O synchronous no volume RAM guest |
| Registration | NtCreate/OpenKey, Set/Query/DeleteValueKey and DeleteKey; rights and ACLs do volume guest |
| RTL | Version, startup/comparison of strings, limited status conversion, heaps with zero/realloc/free/size/destroy |
| TLS/FLS | already existing PE TLS; thread-standard fiber FLS, separate values and output normal/release callbacks |
| Ldr | LoadDll, UnloadDll, GetDllHandle and GetProcedureAddress on catalog and actual proper loader references |
| Exceptions | RtlRaiseException, SEH and catch of stack for infrastructure existing; unwind x64 v1 and subconjunto v2 validated |

NtTerminateThread is a low-level end: it does not perform FLS callbacks or DLL notifications. Normal thread output continues to execute them. Nested APCs, remote termination and creation of another process by native ABI remain rejected. NtTestAlert returns success after delivering APC; a interrupted alert waiting returns `STATUS_USER_APC`. This difference was checked on native Windows.

Native operations pass through existing policy/telemetry. `guardian-pe` records `boot.json`, `analysis.json` and `analysis.html`. There are request/result events with status; file/registry operations also record the feature in volume. This increases the observability of phase 13, without promising perfect malware detection.

## Limits and to-dos

| Appeal native | Limit of this delivery |
|---|---|
| Allocation VM / tracking | 8 MiB per call / 32 native intervals per process |
| Anonymous sections | 8, up to 256 KiB each, 512 frames shareable no total |
| Handles natives | 32 per process, also subject to the quotas of existing managers |
| Threads | Up to 4 per process and 16 global slots; private stacks and TEB/TLS |
| Processes on the native path | An external EXE by boot; native rearing of children still refused |
| Heaps RTL | 4, up to 1 MiB each; flags supported explicitly |
| FLS | 31 indices in the standard fibre; other fibers not yet implemented |
| Files/log changeable | Volume RAM 64 KiB; 32 knots, added payload up to 16 KiB, operations/files up to 512 bytes |
| Names natives | ASCII printable in Unicode descriptors, up to 127 characters; case-insensitive comparison RTL only ASCII |
| Unwind PE no kernel native | Up to 8.192 functions per module; previous kernels maintain 4.096 |

Quotas do not guarantee that all maximums will fit simultaneously. The changeable RAM volume is separated from the read-only EXE/DLLs volume: it is not NTFS, it does not persist and does not allow Ldr to load a saved DLL in that volume. Sections based on file/SEC_IMAGE, I/O asynchronous, named object namesspaces, additional information classes, Unicode/NLS complete, other heap flags and most of the original NTDLL exports remain pending.

The loader accepts the LoadConfig directory as a limited metadata; this does not implement CGG, CET or all the mitigations described in it. Unwind v2 accepts metadata exercised by DLL and synthetic reference. Epilogues outside the existing decoder return explicit refusal; no coverage of all possible instructions is assumed. The host continues with the kernel image limit of 1.5 MiB. CLI uses 256 MiB and the existing 30-second execution budget. Snapshots of this runtime and PE/SMP integration remain pending.

## References executed and acceptance

The original DLL trial uses NTDLL x64 extracted from the image 1 ed `install.wim` ISO Windows 10 22H2 en-BR provided by the user. Extraction does not execute DLL on host. The actual version of the file is **10.0.19041.2788**; the Win10 profile has built 19045. This family difference of enablement is recorded without claiming that there was comparison with a Windows kernel 19045 original.

- SHA-256 ed DLL: `fdb2689bffabe7d2e300882ca1c3fc2fe24a998ffcbd5f48795c7d95712d1e98`.
- SHA-1 matches the entry extracted: `959735a57905cb6037af73cb29a4cf99fb5f95fe`.
- 47 stubs recognized on the map. NtQuerySystemTime is a user mode routine
  which reads the shared data; does not receive an invented number of syscall.
- The original matrix exercises memory, sections, objects, APCs, files and some
  RTL routines. Do not exercise the original DLL's heap, FLS, Ldr or SEH.
- The matrix with own DLL also exercises record, heaps, FLS, creation and
  threads closure, SEH, stack capture and Ldr references in all three profiles: Win7 6.1.7601/SP1, Win10 10.0.19045 and Win11 10.0.22631.
- A self-test executable compares ABI, memory, sections, objects, APCs and
  Heaps with Windows host APIs. Another compares unwind with RtlVirtualUnwind. This does not replace exact native references of Win7/Win10/Win11, still pending.

Acceptance requires 21 CTest tests per configuration, none ignored: the 15 criteria of 14.2. and six new 14.3. The original DLL test is mandatory for acceptance of this machine; it is not declared approved by default when `GUARDIAN_NATIVE_REFERENCE_DLL` has not been configured. Full Release regression contains 96 tests. Local `build-phase14.3` reports and package validation files record the result effectively obtained and stable hashes of previous source code/binary. attempts and failed logs are preserved.

In the initial regression, the previous 90 tests passed; three of the six new tests failed. The native comparison corrected the translation of `STATUS_NOT_IMPLEMENTED` for Win32 1 error, and the reopening of file/registry started to provide the size required by the ACL consultation. The retest of the six native tests passed. The summary combines 96 distinct tests, without hiding the initial failures or stating that the first complete execution was approved.

Negative tests include supervisor pointers, overflow, commit that crosses page, obsolete handle, insufficient rights, multiple point allocation failure, and view rollback. Boots require release of all features beyond the 21 permanent bootstrap pages, complete telemetry and unknown calls rejected. The package is checked by hashes and started again in both settings.

## Reproduction

With MSVC 2022, Windows SDK, CMake, Python 3, WHP and NX active, set build with BUILD_TESTING, GUARDIAN_GUI_COMPATIBILITY_ONLY, GUARDIAN_BUILD_PE_GUI, GUARDIAN_BUILD_EXTERNAL_PE and GUARDIAN_BUILD_NATIVE_NT connected. Set `GUARDIAN_NATIVE_REFERENCE_DLL` for the legal copy of the original Win10 DLL. Compile Release and Debug before running:

```powershell
ctest --test-dir BUILD -C Release -j 1 --output-on-failure
python tools/validate_phase14_3.py --build BUILD --config Release --original NTDLL-LEGAL --output ACEITACAO-RELEASE-NOVA
python tools/validate_phase14_3.py --build BUILD --config Debug --original NTDLL-LEGAL --output ACEITACAO-DEBUG-NOVA
python tools/package_phase14_3.py --build BUILD --release ACEITACAO-RELEASE-NOVA --debug ACEITACAO-DEBUG-NOVA --output PACOTE-NOVO
python tools/smoke_phase14_3.py --package PACOTE-NOVO --output SMOKE-NOVO
```

To mount a matrix of its own: `python tools/native_nt.py bundle --build BUILD --config Release --profile win10 --output VOLUME-NOVO`. The `--original` option selects the legal DLL instead of its own implementation. Microsoft's ISOs, WIMS and DLLs are not part of the public Git or builds package. The full sources of the project go to Aegis; the build of this phase goes to Aegis-builds private, along with the previous phases.

## Technical sources

- [NtCreateSection — Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatesection).
- [RtlGetVersion — Microsoft](https://learn.microsoft.com/en-us/windows/win32/devnotes/rtlgetversion).
- [Exception treatment x64 — Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170).
- [ .NET’s Unwinder AMD64 — official code](https://github.com/dotnet/runtime/blob/main/src/coreclr/unwinder/amd64/unwinder.cpp).
