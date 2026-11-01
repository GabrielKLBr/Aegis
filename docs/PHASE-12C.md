# Phase 12C — system behaviour fidelity

**23/10/2026 Windows Update:**The current matrix has passed on MSVC/WHP: 80 distinct tests Debug, with documented retests, and 80/80 Release in a complete suite, without ignoring. [Corrections and Evidence](WINDOWS-PHASES12B-13-REVIEW-20261023.md). The Linux records below are historical; the scope and comparison pending versions remain open.

**State in 21/10/2026:** Linux implementation and validation approved in the review `acceptance-20261021-full-01`: 50/50 Debug, 50/50 Release and 32/32 ASan/UBSan, 382 stable source files. Formal 12C closure remains pending from native Windows references and revision of 12B dependencies. The project remains Windows/MSVC/WHP, with native development and testing Linux/KVM. The 12D was later authorised and is registered in [PHASE-12D.md](PHASE-12D.md).

Changes are saved directly to the original folder of the HD. The previous state has been preserved in `build-phase12c/baseline-20261021/original-source.zip`, with hashes and manifest. Linux compilations are out of NTFS to preserve existing Windows builds; the final evidence will be copied to the HD.

## Common matrix and references

`shared/guardian/ConformanceProgram.h` contains the same program of 53 cases for time, exceptions, synchronization, memory, handles, I/O, API results and consistency of architecture/identity. The guest uses its own DLL runtime, passes by CPL3, syscalls, scheduler and devices. The reference executable uses the actual Windows APIs with its own temporary files, memory, threads and mutexes. It does not use third-party programs or personal files.

Each case has a stable identity, domain and observed result. The expected values describe the contract; they are not presented as Windows measurements. The program and its entries receive an embedded hash to executables and matrices. Observations of another revision, architecture, NT version, incomplete or repeated cases are refused. Reference capture requires native Windows x64; Linux and Wine are refused in that path.

The guest matrix covers the Win7 (`6.1.7601`), Win10 (`10.0.19045`) and Win11 (`10.0.22631`) profiles, with 16 and 64 MiB, repetitions and snapshot restoration. A different Windows version is identified by the real number returned by `RtlGetVersion`; it is not renamed to cover another version. Unauthorized versions remain pending even with all approved Linux tests. x86 and NT5/NT6.0/NT6.2/NT6.3 profiles do not enter this x64 matrix.

## Behaviors implemented and corrected

- Watches: `QueryPerformanceCounter/Frequency`, `GetTickCount/64`,
  `QueryUnbiasedInterruptTime` and `GetSystemTimeAsFileTime` are derived from a common publication. UTC origin comes from the host clock in VM preparation and is preserved in snapshot; the ranges use the monotonic clock. Each query updates the publication by a controlled output to the VMM, avoiding limiting the counter to the last periodic interruption. The virtual frequency is 10 MHz. No host clock is changed.
- finite `Sleep` blocks the thread up to the deadline, with the same clock as the queries;
  `Sleep(0)` gives execution. An unsupported request does not return as a complete wait: generates an explicit exception. `Sleep(INFINITE)` remains outside the supported subset.
- Mutex without name accepts initial possession, counts recursion and only releases
  The cases cover ownership, timeout zero, recursion, abandonment, thread ending, invalid handles and isolation of `LastError` by TEB.
- Private memory: reservation and compromise combined in `VirtualAlloc(NULL, n,
  MEM_RESERVE的MEM_COMMIT, ...)`, query, protection and actual release, with initial zero, granularity of 64 KiB, pages of 4 KiB and quota of the process. A property record prevents releasing or protecting mappings from the loader, TEBs and stacks through this API. Commitment failure unmakes the reservation. There are at most eight simultaneous allocations per process in this path.
- I/O: creation, reading, writing, EOF, seek, size, sharing, access
  denied, truncated and results/counts; re-check or close an invalid handle produces error. The program files stay in the guest's own volume.
- Exceptions: invalid instruction recovered by context, division by zero with
  execution of `finally` and exception raised with arguments. The test passes the dispatcher and unwind of the guest; the reference uses native SEH.

The complete Debug kernel has grown beyond the previous image limit of 1 MiB. The limit has gone to 1.5 MiB, still below the bootstrap stack in 4 MiB, with explicit verification of this separation and regression to reject larger images. PE validations, section limits, W^X and physical reserve remain active. The Debug program has also exceeded 128 function inputs in `.pdata`. Loader and unwinder now share a limit of 256, maintaining full metadata validation and a common roof; there is no silent truncation of the table.

## Declared differences and limits

VM time is frozen while a snapshot is paused or stored. Resumption preserves the time elapsed and UTC origin, without including the offline interval. This is an intentional difference from the actual Windows suspension/hibernation. The matrix checks the VM contract; it does not present it as a Windows hibernation test. The quantization of the Windows timer is considered when measuring `Sleep`; maximum duration under scheduling load is not imposed.

