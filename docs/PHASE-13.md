# Phase 13 — telemetry, policies and behavioral analysis

**23/10/2026 Windows Update:**The current matrix has passed on MSVC/WHP: 80 distinct tests Debug, with documented retests, and 80/80 Release in a complete suite, without ignoring. [Corrections and Evidence](WINDOWS-PHASES12B-13-REVIEW-20261023.md). The Linux records below are historical; the scope and comparison pending versions remain open.

**State in 22/10/2026:**implementation completed and accepted in the native Linux/KVM matrix: **59 Debug, 59 Release and 35 ASan/UBSan**, flawless or ignored. The product remains Windows/MSVC/WHP; was not used Wine or QEMU. The native Windows acceptance of this review and previous pendings remain open. The result keeps `phaseComplete: false` and `prerequisitesComplete: false` for these requirements. Phase 14 has not been started.

The previous base is preserved in `build-phase13/baseline-20261022/`, with font file, SHA-256 inventory and byte verification. Code and records stay in the original HD folder; builds stay out of NTFS.

The scope is to record actual operations and blocked guest attempts, with authorship and motive, versioned correlation, allow/deny/simulate policy, JSON/HTML reports and state differences. Limits and event loss should be explicit. Events received from VM will be validated as unreliable data.

## Implementation and border

The `GuardianTelemetry/1` protocol carries limited events, with version, boot, sequence, operation and correlation between intention and result. There is PID and thread of the observed process, in addition to the Case/thread caller. This allows distinguishing the closure of a child requested by the parent process. Records include decision, motive, status and, for volume, sizes and hashes FNV-64 previous/later; do not copy the contents of the files.

The host validates envelopes, enums, limits, finished ASCII text, sequence and identity of the pending operation. Texts are not commands, paths received from the VM are not opened in the host and HTML escapes your data without JavaScript or external resources. Events are statements received from the guest kernel; format validation does not prove its veracity if this kernel is compromised. CPL3 programs continue subject to permissions and validation of syscalls.

The new kernel gathers in the same timeline the nine domains: processes, threads, modules, files, record, services, drivers, network and memory. The own UDP echo is requested by syscal application, with real PID/thread, and uses the virtual IP device. It works offline (refusal) or simulated mode (checked internal); it does not receive arbitrary addresses and does not open external connections. Real guest I/O drivers appear in the same stream. The imported PE driver keeps its boot contract separate and has also been connected to the collection protocol, without declaring that it performs within the process profile.

The collection of the application starts after the preparation of the synthetic base and initial internal tests. The preparer restores only its three own documents and removes its previous `.locked` versions; this is an explicit test base, regardless of the host data. The full coverage indicates the instrumental operations of this interval, without stating coverage of any system instruction or API.

The new `guardian-kernel-analysis.gk` kernel runs its own application in CPL3, with synthetic reference files, and checks the actual cleaning of resources at the end. It has its own completion criteria; it does not fill the results of the old CPL3 probes. These probes continue in the `complete` and `processes` kernels, validated by complete regression. The process counter, threads, handles and objects should return to zero, without loss of pages.

The `GuardianLabService/1` service is a real child, with state, authorship, query, start and stop; it is the place for the life of the creative process and its executable belongs to the own catalog. It does not represent the SCM of Windows nor promises to perform third-party services. The `HKCU\Lab\Run` configuration writing is observed as boot configuration; it is not inferred that a non-existent Windows author has been executed.

## Policy and rules

The v1 policy applies to operations that change status. `deny` prevents operation before effect and returns error; readings and resource cleaning remain possible. `simulate` files/log performs effects on VM's private synthetic volume: later reading, dates, permissions, clone, and snapshot reflect the state produced. It does not automatically discard changes; use `--disposable` VM for this cycle. Simulation in other domains is refused with explicit motive. There is no new operation that executes commands or effects on the host.

The v1 rules record numbered evidence and reasons:

- Written startup configuration: suspicious; does not prove subsequent execution.
- Own service created and initiated: confirmed behaviour, linked to it
  service/Case; this does not identify malware.
- Three distinct rewritten and renamed files to `.locked` by the same
  process in up to ten seconds: confirmed pattern. Repeating one path, mixing processes or exceeding the window does not meet the rule.
- recordable/executable memory request: suspect, with real result.
  The current runtime refuses this protection; refusal is not described as success.
- Blockage by politics: suspicious attempt, preserving motive and error.
- Complete collection without pattern: legitimate in the observed subset, without attesting
  general security. Failure, collection still ongoing, rejected entry or loss of events: inconclusive coverage.

The default limit is 4,096 events, configurable between 16 and 8,192. Upon reaching the limit, the loss is counted and new mutations are rejected; the report becomes inconclusive. The snapshot keeps records, sequence, pending operations and policy. Restore with incompatible policy/identity is refused. The limit reserved for snapshot metadata goes from 1 to 5 MiB to conduct maximum collection, preserving the other limits. There are up to 64 history bootes by VM, under the existing storage quota; recording failure makes the VM fail.

