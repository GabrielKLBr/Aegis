# Phase 5 — Guest processes, objects and security

Active base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. Image: `guardian-kernel-processes.gk`, version 0.5.0, x64, a vCPU, WHP. Status: completed in 16/10/2026. Phase 6 was not initiated.

## Implementation

`guest/ProcessManager.cpp` implements processes, handle tables, objects, tokens and access decisions in limited structures, without STL, CRT or Windows host APIs. `ProcessKernel.cpp` integrates this manager to phase 3 memory, to phase 4 scheduler and to phase 2 syscall/interruption entries. WHP executes guest code; the host transports reports, physical permission requests, and existing timer interruptions.

### Processes and memory

Each process has an address space with CR3, data, private allocations, batteries and own security context. The selection of a thread also selects the space of its process. The same virtual address may contain different data in each process. The kernel pages remain supervisors.

The kernel entry saves the context and activates a separate kernel space and idleness. Recovery of a closed process happens after leaving your CR3. Finishing a process closes all your threads, cancels your waits, abandons the possessed locks and recovers memory and handles. One default in CPL3 closes the responsible process; the others continue to be staggered.

Raising a child derives its token from the father and receives an explicit list of inherited handles. Creation is transactional: memory space failures, objects, handles or partial inheritance undo what has already been created. Boot program also undoes stacks and threads if the child's preparation fails.

`ExecutionContext.h` brings together the same context of 720 bytes used in phase 4: General Loggers, RIP/RSP, filtered RFLAGS, selectors, FS/GS and FXSAVE64 bases. Threads and processes images use these common routines. Thread regression continues to check x87/MMX/SSE, XMM0–15, rounding status and NT/IRETQ case corrected in phase 4. AVX/XSAVE remain disabled. `StackProbe.asm` provides a real `__chkstk` for stack frames larger than a page, touching each intermediate page and preserving the loggers required by the ABI of the compiler.

### Handles, references and namespace

Handles belong to the table of a process and carry type of object, access mask granted and attributes of inheritance/protection against closing. New values are monotonic; Process IDs and object have generation. Copying a handle number from another process does not make it valid in the local table. In the authorized inheritance, the child intentionally receives the same value of the inherited handle.

Duplication requires the right to duplicate in the handle of origin and in the handle of the destination process. The result cannot enlarge the mask of origin and passes through the ACL and integrity of the recipient token. The inheritance also revalidates the access of the child. This policy is internal ABI, more restrictive than a transfer of capacity without revalidation; no binary equivalence with `DuplicateHandle` or creation of Windows processes is declared.

Handles references, pending waits and lock property keep the object alive. Close the last handle during a wait or while there is owner does not destroy the object prematurely. A shared object can survive the output of its creator while another process or waits for the reference. Case/thread completion objects also survive the reap when external references exist; the output code remains consultable.

There are `\Global\nome` and `\Session\nome` names, separated by the numerical session of the token. Creating a global name requires the internal privilege `CreateGlobal`; opening an existing name requires the rights requested in the ACL. Names are ASCII, capital sensitive, no path components, with up to 63 bytes in the final name. Type collision is refused. These names are not host paths.

### Tokens and access decisions

Unchangeable Tokens register user, primary group, up to four additional groups, session, three levels of integrity and the privilege of creating global objects. Identities are internal guest numbers; they are not Windows host tokens or credentials. The child maintains identity, session, and primary group, can reduce integrity/privileges and convert enabled groups into groups used only for denials. You cannot add groups, remove parent groups, or rehabilitate a denial group. Father and child are validated before comparing restrictions.

Descriptors keep owner, group, integrity, non-writing policy to higher level and a DACL of up to eight entries. DACL accepts user, group or all, with denials before permissions. DACL empty denies; DACL absent allows, after integrity policy. The owner has implicit control of DACL reading/Amendment. Denial groups do not grant access.

Rights include waiting, query, modify, duplicate, read/write process memory, ending, raising child and read/change DACL. Generic rights are converted by type; invalid bits are refused. Changing DACL requires `WriteDac` and does not allow changing owner, group or label by that way. Change affects new openings, preserving already granted masks. Integrity policy blocks lower level writing/control operations on higher object; reading is decided by DACL.

Copying between processes requires handsets with explicit read or write rights of memory. All bytes of origin and destination are validated before modifying any destination. There is a limit of 4 KiB and an intermediate buffer for overlapping. Addresses are translated by the guest tables; a pointer received from CPL3 does not become an arbitrary host pointer.

## Acceptance exercises

The same core of 693 checks performs in native test and CPL0 on each boot of the process image. It covers restrictions of token, DACL empty/nil, denial groups, rights, integrity, types, namespacing per session, duplication, partial inheritance, closing protection, references and recovery. Includes exhaustion of 32 handles, 64 objects and four threads per process, commit quota, all 21 failed positions in creating a memory space and 128 complete cycles of creation/closure.

The actual boot creates three processes and four user threads:

- Father and adversary begin with different identities. Father raises a child
  by syscall, with reduced integrity, without global privilege and with a single event handle inherited to wait/consult.
- The three processes use `0x40010000`, with `0x1111`, `0x2222` and
  `0x3333` in different frames. The father reads and writes in the son's memory for authorized operations. The son cannot open the father for writing.
- The opponent can't read his father's memory, use his handle number
  or signal an open event only to wait. One of your threads reads a supervisor page and produces real #PF, vector 14, error 5, CPL3. The kernel closes its two threads, including one with no syscalls loop.
- Father and son continue: The father signs the event, awaits the conclusion of the son,
  consults your output code after recovery and ends normally.

