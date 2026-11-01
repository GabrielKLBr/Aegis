# Phase 8 — Preparation of the search for x64 handlers

**Continuation at 18/10:** the search was integrated into the CPL3 dispatch, continuation, unwind and dynamic DLLs. The Linux matrix passed. The document below preserves the previous preparation; Windows acceptance of this review now requires 40 CTests per configuration and still pending. Current status and commands: [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).

Reference: 18/10/2026. Product platform: **Windows x64, MSVC and WHP**. Status: **preparation implemented, native validation pending**. Phase 8 continues to be underway; phase 9 has not been started. The last milestone validated in Windows remains [PHASE-8-UNWIND.md](PHASE-8-UNWIND.md).

## Amendment

`guest/Unwind.h` exposes `InspectHandler`. The operation virtually restores a caller and identifies the exception or end handle in `UNWIND_INFO` v1. Returns the handle address, language data, fixed function allocation basis (stablisher frame) and primary function identity. Addresses are guest metadata; no routine is called.

Accepts EHANDLER, UHANDLER or both and follows strings up to primary function, limited to 32 entries. Do not publish handle during the initial prologue, epilogue, sheet function or when the requested type does not match flags. A frame logger not yet established by SET_FPREG is not used as the base of the stack. The path ignores the operating slots when searching for this operation.

The handle trailer respects the completion for alignment of four bytes. The routine should stay within the same image and go through the executable mapping query provided by the backend. This query is mandatory in the new API. Language data is optional: your address can point immediately after the image, without granting reading permission at that address.

All readings continue to go through the backend, with image and stack limits. Failure, incompatible metadata or exceeded limit preserve both context and metadata output. A successful unhandling operation zeros metadata. The `End` result also preserves both outputs.

`Step`, `Capture` and the loader maintain previous refusal to records/pictures with language handlers. The new inspection is not yet linked to traps or a user API. There is no new SEH support statement, C++ exceptions, Fault recovery, handler return or unwind non-local.

The format follows [treatment of exceptions x64 from Microsoft](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170) and [RtlVirtualUnwind](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlvirtualunwind). The format of language data remains the responsibility of the future handler.

## Verification at this session

The available environment is Linux. Only operating system independent C++ components have been compiled directly; the product CMake remains unique to Windows. WHP, MSVC, MASM and the Windows oracle have not been executed.

| Verification isolated | Local result |
| --- | --- |
| New tests without optimization and with AddressSanitizer/UndefinedBehaviorSanitizer | 3.059 verifications approved |
| New tests, optimized | 3.059 verifications approved |
| Scenes main of inspection | 155 approved |
| Reading failure points injected into these scenarios | 980 approved |
| Portable return of the previous engine in both compilations | 1,045 checks, 59 scenarios, 391 fail points and additional seven-point control |
| Isolated freestanding compilation, no C++/RTTI exceptions | approved; no external symbols on the GCC object produced |
| Comparison with Windows, kernel image and WHP boots | **pending** |

Additional controls cover trailer without codes, optional data at the end of the image, exactly 32 chain entries and refusal of a 33rd entry. There are non-executable address refusals, incompatible flags, truncated trailer and late failure without publishing partial context.

Previous tests have only received build conditionals to allow this isolated scan. In Windows they continue calling `RtlVirtualUnwind` and keep historical markers. Outside Windows they explicitly print that the native comparison has not been performed.

Evidence of this session: `build-phase8-handlers-precheck/validation.json` and logs in the same folder. These results do not replace Windows acceptance.

## Repeat validation in Windows

In the project folder, with Visual Studio 2022 C++ x64, Windows SDK, CMake 4+, Python 3.9+ and WHP available:

```powershell
python tools\validate_phase8_handlers.py
```

The verifier requires Windows x64 and creates a new tree with the name `build-phase8-handlers-windows-<data>-<id>`. Compiles all targets in Debug and Release, audits the guest and requires **38 CTests**, flawless or ignored. Includes the previous WHP tests, comparison of the 59 old scenarios and 153 new context comparisons, selection of handler, data and publisher frame. These 153 comparisons are implemented in the test, but not yet executed.

The `phase8-handlers-windows-validation.json` manifest records failures and only indicates approved validation if the two settings comply with the contract. Font hashes are checked before and after. `build-codex` historical logs and disks are not used as the destination of this execution.

Contract: `config/phase8-handlers-acceptance.json`. Copy of the previous six files: `snapshots/codex-before-phase8-handlers-20261018/`, with SHA-256 manifest.

## Continue

First perform native validation and correct any divergences. Then integrate the search to dispatch in CPL3, set the full context and validate call, return and continuation of handlers without accepting addresses/permissions undue. Stack limits should come from the PID/TID link in the kernel, as in the previous capture, and not from modifiable fields in the TEB.

Non-local unwind, load/discharge dynamic DLLs, expansion of startup/implementation/end failures and final audit per application remain pending. This preparation does not complete phase 8 or authorises to start phase 9.
