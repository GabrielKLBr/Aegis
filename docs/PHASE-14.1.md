# Phase 14.1 — Versified contracts and compatibility matrix

Implementation initiated in 23/10/2026, continued validation in 24/10/2026. The scope of this subphase is to define, persist and validate the selection of contracts, with verifiable inventory of dependencies. 14.2 is not initiated by this delivery. Final acceptance results are in the `build-phase14.1/acceptance-Release-accepted/validation.json` and `build-phase14.1/acceptance-Debug/validation.json` files; only `PASS`, without ignored tests, confirms the respective execution.

## Implementation

`shared/guardian/NtCompatibility.h` defines its own contract version 1 and three x64 targets: Windows 10/10.0.19045 (main), Windows 7/6.1.7601 and Windows 11/10.0.22631. They are compatibility targets, not fully implemented Windows systems. XP/View/8/8.1 existing in the hardware inventory are not accepted as execution contracts of this subphase; x86 depends on 14.12.

The manifest records profile, architecture, contract, kernel SHA-256, application and runtime, modules and inventory, corpus policy and domain compliance pendings. Windows binary revisions and versions remain missing because the executable package of this phase contains only the runtime itself; no Windows revision is invented for it. Import and validation of original DLLs continue in the following subphases.

`tools/nt_compatibility.py` produces a new package, with `manifest.json`, `inventory.json` and `binding.gnt` (176 bytes, little endian). Hashes link the bytes used in the session. The chosen runtime needs to be fully embedded in the selected kernel; specify another `user32.dll` does not install or replace it. SHA-256 checks identity/integrity, does not sign or trust the supplier. The package does not include binary or private host paths.

`KernelBootSession::Prepare` copies the descriptor, confers kernel/application, version, profile and consistency with synthetic hardware/environment when present. Posts the contract on `0x1F1000`, reserved boot page, read-only and unexecuted. The caller cannot change the selection by changing the original object after preparation. Reuse the session is refused; changing the profile requires a new session. The contract does not modify the identity or the Windows host DLLs.

Snapshots of the existing CPU path preserve the descriptor in RAM. Restore gives full profile, contract, boot, hashes and permissions; refuses another manifest/runtime and the absence of required configuration. Snapshots of the new PE GUI contract remain explicitly unsupported, as in phase 12; this delivery does not declare resolved the complete recovery of external applications.

The `guardian-gui-pe` own executable accepts `--nt-binding` and `--nt-manifest` together, also checks the SHA-256 of the manifest and saves copies and summary in the session folder. `boot.json` includes `ntCompatibility`; old executions without explicit selection continue with `null`. The C++ direct path can receive test descriptors without JSON; it validates identity and limits, does not parsing JSON manifests. To produce and audit the complete package, use Python tool.

## Version and behaviour queries

The EP itself consults the contract for a new Guardian operation, `0xA1`, without pretending to implement `RtlGetVersion`, `GetVersionEx` or the native Windows ABI. The kernel fully validates the user pointer, size and fields. Without configured manifest, the operation returns `Unsupported`. The tests receive the guest's three builds and observe distinct window titles; the application code continues in CPL3.

