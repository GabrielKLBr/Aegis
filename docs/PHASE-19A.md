# Phase 19A — Isolation and permissions review

This delivery is a bounded engineering review and hardening pass over the MiniNT/service/native-helper boundaries. It is not a release security certification, an independent audit, or completion of phase 19. No real malware is used in its acceptance tests.

## Changes

- Engine frames and caller-controlled JSON files now have structural quotas in addition to byte quotas: 32 parser levels and 65,536 parser events. Duplicate fields, including equivalent escaped names, are rejected. The same reader protects protection requests, persisted protection records, scanner records and reputation responses. Frames must contain objects. Privileged requests remain limited to 64 KiB and engine frames to 2 MiB.
- Service workers, scanner children and native GUI/audio helpers start with remote-image loading disabled, low-label image loading disabled, System32 preferred and legacy extension points disabled. Runtime DLL searches exclude the working directory. A failed policy setup aborts the operation.
- Leaf antivirus workers, static scanner children and graphical helpers seal their child-creation policy before processing file contents or guest commands. Supervisors retain children because MiniNT needs its RAM owner and native helpers. The GUI protocol handshake verifies the sealed policy without changing its existing wire format.
- Leaf antivirus and static-parser jobs restrict foreign USER handles, clipboard operations, desktop switching, global atoms, display/system settings and exit operations. Supervisors keep their existing process/memory quotas and cancellation controls. Leaf UI limits are not applied to coordinating jobs that need nested jobs.
- The private-worker connection deadline is capped by the request's remaining budget. Scanner failure diagnostics include the child exit code.
- Engine updates refresh the retained runtime's host-helper hash links in the new inactive slot. Every old member and binding is verified first; only the two fixed helper roles from the verified publisher package are eligible. The helper, manifest, NT binding and catalog hashes change together. Guest DLLs, kernel and volume stay unchanged; the prior slot remains available for rollback. Existing checkpoints remain tied to their original controller/runtime identity and require their matching version.

Catalog migration accepts at most 16 applications, 64 MiB per retained member, 512 MiB total and 2 MiB of JSON metadata per file. Larger or invalid retained metadata is refused before replacing live services. Its live repair/rollback check uses two slots of the same signed 19A package; it does not repeat every historical cross-version update scenario.

Windows build 26300 rejected helper DLL initialization when the token child-creation attribute was applied before startup. The final implementation seals the process mitigation immediately after trusted DLL initialization. GUI helpers do this before reading any guest command; antivirus leaves do it before their identity handshake; scanner leaves do it after reading the bounded, coordinator-generated job descriptor and before reading the payload. There is no retry that omits the required seal. This assumes the executable and startup dependencies are trusted; the installed engine slot is administrator protected and the portable launcher verifies its signed tree.

## Reviewed boundaries

| Boundary | Review and acceptance |
|---|---|
| User client → LocalSystem service | Local pipe ACLs, medium-integrity write requirement, rejection of remote clients, owner SID/session authentication and original elevation checks. Public callers cannot request internal worker operations. Malformed headers, paths and privileged JSON requests are refused. |
| LocalSystem → user worker | Filtered caller token, removed administrative privileges, supervisor-owned process/thread ACLs, filtered environment, no inherited handles, suspended creation, job assignment and cancellation. Live normal-user injection/handle-duplication attempts are denied. |
| Worker → LocalSystem | Random private pipe name, exact spawned PID check, typed bounded frames and structural JSON limits. Worker output never supplies an arbitrary privileged filesystem target; quarantine retains the server-selected path and verifies its digest under the caller's access context. |
| MiniNT → native graphics/audio | Typed versioned commands, opaque session/process-owned resource IDs, bounded copied data, two explicit pipe handles, low integrity, restricted token, one-process job and child-creation seal. No generic DLL/function/message invocation is added. |
| Guest memory and files | WHP guest mappings and the existing separate RAM owner remain in use. Guest file calls use the guest volume/namespace. An SDK-linked fixture cannot see an inert host sentinel at the same Windows path; its guest write leaves the host sentinel unchanged. |
| Guest network | The unified runtime's default internet policy remains offline. Its current automatic catalog refuses the socket fixture's unavailable Winsock imports before execution; an owned loopback listener receives zero connections. This is a refusal test, not end-to-end socket compatibility. The directly exercised internet backend unit suite separately verifies its offline policy. Reputation lookup is a separate host operation and remains opt-in. |
| Privileged files and maintenance | A user-worker scan cannot read an administrator-only test file. Service installation preserves running/startup states and protection policy. The existing Release/Debug installation fence is preserved. |

