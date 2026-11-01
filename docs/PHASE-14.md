# Phase 14 — Scanner, rules and threat intelligence

Update of the queue in 29/10/2026: a [14.13](PHASE-14.13.md) replaces the legacy forwarding described in this historical delivery by packages inscribed in the runtime itself. The lack of package is inconclusive, without fallback.

23/10/2026. Delivery Windows implementation and acceptance completed. Linux validation of this revision is partial. This step evolves existing scanners. Phase 15 has not been started.

## Result and scope

`guardian-scan` uses the same engine as `av scan`, `av clean`, the monitor and Windows scan screen. On Linux, `guardianav scan` also forwards to the shared engine. The interface shows suspicious and inconclusive analyses; cancel, fail to open a folder or choose an individual file produces real status.

The results have four classifications:

| Result | Meaning |
| --- | --- |
| `detected` | Local signature, harmless test marker or YARA rule explicitly configured as detection. |
| `suspicious` | Heuristics or external provider report; requires evaluation. Do not initiate automatic quarantine. |
| `unknown` | No matches within declared range. ** Does not mean secure file.** |
| `inconclusive` | Unable to complete analysis. Error, cancellation, limit or format without support is visible. |

`complete` is independent of classification. A file may have a known detection and also contain unanalyzed parts. SHA-256 identifies the content; SHA-1 and MD5 are auxiliary and do not authorize a file on their own.

The engine reads a limited copy and verifies stability/size. The worker receives only this copy and checks its SHA-256. Rules, parsers and decompressing have a time frame and memory limit in another process. In Windows, Job Object closes the process tree by canceling or breaking the deadline. This parsing worker has the user's identity: the program execution isolation is done by VM.

## Formats and rules

- Local `sha256`/`substr` subscriptions, with strict validation of the base, plus
  The search runs through all the content accepted, including after the old prefix of 4 MiB.
- Real YARA, compiled from fixed fonts. JSON packages contain source,
  license and notice identifier. MIT, BSD-3-Claude, Apache-2.0 and CC0-1.0 are accepted; absent/unknown license, compilation error and external `include` produce inconclusive analysis. The available modules are `pe`, `math`, `time` and `string`; other unsupported imports fail explicitly. The declaration does not replace the human revision of origin/license of the imported rules. There is no binary rule loading.
- PE: headers and ranges, architecture, sections, entropy, imported DLLs and
  recordable/executable. section alert No Authenticode signature nor does it determine the legitimacy of an installer. VM PE compatibility remains subject to documented limits in previous phases.
- Scripts are inspected as text, including the simple UTF-16LE case.
  dynamic execution combination with encoding/download is a heuristic; no script is evaluated in the host.
- ZIP, TAR, GZIP/TGZ and CAB in the configured subset; MSIX/APPX and its bundles
  each member is analyzed in memory, without extracting paths in the host. Links, crossing/absolute paths, duplicates, encryption, corruption and unavailable compression are refused.
- MSI/MSP, RAR/7z/XZ/BZ2/ISO and recognized NSIS/Inno payloads remain
  There is no promise to unpack any installer or to detect every packer.

Standards: file/member of up to 32 MiB, total expansion of 64 MiB, up to 256 members, depth 3 and static analysis of 5 seconds. There is 1 additional second for start/close the worker. The process has 384 MiB; analysis in MiniNT allows the individual WHP auxiliary, with a total budget of 512 MiB. The expansion is counted again at each level, avoiding bypassing the budget with nested files. Larger limits are validated, not unlimited. The CLI limits each report to 10,000 entries or 32 MiB of results and informs truncation; the enumeration has maximum depth 64.

## Settings and usage

Offline example: [scanner.example.json](../config/scanner.example.json). Own and harmless YARA package: [scanner-rules.example.json](../config/scanner-rules.example.json). To use it, add `rulePackage` with your path to the configuration file. Relative paths are resolved from the configuration folder.

```powershell
.\guardian-scan.exe scan "C:\pasta de teste" --config scanner.json --json
.\guardian-scan.exe queue --config scanner.json
.\guardian-scan.exe sandbox-next --config scanner.json
```

