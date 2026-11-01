# Phase 14.5 — original C/C++ runtimes in MiniNT

Windows x64 implementation for a delimited console corpus, compiled by MSVC 19.44 with `/MD`, normal startup and FH3 and FH4 handlers. The kernel and NTDLL/KERNELBASE/KERNEL32 layers remain unique. UCRT, VC runtime, MSVCP and MSVCRT selected are local legal entries and run exclusively on the guest. This phase does not establish compatibility with most Windows applications.

## What has been implemented

- Kernel `guardian-kernel-crt.gk` and variants of the four support DLLs;
  previous variants remain available for regression.
- Win32 start-up dependencies, process-coded pointers,
  module query by address, location, CRLF console and synchronization. Critical sections recursive, exclusive SRW, init-once and condition variables work on the guest, without consuming a handle by CRT lock.
- CRT API sets forward to the selected UCRT using reencoded data
  schema Win10. No fallback for host functions or addresses.
- C++ exports with decorated names of up to 511 characters and data imports.
  The corpus uses original MSVCP `std::cout`, as well as strings and vectors.
- `RtlCaptureContext`, unwind metadata query and virtual restore
  limited from a frame. Metadata comes only from guest process modules; private syscalls are limited to authorized own DLLs.
- Unwind consolidation for C++ catches by value and reference; callbacks and
  Destructors perform in CPL3. Record, frame pointer, context and destination are copied/validados before any resumption.
- Correction from `EXCEPTION_TARGET_UNWIND` to `0x20`, checked against SDK.
  `0x40` is the bit of collided unwind; the previous constant caused the original CRT to destroy objects outside the scope of the `try` that were alive after the `catch`.
- FLS cleaning before destroying the heat that stores CRT data.
  maximum zero can grow within the 1 MiB quota of implementation. The PEB informs the same heat returned by GetProcessHeap.
- Registered inheritance attribute for CRT file handles, without enabling
  legacy of handles in the creation of processes. External security descriptors and flags without implementation return error.

## Acceptance corpus

| Scene | Scan |
| --- | --- |
| C/CRT program with own diagnostic input | UCRT original, malloc/free, math, puts/flush, catch and unwind of context. |
| MSVC `/MD`, FH3 | Global builder, main, environment, strings, vector, Cout, int/runtime_error exceptions, RII and live objects after catch. |
| MSVC `/MD`, FH4 | Same program using `vcruntime140_1.dll` and the current compiler's FH4 metadata. |
| Threads | `std::thread`, mutex/condition variable, errno and TLS isolates, constructor/destrutor of thread_local. |
| Files and conversion | fopen/fwrite/fflush/fseek/fread/fclose/remove, missing file, realloc, mathematical error and locale UTF-8 with UTF-16 replacement pair. |
| Case son | CRT's normal startup with three arguments, including `hello world` in quotes, waiting and child cleaning. |
| MSVCRT original | LoadLibrary, malloc/free, strlen and atoi; download. Optimisation LFH refused explicitly. |
| Limits | CopyFileW without implementation ends the program with diagnosis; rethrow C++ is refused and the analysis is inconclusive. |
| Integrity | File tampered with and runtime outside version/digest policy is not accepted. |

`crt_windows_reference` only runs our two copied test programs for a clean folder using the installed Windows DLLs. The DLLs provided for the guest are not executed on the host. This reference confirms the corpus results, not kernel equivalence of an exact NT review.

## Versions and requirements

`config/crt-references.json` fixed size, version and SHA-256 of the nine inventoried references. The five effectively exercised inputs are MSVCRT 7.0.19041.546, UCRT 10.0.19041.789, `vcruntime140.dll`, `vcruntime140_1.dll` and `msvcp140.dll` 14.44.35211.0. The four additional MSVCP extensions were only inventoried; they do not receive execution acceptance at this stage.

The selected package was accepted only in the NT Win10 profile. Win7 and other combinations are rejected by the volume builder, including Win11 still without own CRT matrix. The runtime version is not inferred from the NT version. A new version requires explicit policy and new validation, not only rename DLL. The references come from the legal ISO already provided and from the VC Redist of the Visual Studio local installation; they are not automatically collected from the host.

The redistribution rules continue those of suppliers: [Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-160), [UCRT](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment?view=vs-2019) deployment. The package of this phase contains only its own components, compiled programs and evidence. The user again provides the legal DLLs fixed to build a local volume; ISOs, inbound DLLs and volumes with Microsoft code do not enter into the publication of sources or build.

## Limits that remain

- Rethrow, exceptions nested during a catch/cleanup, collided unwind and
  translation of exceptions are not accepted. The second exception is contained and cannot produce an execution verdict complete/safe.
- `RtlVirtualUnwind` does not implement contextual pointers or variants other than
  corpus x64 limited. Unwind data incompatible fail in contained form.
- 1 MiB per heap, four heaps per process, three processes and four threads
  by process. Quotas were not removed to accommodate CRT.
- Mutable files remain limited to 512 bytes, with ephemeral volume and
  shared within session. No access to actual host files.
- Locale: C, UTF-8 of the corpus and selected metadata pt-BR/en-US; is not NLS
  complete. Console accepts ASCII/tab/CR/LF, with a budget of 4096 bytes; the Unicode conversion of CRT does not equal the complete Unicode console.
- `validation/crt-export-coverage.json` identifies imports that close the
  program case called. Other unimplemented contracts return explicit error such as HeapSetInformation/LFH, pipe, enumeration and metadata of files, local times and date formatting. They do not count as functional APIs just because the symbol can be solved.
- No executables of different toolchains, `/MT`, CRTs of
  Microsoft debug, GUI, DirectX/WinRT or Flappy. Build Debug uses `/MD` with normal redistributable runtimes, without including Microsoft debug DLLs.
- Phase 14.6 was not initiated. Previous SMP/compatibility to-ed
  and the other phases remain identified in the roadmap.

## Verification and reproduction

Four new CTest tests: `crt_contract`, `crt_windows_reference`, `crt_runtime_tools` and `crt_runtime_integration`. Delivery criteria: Full Release suite of 104 tests, acceptance of 29 tests without ignored in Release and Debug, stable source code/binary/references, and package smoke test in both settings. The numbers effectively obtained are in the validation JSON/XML; incomplete attempts are preserved separately.

```powershell
cmake -S . -B BUILD -G "Visual Studio 17 2022" -A x64 -DGUARDIAN_GUI_COMPATIBILITY_ONLY=ON -DGUARDIAN_NATIVE_REFERENCE_DLL=NTDLL-LEGAL -DGUARDIAN_APISET_REFERENCE_DLL=SCHEMA-LEGAL -DGUARDIAN_CRT_REFERENCE_DIR=PASTA-CRT
cmake --build BUILD --config Release --parallel 6
cmake --build BUILD --config Debug --parallel 6
ctest --test-dir BUILD -C Release -j 1 --output-on-failure --output-junit REGRESSAO.xml
python tools/validate_phase14_5.py --build BUILD --config Release --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --reference PASTA-CRT --output ACEITACAO-RELEASE
python tools/validate_phase14_5.py --build BUILD --config Debug --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --reference PASTA-CRT --output ACEITACAO-DEBUG
python tools/package_phase14_5.py --build BUILD --release ACEITACAO-RELEASE --debug ACEITACAO-DEBUG --regression REGRESSAO.xml --output PACOTE-NOVO
python tools/smoke_phase14_5.py --package PACOTE-NOVO --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output SMOKE-NOVO
```

Stop after delivery of this phase; the automatic publication of the commit does not allow to start on 14.6.