The review inspected `services/Server.cpp`, `Worker.cpp`, `Protection.cpp`, the quarantine handoff, scanner child launches, `runtime/Unified.cpp`, native GUI command/resource validation, network policy validation and the WHP RAM-owner launch/mapping path. It does not claim exhaustive review of every kernel syscall, DLL export, driver, media codec or Windows version.

## Validation

Machine-readable results and exact scope are in [PHASE-19A-RESULTS.json](PHASE-19A-RESULTS.json). Only this delivery and directly exercised dependencies are tested; historical phase suites are not replayed.

The repeatable tests are:

- `engine_services_tests`: protocol, structural JSON quotas, duplicate fields, partial transfers, cancellation and terminal replies.
- `isolation_boundary_tests OUTPUT USER32_HELPER`: inert low-integrity negative-permission probe, explicit handle inheritance, child-creation denial with a job that otherwise allows two processes, real-helper policy handshake/cleanup and the privileged file reader's depth limit. The probe also deliberately records that ordinary user files remain readable.
- `native_gui_broker_tests HELPER` and `audio_broker_tests HELPER`: the directly changed launch/handshake path, native rendering/audio, session/resource ownership, helper failure and cleanup.
- Reputation unit tests: changed bounded provider/cache reader, using synthetic responses and an isolated inert credential target; no online provider query.
- `runtime_catalog_tests OUTPUT`: eight helper-update, integrity, namespace, binding, idempotence and retained-slot checks, using inert bytes rather than executable sample code.
- `IsolationServicesTests.py`: already installed services, normal-user hostile requests, harmless positive detection, worker process permissions, offline hashing, protected test file and the SDK-linked guest filesystem/network fixture. The test requires administrator-prepared inert sentinels; it never installs services or requests elevation itself.
- Signed portable-tree verification and a copied portable app startup, in both configurations. The installation stays Release; the Debug client exercises that Release service. A refused Debug-over-Release installation is a successful safety check, not a Debug service deployment.

## Open findings and release gates

| ID | Status | Required follow-up |
|---|---|---|
| 19A-01 | Fixed | Byte-only JSON limits could admit very deep or allocation-heavy trees in a privileged broker. Enforce structural quotas and reject ambiguous duplicate fields. |
| 19A-02 | Fixed | Auxiliary launch paths lacked explicit image/extension-point policy and a verified leaf child-creation seal. Enforce and test these policies. |
| 19A-03 | Fixed | Non-graphical leaves lacked the job's desktop restrictions. Apply them at leaves while preserving coordinating/nested jobs. |
| 19A-04 | Fixed | Updating a signed host helper left retained runtime enrollment pinned to the old helper. Refresh the hash chain only in the new installation slot after validating the retained data; do not weaken the runtime's helper verification. |
| 19A-R01 | Open — release gate | Filtered/restricted user tokens and jobs do not provide complete filesystem, registry, credential or network confinement if a host helper is compromised. The low-integrity probe can read an ordinary user-owned test file. Service coordinators deliberately retain user access and child creation. Design role-specific AppContainer or equivalent capability confinement and broker only the required files/network/GUI operations; validate native graphics, audio, WHP and credential compatibility before relying on it. |
| 19A-R02 | Open — release gate | No independent specialist audit, sustained parser/protocol fuzz campaign, second-user login test or clean-machine/Windows-version matrix is performed here. Complete these before production security claims. |
| 19A-C01 | Open — compatibility | The automatic runtime templates do not satisfy the SDK-linked Winsock fixture's imports. It is refused before execution. Completing those dependencies and then repeating the guest-to-transport offline test belongs to a separate compatibility delivery. |
| 18-R | Open | The clean Windows/no-WHP machine matrix, CA-backed Authenticode and production publisher-key operations remain pending from phase 18. |

Production signing must use an identity unavailable to analysis workers. Removing privileges or making a key non-exportable is not a substitute for separating the signing role from untrusted-input processing.

Do not interpret a passing inert corpus as proof that an arbitrary hostile guest or a compromised host helper cannot escape. The delivery adds measured defenses and explicit limits; `releaseReady` remains false. Stop after publishing 19A. Further containment work and the remaining phase-19 batches require a new request.

## References

- [Windows restricted tokens](https://learn.microsoft.com/en-us/windows/win32/secauthz/restricted-tokens).
- [Process startup attributes and mitigation policy](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute).
- [Windows AppContainer isolation](https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-isolation).
- [Nested jobs and UI-limit constraints](https://learn.microsoft.com/en-us/windows/win32/procthread/nested-jobs).