Runtime is an explicit subset. Mutex security names and descriptors, `Sleep(INFINITE)`, address allocation requested, reserve/commitment separate, decommit, guard/implementation protection, JIT and a `KUSER_SHARED_DATA` page in Windows address are not announced complete. PE profiles continue with a vCPU while SMP integration of 12B remains pending. The `resources` contract retains its own SMP matrix; it does not prove a multiprocessor PE scheduler.

The old `MiniNt` demos do not conform to this delivery. The matrix reaches the kernel itself and the runtime declared above. Provisional interfaces of other demos and general compatibility with Windows are not classified as full support.

## Validation and continuation on Windows

The consolidated result is in [`result.json`](../build-phase12c/acceptance-20261021-full-01/result.json), with commands, times, input hashes, JUnit files and comparisons. The execution was rebuilt in new folder, without changing the 382 source files during compilation and testing. There were no failures, skips, Wine or QEMU.

| Profile native Linux | CTests | Matrix 12C |
| --- | --- | --- |
| Debug/KVM | 50/50 | 15 boots × 53 cases = 795 Remarks |
| Release/KVM | 50/50 | 15 boots × 53 cases = 795 Remarks |
| Core ASan/UBSan | 32/32 | 8,322 checks of the new core; no boot KVM |

There are **132 CTest approved** and **1.590 guest matrix observations**. The three NT profiles pass with 16 and 64 MiB, two repetitions per size and one restoration by profile/configuration. There are also 12 invalid snapshot clock rejections by setting: wrong frequency, invalid UTC origin, odd sequence and erasure of the two new fields. The core test checks memory rollback, quotas, ownership of the mappings and clock sampling.

The complete arrays of observations are in [`Debug`](../build-phase12c/acceptance-20261021-full-01/host-Debug/conformance-Debug/matrix.json) and [`Release`](../build-phase12c/acceptance-20261021-full-01/host-Release/conformance-Release/matrix.json). Comparisons explicitly record `referenceCompared: false` and the Windows versions still missing: `6.1.7601`, `10.0.19045` and `10.0.22631`. An empty list of divergences in that state does not mean approved Windows comparison.

The hash of the common program is:

```text
258fb7ba10d3dfadaf3d7f3c4a9707e328313e8091b0cc9b56a053a25758d1bd
```

The cross-review of syntax of the eight host/reference Windows files passed with LLVM-MinGW, without running Windows binaries. The registry identifies the macros used only to review build metadata. It does not constitute MSVC compilation or WHP acceptance. The development history remains separate from final acceptance and does not enter those totals.

In Visual Studio 2022 x64 terminal, in the original project folder, using a new output folder:

```powershell
python tools\validate_phase12c.py --output build-phase12c\windows-full-20261021 --generator "Visual Studio 17 2022"
```

This stream compiles with MSVC, executes guests in WHP and captures the native reference of the Windows version actually used. To compare references collected at other authorized installations, add `--reference caminho\reference.json` once per exact version. The hash of the program needs to match.

## Delivery in original folder

The fonts and these Markdown are in the original folder of the HD. Previous backup remains in `build-phase12c/baseline-20261021`. The selected final evidences were copied to `build-phase12c/acceptance-20261021-full-01`, preserving the builds and records of the previous phases. The complete Linux compilation continues in `/home/gabri/Projetos/GuardianAV-12C-build-20261021`.

In `build-phase12c/delivery-20261021`:

- [`GuardianAV-12C-fontes-20261021.zip`](../build-phase12c/delivery-20261021/GuardianAV-12C-fontes-20261021.zip):
  code, documentation and evidence selected to continue on Windows.
- [`GuardianAV-12C-linux-20261021.tar.gz`](../build-phase12c/delivery-20261021/GuardianAV-12C-linux-20261021.tar.gz):
  six ELF Release executables and guest's own images. The `bin/guardian-conformance` program repeats the matrix with the conformity image.
- [`SHA256SUMS.txt`](../build-phase12c/delivery-20261021/SHA256SUMS.txt) and
  [`delivery.json`](../build-phase12c/delivery-20261021/delivery.json): identification and conference of packages.

The ZIP is checked against all 382 hashes of the accepted code. The package's ELF executables match the validated Release binaries. The guest images are given to the VMM; their execution bit is removed in the Linux package. The diff since the backup is in `build-phase12c/review/changes.diff`.

The formal termination of 12C requires reviewing the 12B dependency and completing the native references of the declared versions. Linux approval, syntax cross-review and comparison with expected values do not replace these references.

## Contract references

The test considers the quantization and resumption described in [Sleep](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep), the frequency/count separated from [QueryPerformanceCounter](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter), the initial possession and recursion of [CreateMutexA](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa), and the coverage of pages intersected by the interval in [VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect). These specifications guide the cases; references performed remain separate evidence.