The contract contains an explicit manifest-sensitive query model: Win7 saves 6.1.7601; in Win10/11 targets, the modern declaration-free mode uses 6.2.9200, the Win8.1 statement uses 6.3.9600 and the Win10 declaration returns the full target. The mode is a parameter of the test itself. Interpreting the PE manifest and applying this behavior to the actual Windows APIs belongs to the 14.2–14.4. The behavior conditioned to the manifest and the NT numbers are documented by Microsoft in [GetVersionEx](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexa) and [system versions](https://learn.microsoft.com/en-us/windows/win32/sysinfo/operating-system-version).

APIs, API sets, syscalls, layouts/classes, errors, clocks, synchronization, memory, files and graphics have target selection in the manifest, with `pending-native-conformance` status. Their full behavior will still be implemented in batches 14.2–14.12. No native references from exact builds 7601, 19045 and 22631 were collected; local Windows 26300 does not replace these references. There are no host copied syscalls tables nor an approved native compliance claim.

## Inventory and priority

The inventory reuses phase 12 PE parser and adds static imports by name/ordinal, delay imports with RVA, export and ordinal tables, candidate data exports and forwarders. Solves transitive dependencies of the modules provided, identifies cycles, produces order by dependency and priority by family/subfase. API sets are pending from a version validated schema; [the contract is not necessarily the name of a DLL](https://learn.microsoft.com/en-us/windows/win32/apiindex/api-set-loader-operation) file.

Imports PE does not inform whether the symbol will be used as a function or given. Exports classification uses section permissions and is marked as a candidate. Dynamic imports cannot all be inferred statically: optional explicit observations contain `source`, `module`, `symbol` and `evidence`, without declaring full coverage of GetProcAddress. A present export has behaviour **not validated**, even when its name resolves.

The Windows inventory of this delivery covers 12 modules of the 8/12 phases, with 155 imports solved statically. Flappy provided by the user has 178 imports, of which 162 remain unresolved in the current GUI runtime. It was only inspected; it was not executed or promoted to compatible application.

Limits are 128 modules, 32 MiB per file, 128 MiB aggregates and 16,384 imports/exports; forwarders have depth limit. Invalid files, different architecture, overlays, duplicate names, links/reparse points and inconsistent profile combinations are rejected. There is no LoadLibrary or binary execution inspected in the host. Delay imports in VA format, version resource extraction and API resolution sets remain explicitly outside the implemented inventory, with diagnostics instead of manufactured results.

`config/nt-compatibility/corpus-policy.json` fixed before marketing: at least 32 independent programmes, eight categories with four programs each, at least eight programs per architecture x86/x64, target 75% funtionals per profile and 50% by category. Fixtures themselves do not count. Versions, hashes and scripts will be frozen before the campaign; failures do not allow to exchange programs to raise the result. Selection is still incomplete. This policy measures the declared corpus, not the entire ecosystem or malware detection.

## Usage

Run at the project root using the build of this phase:

```powershell
$b = 'C:\Users\gabri\.codex\tmp\aegis-phase14-1-20261023'
python tools/nt_compatibility.py create --profile win10 `
  --kernel "$b\guest\Release\guardian-kernel-pe-gui.gk" `
  --application "$b\guest\fixtures\Release\guardian-gui-application.exe" `
  --runtime "$b\guest\fixtures\Release\user32.dll" --output .\perfil-nt-novo
python tools/nt_compatibility.py verify .\perfil-nt-novo
& "$b\Release\guardian-gui-pe.exe" "$b\guest\Release\guardian-kernel-pe-gui.gk" `
  "$b\guest\fixtures\Release\guardian-gui-application.exe" .\sessao-nova `
  --nt-binding .\perfil-nt-novo\binding.gnt `
  --nt-manifest .\perfil-nt-novo\manifest.json --interactive
```

In the own reference, type capital `G`, click on the window area and close in up to 30 seconds. Use new folders. The own PE contains guest calls: do not start it directly on Windows. For static inventory of another EXE, use `inventory`, the same `--application`/`--runtime` parameters, additional modules in `--module` and `--output arquivo-novo.json`.

## Validation and continuity

The new tests cover SHA-256 with known vector, profiling policies, version/revision/contract/hashes refusal incoherent, synthetic parser with ordinais/data/forwarders/delay imports, dynamic observations, cycles, canonical persistence, tampering, GUI execution invited in all three profiles, CPU restoration and rejection of incompatible snapshots. The CLI integration only executes the PE itself and verifies the refusal of tampered entries before creating the session.

The `tools/validate_phase14_1.py` validator requires the four new tests and the three previous GUI tests, without faults/ignored, and records hashes from sources and binary before/later. The Release matrix includes relevant additional regressions; Debug covers the new contract and GUI reference. Acceptance does not equal new results of the complete scanner nor a new Linux/KVM. acceptance

In the first Release matrix, the four new tests and GUI regressions, snapshot and analysis passed, but runtime/lifetime old people have exceeded the functional period of 10 s. The preserved reports record 12,7–15,7 s at different stages of I/O; lifetime passed the repetition without altering the binaries, while runtime returned to the deadline in another boot. This indicates sensitivity to the host latency, without proving its exact cause. Windows PE functional tests with persistence begin to have budget of 30 s by boot. The product watchdog and the deadlines for specific tests of CPU/time not changed. Initial failures remain in `acceptance-Release` and `first-regression-failures`, with the repetition in `retest-runtime-lifetime.*`; the subsequent acceptance must be identified separately. CPU/watchdog with its original deadlines and the three lots of runtime, but found the same condition in the basic lot PE, still limited to the 3 s despite using persistence. Windows functional budget of 30 s therefore, it applies to the four lots of PE; the result of this second attempt is preserved in `acceptance-Release-final`.

Previous copy of modified files is in `build-phase14.1/baseline-20261023-224422`. Previous local changes of the 12/12F phase have been preserved. By completing this subphase, stop; 14.2 requires new request.