The report requires two normal terminations, a suit with a missing process contained, at least six refusals of access, an inheritance, two authorized remote copies and four recognized timer interruptions. At the end there are zero processes, objects, handles and live references. Only the 21 frames of the kernel/ociosidade. space remain The test explicitly awaits the minimum of ticks; do not assume that four interruptions have always occurred before another operation.

Each configuration must pass 40 process bootes, 20 with 16 MiB and 20 with 64 MiB, plus 12 negative external cases: format/meeting/booked/mask invalid report, order, unknown command, IN/OUT wide, replay and no timer with watchdog. Phases 1–4 add up to 160 positive bootes per configuration. Criteria: `config/phase5-acceptance.json`.

## Check result

| Scan | Result |
|---|---|
| Build complete Debug / Release | approved / approved |
| CTest by configuration | 25 approved, zero failed and zero ignored |
| cases/safety core | 693 verifications, native and in each boot |
| Boots positive of cases | 80 (40 by configuration) |
| Phase 1–4 positive regression boots | 320 |
| Cases external negative | 160 (24 of phase 5 and 136 of regression) |
| Examples reliable do project | seven per configuration, 14 approved executions |
| Images of production auditadas | five per configuration without imports/TLS/relocations/W+X |
| Build Debug clean independent | approved, with five image audits |
| Full build artifacts | 54 executables and seven images per configuration |
| Evidence historical preserved | 184 files with unchanged SHA-256 |

In the process matrix, the observed times were 103.089–123.917 ms in Debug and 75.177–103.123 ms in Release; they are measured by this machine, not guaranteed time. The image occupies 524.288 bytes of virtual space in Debug and 499.712 in Release, both within the limit of 1 MiB of the loader. Additional CLI verification ran two more bootes of processes per configuration; they are not added to the 400 bootes of the above matrix.

## Build and execute

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-processes.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-processes.gk --processes --ram-mib 64 --repeat 2 --report .\build-codex\phase5-cli-Release.json
```

`--processes` selects this contract. Combinations with memory contracts or threads are refused before allocating a VM. Previous images continue to be compiled and tested. Transport of the new report uses the EF port and fixed physical address `0x10B000`, with validated size, version, session and order. WHP is used directly, without QEMU and without changing Windows settings.

## Evidence and recovery

Results of this phase use prefix `phase5-`, including the regressions. 184 Historical evidence files keep their hashes. Backup of the previous eleven files changed stays in `snapshots/codex-before-phase5-20261016-201504/`, next to the historical inventory. 14 new implementation files, tests, configuration and documentation.

Evidence ed `build-codex/`:

- `phase5-full-debug-script.log`, `phase5-full-release-script.log` and
  `phase5-*-tests-details.log`: complete builds and individual results.
- `phase5-boots-Debug.json`, `phase5-boots-Release.json`, your `.negative.json`
  and `phase5-regression-phase1-*` regressions to `phase5-regression-phase4-*`.
- `phase5-reference-programs.json`, `phase5-smoke.json` and
  `phase5-cli-Debug.json` / `phase5-cli-Release.json`.
- `phase5-image-audit.json`, `phase5-build-boundaries.json`,
  `phase5-clean-configure.log` and `phase5-clean-debug-build.log`.
- `phase5-change-manifest.json`: hashes from previous/current sources, files
  created, executable, images, evidence and verified matrix. Revalidate without running programs: `python tools/validate_phase5.py --snapshot snapshots/codex-before-phase5-20261016-201504`.

The `build-codex-phase5-clean/` folder contains the independent Debug compilation, with the five output images audited. LNK4281 is the expected warning of the fixed base at 2 MiB, required by the loader. Intermediate attempts have been preserved, including the first execution that did not yet await four ticks and the subsequent inclusion of the stack probe required for the freestanding Debug.

## Limits and Stop

| Resource | Current limit |
|---|---:|
| vCPUs | 1 |
| User processes/spaces including kernel | 3 / 4 |
| Threads by Case | 4 |
| Handles by process, including internals | 32 |
| Objects in the process manager | 64 |
| Arena virtual by Case | 16 MiB |
| Private pages committed in the arena by process | 128 (512 KiB) |
| Additional fixed cost per space | 21 frames |
| Additional groups / DACL entries | 4 / 8 |
| Remote copy size | 4 KiB |

Stacks consume the share of private pages. Scaler objects also have their own inherited limit of phase 4. In the facade of processes, events and times created are automatic; manual events continue in the scheduler and completion objects. This image uses only the RX page of own test programs. It does not yet load arbitrary Windows applications.

This is an internal ABI. It does not implement Windows SIDs, logon, LSA, impersonation, full NT privileges, jobs, SACL audit, graphical sessions or general parity with structures/syscalls NT. Integration of permissions with I/O, files, registry, and network belongs to the following phases. Loader and runtime are the subject of phase 8; expanded compatibility with binary and Windows drivers, of phase 12. The completion of this phase does not mean completion of the antivirus or guarantee that a program will not recognize the environment.

Host MiniNt remains a reference; its shortcuts do not meet the syscalls of the new image. Only own programs and reliable examples of the project have been executed.

At the end of phase 5, stop. Phase 6 only begins after further request.

## Semantic references

- [Microsoft: Access Control Lists](https://learn.microsoft.com/en-us/windows/win32/secauthz/access-control-lists)
- [Microsoft: Access Control](https://learn.microsoft.com/en-us/windows/win32/secauthz/access-control/)
- [Microsoft: Mandatory Integrity Control](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control)
- [Microsoft: access rights to processes](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/ProcThread/process-security-and-access-rights.md)

These references guide the model; the differences and limits of the ABI itself are stated above.