The configuration file is optional. The personal configuration, when it exists, is in `%LOCALAPPDATA%\Aegis\scanner.json` on Windows or `$XDG_STATE_HOME/aegis/scanner.json` on Linux (fallback: `~/.local/state/aegis/scanner.json`). Windows frontends also read it. `state` can choose a dedicated folder for cache/queue; by default is next to the personal configuration of Aegis, in `scanner-v1`. Unempty folder without scanner marker is refused. Windows uses ACL for user/SYSTEM; Linux uses permission 0700.

`guardian-scan` outputs: 0 without correspondence/unknown; 1 detection/suspect; 2 invalid usage or configuration; 3 incomplete analysis. The legacy Windows console preserves 2 for threat/suspect and uses 3 for error/inconclusivo. Do not interpret output 0 as a security certificate.

## SQLite, cache and queue

SQLite persists results, validity, quotas and backoff. Static cache includes SHA-256, name/extension, engine sources, platform/compiler/configuration, dependency lock, rules, signatures and limits; changed files and policy changes do not reuse the opinion. Only complete static analysis is stored. External errors have not seen negative cache. There are TTL, up to 4,096 records and bank page limit.

`queueUnknown: true` keeps an immutable copy by SHA-256 and a deduplicated entry for the sandbox. `sandbox-next` claims a transaction task, scans the copy and calls the existing **MiniNT/WHP** in CPL3, without executing the file as Windows process. The guest changes its virtual files/log system. The report identifies `MiniNT-WHP-legacy`, missing imports, crashes and events. PEB/line command uses `C:\Samples\sample.exe`, without exposing the private host path. The string is limited to the buffer of the process parameters; a PE itself compares all characters within the VM and fails to receive another path. Unwarned execution continues `unknown`, never “safe”. The queue is local and manual; it does not initiates dynamic execution automatically.

The legacy loader has received limits for file size, headers, sections, directories, blocks/targets relocation, names, thunks and written in IAT. Arithmetic uses unoverflowed intervals; invalid tables do not reach execution. TLS, delay-imports and managed startup are explicitly refused. Tests include IAT/ILT and off-image relocations, as well as a program that confers a valid DIR64 relocation during execution in the guest.

The adapter accepts PEs compatible with the limited imports of this backend. It does not equal running full Windows applications in the recent kernel, nor does it replace the nine-domain pipeline of phase 13. On Linux, this external forwarding is explicitly unavailable; there is no fallback for running on the host.

The queue has up to 128 retained tasks and 512 MiB of copies. When reaching the limit, the new analysis informs the error. Aborted tasks have ease and go to inconclusive; they are not marked as completed silently. There is no automatic exclusion of history/copies at this stage. Temporary directories have limit and cleaning of fixed names created by the job itself.

## External intelligence

Offline and all disabled providers are the defaults. To view, configure `offline: false`, enable the provider and recognize its conditions in `termsAccepted`. Keys come from `keyEnv`, never from the report or SQLite. The code uses only HTTPS fixed endpoints, without redirecting and with certificate validation, limited response and hash conference returned. There is no upload endpoint, malware download or auto-supply submission.

| Provider | Operation | Treatment |
| --- | --- | --- |
| VirusTotal | GET `/api/v3/files/{sha256}`, `x-apikey` | Reported statistics indicate suspicious; absence/zero does not release file. Requires Premium license suitable for using this product/hash-only. connector |
| MalwareBazaar | POST `query=get_info&hash=...`, `Auth-Key` | Hash reported indicates suspect; `hash_not_found` is unknown. |
| CIRCL hashlookup | GET `/lookup/sha256/{sha256}` | Presence on the basis of known files is information, not whitelist. |

minute/day quotas are persistent and transactional. 429/503 activate backoff; there is no automatic replay of requests. Cache has configurable TTL. Authentication failed, invalid JSON, divergent hash and unavailability are displayed in the `intelligence` field without replacing local evidence. Offline can use still valid reputation cache without calling the network.

Windows uses WinHTTP. Linux uses native libcurl >=7.85 when development files and pkg-config are available; without them, compile offline and informs unavailability if a query is requested. It does not use Wine.

