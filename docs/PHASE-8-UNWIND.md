# Phase 8 — Unwind virtual x64 and call capture

Reference date: 17/10/2026. Status: **phase 8 in progress**. This is the fifth milestone. Phase 9 was not initiated.

## Result

The kernel now virtually restores the context of an x64 caller using `.pdata` and `UNWIND_INFO` v1 of the loaded PE modules. The operation works on a copy of the loggers: it does not modify the checked stack, does not resume code and does not execute handlers. It is a basis for the treatment of exceptions; the dispatch of user exceptions and non-local unwind remain pending.

The actual DLL exports a subset of `RtlCaptureStackBackTrace`. The request starts from a real syscall in CPL3, with the registers saved by the kernel. The first internal calls of the runtime are omitted. The program receives up to 16 return addresses from its executable modules, can omit up to 16 callers and can request the sum of 32 bits of addresses like hash. A successful capture preserves `GetLastError`, including when omit callers produces an empty list. Parameters outside those limits fail. Access to invalid caller pointers still follows the fatal behavior of guest page failure; this API does not implement SEH for these accesses.

The kernel gets stack limits by linking PID, TID and private thread allocation. It does not trust `StackBase`/`StackLimit` written in the user's TEB. The proof temporarily changes these fields and continues to capture the correct stack. Readings pass through the process tabs, with border check and overflow; they never use Windows host addresses.

## Supported format

The engine without CRT, STL or Win32 handles sheet functions, small and large allocations, push/save of non-volatile registers, pointer frame, XMM6–15 registers, machine frames and metadata chaining. In a partial prologue, only performs the operations already achieved. It recognizes epilogues with RSP adjustment, `lea` by the pointer frame, pops, return, direct jumps out of function and indirect jump via RIP-relative slot. The route limits the chaining to 32 inputs and captures to a fixed amount of frames.

The tables belong to modules already validated and protected by the loader. The engine rechecks the used inputs, codes, lengths, registers, relatives and readings. An error does not publish partially restored context nor a capture prefix. The arrival at bootstrap outside the PE modules closes the course normally without trying to interpret your assembly stack.

There is no declared support for v2/v3 metadata, JIT tables, language handlers, SEH/C++ exceptions or all possible formats of tail call. It is not a complete export of `RtlVirtualUnwind` or `CONTEXT` from Windows. The process continues to be terminated if an untreated user exception reaches the current path of traps.

Format references: [unwind x64 from Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170) and [prologues and epilogues x64](https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=msvc-170). The implementation is proper and limited to the paths described above.

## Validation

The native test approves 2,166 configuration checks and compares restored loggers with Windows `RtlVirtualUnwind` in 59 scenarios. It covers partial prologues, before/later fixed allocation saves, SIMD, frame pointer with dynamic stack, machine frames, epilogues and chaining. Each reading of these scenarios suffers an individual injected failure: there are 391 points, plus seven of a separate control, verifying that the original context remains intact. There are additional tests of invalid metadata, guards, limits, capture, hash, frame omission and subsequent failure without publication of partial results. This call to Windows exists only in the native comparison test.

In the guest, the console itself runs through a chain of calls that crosses the executable and the DLL. It checks the modules of the frames, the default list, the hash, error preservation, false limits on the TEB and rejection of malformed requests. The proof runs on the initial thread and on three workers with private stacks. It fully maintains the previous test with 31 workers, 360 protected increments, 23 child processes and ten boot creation failures. By boot there are 16 frames and four empty catches after omitting the chain.

The `PE x64 virtual stack capture PASS` marker only leaves after the console zero return and cleaning of all PE processes. The current contract requires 37 CTests in Debug and Release, without ignoring, and 800 bootes in the complete matrix, with regression of phases 1–7 and the previous milestones of phase 8.

The commands and evidence of this landmark use the `phase8-unwind-` prefix:

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
python tools\validate_phase8_unwind.py --snapshot snapshots\codex-before-phase8-unwind-20261017 --run-programs
```

The snapshot saves the originals. The checker checks the hashes of the previous evidence and disks of phase 7 and the previous four milestones of phase 8. The new manifest records sources, images, programs, reports and disks. No boot, NX or Windows host security setting is part of this milestone.

## Next work within phase 8

There are still no dispatch of exceptions for the user, call and return of handlers, non-local unwind, dynamic loading/download of DLLs, expansion of failure cases and the final audit of applications. The completion of the phase requires these behaviors; capturing calls does not end phase 8.