## Usage

```text
guardian-vm --root CATALOGO create amostra guardian-kernel-analysis.gk storage
guardian-vm --root CATALOGO analysis-policy amostra
guardian-vm --root CATALOGO network amostra simulated
guardian-vm --root CATALOGO analysis-policy amostra file deny
guardian-vm --root CATALOGO start amostra
guardian-vm --root CATALOGO status amostra
guardian-vm --root CATALOGO analysis amostra
guardian-vm --root CATALOGO analysis-export amostra DIRETORIO
```

Exchange `deny` with `allow` or `simulate`. The policy only changes with the VM stop; old checkspoints preserve your policy and cannot be resumed under a different configuration. For your own reference application, add `--scenario benign` or `--scenario patterns` to the configuration. `--event-limit N` allows you to test saturation. Reports are JSON, HTML and state differences, with `analysis-history/` history within the VM.

## Validation and delivery

The only acceptance of this review is `acceptance-20261022-full-03`:

| Configuration | CTests approved | Failures / ignored |
|---|---:|---:|
| Debug | 59 | 0 / 0 |
| Release | 59 | 0 / 0 |
| ASan/UBSan | 35 | 0 / 0 |
| Total | 153 | 0 / 0 |

The core has passed in 1233 Debug, 1233 Release and 1233 sanitizer checks. Includes unreliable entries, escaped HTML, loss/capacity, negative correlation by file/Case/time, restoration with identity and policy, 8.192 serialized events, real volume policy and metadata of registration values with names in uppercase/lowercase.

The boots themselves passed in 864 Debug and 864 Release checks. They exercise to allow, deny, simulate, legitimate use and saturation; compare the actual contents of the files, resume snapshot, refuse incompatible policy, confer the nine domains in the same collection, simulated network and PE driver in the separate contract. Baseline kernels without collector refuse the policy configuration.

Independent manager/oracle has passed in 1038 Debug and 1041 Release checks, including real VMs, correlation from events, export, report persistence, clone, rollback, visible loss and intact personal control file. The counts include progress queries and may vary with the duration of VMs. The personal canary has stayed out of RAM and invited data on the exercised channels; it is not general proof that any possible implementation is free of all leaks.

The 418 source files remained the same during the matrix. `source-inputs.json`'s SHA-256 is `fd9432b0d4413788289b53ea05a5060ac13bd77de2b8710e2465c9e19203af60`. The final syntax crosscheck passed 14 Windows drives over the same code. It does not run `.exe`, does not use MSVC/WHP and does not replace native Windows acceptance.

The old snapshot, manager, compliance and sessions tests continue in the matrix; their criteria and timeouts have been preserved. The directed repetition of the five regressions was approved before the final complete execution.

In the original HD folder:

- Evidence selected: `build-phase13/acceptance-20261022-full-03/`.
- Windows Review and Check: `build-phase13/review/`.
- Source: `build-phase13/delivery-20261022/GuardianAV-13-fontes-20261022.zip`.
- Linux: `build-phase13/delivery-20261022/GuardianAV-13-linux-20261022.tar.gz`,
  with 12 ELF tools, 21 kernels, four own programs such as data and JSON/HTML report example of the Release run accepted.
- Integrity and delivery test: `SHA256SUMS.txt`, `delivery.json` and
  `package-verification.json` in the delivery directory. Additional verification extracts the package, executes the delivered core and VM, checks simulation, nine domains and real export.

## Corrections found in validation

The first matrix was replaced during Debug, before acceptance: a marker announced collection in kernels that did not start the collector. The marker came to exist only in the paths that initialize it. Partial results were not counted as acceptance.

The `full-02` matrix ended with five Debug regressions that had the same cause: Invalid text audit anticipated denied access, changing the original error of a process creation with uncompleted command. The collector started marking the invalid label and preserving the API validation; only a configured policy can deny in advance. The case also runs on CPL3 in its own application. The default execution has been preserved and does not accept this revision.

The directed test also reached the old limit of 10,000 outputs from the hypervisor. Profiles with collectors pass 100,000 per normal stretch, maintaining the watchdog time and the limit of events; the others retain the previous limit. There was no reduction in the tests or magnification of their timeouts to obtain approval.

The hashes and sizes of the record were also verified using the same normalization criteria for volume names, preserving the coherence between the event and the real state.

## Reproduction

```text
python tools/validate_phase13.py --output DIRETORIO_NOVO --llvm LLVM_NATIVO --openssl-root OPENSSL_NATIVO
```

On Windows, run on a Visual Studio x64 terminal without Linux paths. The validator keeps the previous tests and to-ed. Policies that prevent loading or operations required by the program may prevent its completion; this failure and inconclusive coverage are explicit.

During execution, the coverage remains in course/inconclusiva. For the final result, wait for the VM stop and `finished: true`; `complete: false` continues to indicate loss, rejection or failure, even after the end.

This delivery ends the implementation and Linux acceptance of phase 13, keeping the Windows and previous requirements open. Phase 14 depends on new request.