References verified in 23/10/2026: [VirusTotal — archive](https://docs.virustotal.com/reference/file-info), [Public/Premium](https://docs.virustotal.com/reference/public-vs-premium-api), [MalwareBazaar](https://bazaar.abuse.ch/api/), [CIRCL](https://www.circl.lu/services/hashlookup/), [YARA C API](https://yara.readthedocs.io/en/latest/capi.html). Trade conditions must be confirmed by the operator for the chosen use.

## Compilation and dependencies

The optional `tools/validate_phase14.py` utility records JUnit, output codes, source code/binary hashes and refuses changes during the test (Python >=3.11).

CMake gets **sources**, confers SHA-256, and compiles YARA 4.5.8, libarchive 3.8.10, zlib 1.3.2, SQLite 3.53.4 and nlohmann/json 3.12.0. URLs, versions, licenses and hashes are in [ScannerDependencies.json](../cmake/ScannerDependencies.json); warnings follow the installation on `share/aegis/licenses`. No executables or samples are downloaded. A previously verified font cache can be reported in `GUARDIAN_SCANNER_DEPS`; this override presupposes a reliable local tree.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Keep `guardian-scan-worker` next to `guardian-scan`, `av` and `avui`. In Windows the referral to VM also needs `guardian-whp-memory.exe`. `cmake --install` includes the scanner executables, the auxiliary, the base and warnings.

## Acceptance

Matrix of the final review, with sources assigned before/later of the tests:

| Environment | Scope | Result |
| --- | --- | --- |
| Windows 11 10.0.26300, MSVC 19.44, Debug | `scanner_core`, `scanner_pipeline`, `scanner_facade` | 3/3, without ignored |
| Windows, Release | Full suite of the project, including the three new tests | 83/83, without ignored (470,37 s) |
| Ubuntu 24.04 / WSL2, GCC 13.3, Debug | Motor/worker, pipeline and CLI Linux | 2/2 + 1/1, without ignored |

The Windows test only executes own PEs on MiniNT: normal output, virtual path verification on the PEB, valid relocation and refusal of malformed tables. The guest in loop is closed within the budget. The host canary remains unchanged. The file tests cover signatures/hashes, YARA, content beyond 4 MiB, scripts, nested files, links, crossing, corruption and limits. SQLite is exercised with TTL, persistence, competition, quotas and backoff. The Windows facade is tested with individual file, invalid reading, detection and cancellation. Tests of providers use simulated transport; **Real queries with credentials have not been performed**.

Linux validation is partial: scanner/CLI natives without Wine; there was no external PE execution in KVM nor revalidation of the entire Linux/Release/sanitizers. suite The available environment had no development libcurl; the exercised Linux delivery was offline. The acceptance of external MiniNT is Windows/WHP.

Local evidence: `build-phase14/acceptance-Debug-hardened/`, `build-phase14/acceptance-Release-hardened/`, `build-phase14/acceptance-Linux-hardened/` and `build-phase14/linux-scanner-hardened.xml`. Each acceptance includes real codes, JUnit, hashes of 441 relevant source entries and executables. Previous Windows 12B–13 revision settings have been preserved; there is snapshot of the previous base in `build-phase14/baseline-verified-20261023.zip`.

The `tools/validate_phase14.py` utility allows you to repeat the scope of the scanner; with `--all`, you run the full build suite. Example:

```powershell
python tools/validate_phase14.py --build build --config Release --all --output build-phase14/nova-aceitacao
```

Local delivery (outside Git): `dist/aegis-phase14-windows-x64-20261023/`, with `avui.exe`, `av.exe`, `guardian-scan.exe`, worker, helper WHP, configuration, license notices, documentation and evidence. There is ZIP and SHA-256 next to the folder. `manifest.json` records the accepted files and sources. Delivery is checked with an offline scan of a harmless document using copied executables.

In this delivery, open `avui.exe` for the interface or refer to `LEIA-ME.txt` for CLI. Modern CLI issues JSON; personal configuration is not changed by copy. Phase 15 remains unstarted.

No test requires real malware, API key, BIOS/NX change or host driver. The distributed base is demonstrative; this phase does not establish threat coverage of a commercial product or full Windows compatibility. The PE/SMP to-ed and NT references specific to the previous phases remain open, as the 23/10/2026. Windows review
