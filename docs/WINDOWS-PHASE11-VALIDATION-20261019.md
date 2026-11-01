# Windows validation of updates up to phase 11

Data: 19/10/2026. Platform: Windows 11 x64, MSVC 19.44 and WHP real.
Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`.

**Result: the review has not yet passed Windows acceptance.** The 52 tests of each configuration have been performed without ignored tests. The same eight failures appeared in Debug and Release.

| Configuration | Approved | Failures | Ignored | Time ed suite |
| --- | ---: | ---: | ---: | ---: |
| Debug | 44/52 | 8 | 0 | 304,76 s |
| Release | 44/52 | 8 | 0 | 187,50 s |

Total: **104 executions, 88 approved and 16 failures**, corresponding to eight different tests. The fonts remained the same during the two suites. Release compilation was prepared while Debug was running; CTest suites were executed one after the other.

## What happened?

Previous regressions and the full console passed: processes, memory, threads, files/log, heat, TLS, children, exceptions x64, unwind and dynamic DLLs. Each configuration approved the 40 console initializations complete including resource cleaning. Comparisons with Windows executed 153 handlers cases and 59 unwind cases per configuration.

They also passed the network core tests, serialization/relay, rasterizer, windows/entry and native viewer. This does not approve the integrated network paths and graphics within the guest, which failed according to the table below.

## Failed confirmed

| Test | Result in both settings |
| --- | --- |
| `guest_session_snapshot` | `Snapshot check 6`: failure to set pending interruption in the WHP architectural test |
| `vm_catalog` | `Cannot publish transaction file` on the path to atomic file publication |
| `vm_manager` | Paused start fails in `WHvMapGpaRange`, `0xC0370008` code |
| `guest_network_boot` | `Unknown network command` |
| `vm_network` | Worker It ends. with `Unknown network command` |
| `guest_graphics_boot` | `Graphics boot check 1` |
| `graphics_application` | `Unknown graphics command` |
| `vm_graphics` | Paused start fails in `WHvMapGpaRange`, `0xC0370008` code |

The source review found an indication to investigate the network and graphics commands: the WHP monitor scans 32 bits of RAX even in I/O accesses of a byte, while controllers compare the full value with 1 and 2. It is a common cause hypothesis; this round recorded the failures without applying a correction in that path. The publishing failure still needs the `GetLastError` code to identify its cause, and the IRQ/mapping paths need specific WHP diagnosis.

## Adjustments required to compile the tests

The first attempt stopped in the compilation. Two points were corrected:

- Explicit inclusion of `<string>` in `GuestExceptionDispatchTests.cpp`, required for `std::to_string` in MSVC.
- Rename a local port variable in `VmMain.cpp`, eliminating the hidden warning treated as error.

The originals are in `snapshots/codex-before-windows-phase11-tests-20261019`, with patch of the two changes. After these adjustments, all the executables and the images of the suite were built on a new tree, and the audits of the images passed in Debug and Release.

The 331 entries of the Linux validation sources manifest initially coincided with this folder. The two build adjustments change the hashes of these files; previous reports and Linux packages were preserved. Joint acceptance was not marked as complete. Phase 12 was not started.

## Evidence

The folder is `build-phase11/validation-20261019-115314-1d807fdc/`:

- [Summary with hashes, results and complete errors](../build-phase11/validation-20261019-115314-1d807fdc/windows-tests-summary.json).
- [JUnit Debug](../build-phase11/validation-20261019-115314-1d807fdc/host/tests-Debug.xml) and [log Debug](../build-phase11/validation-20261019-115314-1d807fdc/Debug-ctest.log).
- [JUnit Release](../build-phase11/validation-20261019-115314-1d807fdc/host/tests-Release.xml) and [log Release](../build-phase11/validation-20261019-115314-1d807fdc/Release-ctest.log).
- [Official verifier register](../build-phase11/validation-20261019-115314-1d807fdc/validation.json): it interrupts the sequence after Debug failed. Release was completed separately, with the same sources and criteria.
- [First attempt, interrupted on compilation](../build-phase11/validation-20261019-114948-3d2798a1/validation.json).

The main command was `python tools/validate_phase11.py`. The continuation Release used the same tree `host`, compilation/audit and `ctest -C Release --output-on-failure --output-junit tests-Release.xml`.
