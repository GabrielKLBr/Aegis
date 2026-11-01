# Phase 8 — Loader, runtime and console applications

**Continuation in 18/10/2026:**Excepts, non-local unwind and dynamic DLLs implemented and accepted in the matrix Linux/KVM. Windows acceptance of this review remains pending. Current status and criteria: [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md). The results below record historical milestones; phase 9 was not started.

Active base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. State in 17/10/2026: **in progress**. The first milestone, described below, covers loader PE and console with DLL/TLS. The second milestone connects files, log, console, heap and locks: [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md). Current validation sums up 35 tests per configuration and 720 boots. Phase 9 was not started. Numbers of 33 tests and 640 boots below record validation of the first milestone.

## Implementation of this milestone

`guest/PeLoader.cpp` reuse the concepts of the previous MiniNt loader (PE32+, DIR64 relocations and import resolution), replacing its host dependency of Win32, STL and stubs with a freestanding backend. Parser, relocations, DLL connection, and the application of permissions run on guest CPL0. Addresses recorded in IAT belong to guest images.

The loader checks headers, sizes, alignments, boundaries and overlapping of sections, directories, strings, import descriptors, thunks and exports. Accepts imports by name or ordinal and the alternative with `OriginalFirstThunk` zero. DLLs are found in an explicit list of fonts; names have not seen Windows host paths. Absent dependencies, cycles, missing symbols, and unimplemented features produce failure. A rejected load removes the new images, preserving previously loaded dependencies. Removal failures remain traceable and allow for a new cleanup attempt.

The images start recording and without execution. After the fixups, the headers are read-only; each section receives R, RW or RX, and gaps run out of access. W+X and IAT sections within the code or headers are refused. In the guest, these permissions pass through the existing memory manager and the physical permissions of the WHP.

There is reading and relocation of the TLS directories, index assignment and validation of templates and callbacks. Bootstrap of the initial matrix installs template and zeroed part, publishes the TLS vector in the TEB, calls the callbacks and `DllMain` in the dependency order, executes the console entry point and makes the detach at the normal output. Initializes the fields used by the application: identity, stack boundaries, TB pointer, TLS vector, PEB pointer and image base. These fields do not yet constitute a complete implementation of PEB/TEB.

The loader checks the limits of the `.pdata` table, the functions tracks and the unwind code structure version 1 without handlers. A x64 sheet function can dispense that table; the optimized DLL in Release exercises this case. **There is no user exception dispatch or unwind execution yet.**

## Application and behavioural criteria

The files in `guest/fixtures` are compiled by MSVC as a console executable and a real PE DLL without CRT: `guardian-pe-console.exe` and `guardian-pe-runtime.dll`. A generator incorporates its bytes into the test kernel. No host-remedy host host machine code for these PEs. Relocation and IAT modifications are made by the guest loader.

The console runs in CPL3, in a private process space, with its own stack and memory protection. The criteria exercised are:

- The two modules carry out of the preferred addresses, with DIR64 applied.
- Two imports, by name and ordinal, point to functions distinct from DLL.
- TLS callbacks of both modules run before their respective points
  input, and `__declspec(thread)` accesses use the vector published in the TEB.
- `DllMain` confirms the state produced by its TLS callback.
- The console gives identity, PEB and stack limits, gets 42 of the call to
  DLL and 17 of the function imported by ordinal, and ends with zero code.
- Unmounting and ending return all pages allocated to the test.

The `PE loader mapping checks PASS` and `PE console TLS DLL execution PASS` messages are only issued after the respective criteria. The process report keeps the counters of the previous matrix; the additional execution of the console is checked separately by those messages and the boot test of phase 8.

## Validation and evidence

Validation of this milestone requires complete Debug and Release, 33 tests per configuration and no unknown test. The matrix adds 640 boot: 80 with the new loader/console and 560 regression of phases 1–7. Each new boot checks 26 mapping conditions before running the console and the previous matrix. Previous tests maintain their 240 external failure scenarios in total.

The native test runs the same `PeLoader.cpp`, using the two compiled PEs and a backend that applies permissions. Covers truncated files, malformed headers and directories, overlaps, W+X, imports, ordinals, DLLs, TLS and invalid unwind metadata. Injects failure in each backend operation of the load path and checks rollback and absence of published modules. Also exercises preloaded dependency and unmap failure with new attempt. The totals of each configuration stay in `phase8-change-manifest.json`.

A new tree, `build-phase8-clean`, validates the compilation of the eight production images and their PE contracts. The seven reference examples of MiniNt are executed by configuration, along with audits and tests by CLI. Evidence files and new disks use `phase8-` prefix. The 347 historical files registered in the snapshot and 141 phase 7 disk artifacts remain checked by SHA-256, without overwriting.

Current commands are in [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md). The `phase8-*` reports of this first milestone are historical; the continuation uses `phase8-runtime-*` to preserve them. Manifestos explicitly maintain `status: in_progress` and `next_phase_started: false`.

## Limits and work remaining from phase 8

The loader accepts up to four modules of 128 KiB and 16 sections per module, with section alignments of 4 KiB and file of 512 bytes. There is limit of 256 relocations DIR64, 256 exports, 256 thunks per descriptor, eight TLS callsbacks per module and 256 bytes of TLS per module. The current execution matrix uses two modules and one thread per PE process. The kernel image remains limited to 1 MiB, no imports, TLS or proper relocations.

Forwarders, imports postponed/bound, SEH handlers and other unimplemented directories are refused. Upload is initial and follows a fixed catalogue of own files; there is no file yet `LoadLibrary` User. The proof console does not use CRT or Windows DLLs. There is no claim to run Windows arbitrary or general compatibility programs. Second milestone update: unwind metadata chains are now validated to accept the optimized output of the compiler, yet without running unwind. See [PHASE-8-RUNTIME.md](PHASE-8-RUNTIME.md).

To complete phase 8, there is still a lack of:

Update: The items 1 and 2 below already have the initial subset validated in [second mark](PHASE-8-RUNTIME.md). The remaining extensions and limitations are in this document; children/threads PE, exceptions and dynamic DLLs remain pending.

1. Connect the PE runtime to the console matrix NT/Win32 APIs, including
   files, log, error and console output.
2. Integrate real heat and locks to runtime in user mode.
3. Raising children and threads from PEs, with TEB/TLS private by thread,
   thread callbacks, cancellation and cleanup on fault paths.
4. Implement dispatch of user exceptions and unwind x64, with proofs of
   recovery and spread between functions.
5. Complete dynamic load/discharge of DLLs and the boot failure matrix,
   execution and termination, with criteria per application.
6. Gather the final phase audit and stop before phase 9, as requested.

Format references: [PE/COFF from Microsoft](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) and [Excess treatment x64](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
