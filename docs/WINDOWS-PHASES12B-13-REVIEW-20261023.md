# Windows revision of phases 12B to 13 — 23/10/2026

**State:**Native matrix Windows/MSVC/WHP validated: **80 distinct tests in Debug**, with the retests described below, and **80/80 in a complete suite Release**, without ignoring. No later phase has started. Scope and comparison pending versions remain open.

The sources of the continuation of 12B, 12C, 12D, 12E and 13 were reviewed in the `D:\Desktop Folders (DO NOT ERASE)\AntiVirus` folder. The Git base is `63f02b7`; the file of the sources of the Git database prior to corrections is in `build-windows-review-20261022/baseline-sources.zip`. The environment uses Windows build `10.0.26300`, MSVC `19.44.35228` and native WHP.

## Fixes

- Link and include: the two unwind tests receive `guardian_abi` and its
  Shared headers. The storage host test includes `Files.cpp`, required by the new aggregate usage calculation.
- Compilation MSVC: local names that hide variables were corrected in
  compliance program and in the creation of services. The analysis kernel excludes from the compilation the old probes that stayed after their return.
- The textual errors of the resource session are converted to UTF-8 in
  Windows, without reducing broad characters to bytes.
- Tests include your required headers, use byte values with
  explicit type and copy your literal texts with size check. `/W4 /WX` checks have been preserved.
- Global resource control awaits up to five seconds for the transaction
  The new test reproduced before the `Cannot acquire VM lease` correction; then passed, without changing the consulted configuration. The exclusive leases of the VMs remain exclusive.
- The sparse disk test dynamically allocates your work buffers
  64 KiB. Windows confirmed the previous burst with the exception `0xc00000fd` in the 1000 application event. Retest saves content checks, persistence, clone, discard, quota and property.
- The PE functional test of runtime/cycle of life uses ten seconds per boot in
  Windows, like the full PE test. The old three-second timeframe canceled a boot during the I/O sequence after the PE program concluded. The other 16 bootes of this test had a median of approximately 1.4 seconds. The adjustment is restricted to this functional test with durable recordings in the host; the specific clock, watchdog and lock checks were maintained. 40 bootes remain per contract, in both RAM quantities and with the same content, persistence, memory and closure criteria.

## Evidence and method

| Implementation | Result |
|---|---|
| Debug, first suite complete | 78/80; PE timeout and stack burst preserved |
| Debug, five tests affected by the final adjustments | 5/5 |
| Debug, coverage consolidated | 80 different approved; zero ignored |
| Release, suite complete | 80/80; zero failed and zero ignored |

The Debug coverage brings together 75 results from the first execution and five retests. It does not represent a single full suite without faults. Between the first execution and the retest changed only `test/GuestPeBootTests.cpp` and `test/SparseDiskTests.cpp`; the five tests dependent on these files were re-executed. The product sources remained identical in this interval.

The records are in `build-windows-review-20261022/`:

- `attempt-01/`: First clean compilation and its original errors.
- `admission-before.log` and `admission-before.xml`: reproduction of competition.
- `stack-overflow-event.xml`: diagnosis original do Windows.
- `debug-01/`: initial full suite, XML and entrance hashes.
- `debug-retest-03/`: five retests and contract reports.
- `release-01/`: clean compilation Release, suite and entrance hashes.
- `windows-debug-consolidated.xml`: Debug results with the origin of each test.
- `validation.json`: consolidated count, stable hashes and limits of acceptance.
- `artifact-hashes.json`, `source-inputs-final.json` and `changes.patch`: artifacts,
  compiled entries and differences from already versioned files.
- `process-cleanup.json`: no remaining test worker.

Debug was run on D drive. Compilation and Release test data use `C:\Users\gabri\.codex\tmp\aegis-windows-review-20261022-release`; fonts and records remain in the original project folder.

## Coverage observed in both configurations

- 12B: 128 MiB real RAM, checkpoints, quotas, sparse disk, migration with
  accentuated paths and recovery after forcibly closing a worker. The kernel itself `resources` passed with one to four CPUs and restoration.
- 12C: 15 executions, with 795 observations of contract; reference catch
  native ed build `10.0.26300`.
- 12D: 438 Debug and 395 Release checks in the session cycle,
  including more than 31 seconds of accumulated execution, suspension, resumption and automated interaction.
- 12E: 12 profiles with restoration and refusal of changed policy; six pairs of
  scenarios in the manager, with 613 Debug checks and 616 Release of persistence and privacy.
- 13: 1,233 core checks and 864 boot checks in each configuration;
  in the management cycle were 1,192 Debug and 1,175 Release. The coverage includes nine domains in the timeline, allow/deny/simulate policies, export, event loss and restoration with linked policy.

Manager counts include status queries and may vary with execution progress. The 80 named tests and their criteria have been preserved.

## Preserved Pending

The installed build reference does not replace the exact references of `6.1.7601`, `10.0.19045` and `10.0.22631`, still missing in 12C. The comparator informs `referenceCompared: false`; an empty list of differences in this state does not close the comparison between versions.

Integration of SMP and scalable volume to previous 12B PE profiles remains open. General compatibility with Windows programs and drivers remains limited to documented contracts. These tests do not declare complete completion of the stages or undetectable sandbox.

Linux has not been rerun after these Windows fixes; previous Linux results remain as historical records of their revisions.
