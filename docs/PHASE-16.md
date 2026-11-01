# Phase 16 — Protection service, IPC, and real-time monitoring

Protection now runs in the Windows service, with persistent policy and queue, monitoring after file changes, scheduling, notifications, and a protected vault. Work continues when the application is closed. This delivery does not change MiniNT compatibility or repeat the 12F–14.x campaigns.

## Using the application

Open **Protection** to inspect the actual status, start a quick, full, or folder scan, track the queue, and cancel a manual scan. **Protect folder** adds the selected folder and enables monitoring and quarantine of **confirmed detections**. Pausing/resuming protection, protecting folders, stopping the antivirus service, restoring content, and deleting vault entries require an administrator-authorized client. The application does not automatically request elevation for these actions; service refusals appear in the interface. The MiniNT service retains its separate controls. Initial configuration does not select folders or enable quarantine of personal files without an explicit choice.

The page indicates paused or incomplete protection, a missing user session, and failure notifications. The previous scanning interface remains available for interactive sessions; the persistent queue and monitor are accessed through Protection. Full migration of the interface, accessibility, and languages remains in phase 17.

## Supervision and authorization

The supervisor runs as LocalSystem with the privileges required for IPC, process creation, and obtaining the installation owner's session token. It does not retain SeDebug/SeBackup/SeRestore to inspect arbitrary files. Parsers run with a restricted user token, medium integrity, memory/process limits, and a Job that closes with the supervisor. Worker process/thread objects belong to SYSTEM and do not grant modification/termination rights to the ordinary account.

The endpoint rejects remote clients, low-integrity processes, and users other than the installation owner. The client checks the PID published by SCM; the supervisor checks the worker PID and uses a random pipe for each execution. Internal operations 25/26 cannot be invoked through the public endpoint. There are four bounded front-end connections. Automatic execution capacity is separate from manual execution: a long manual scan does not occupy the worker reserved for monitoring.

Administrative authorization is checked in the authenticated token **before** reducing it. JSON content does not grant privileges. Configuration, deletion, restoration, and cancellation of automatic tasks require an administrator; status queries, notifications, scan requests, and cancellation of the caller's own manual tasks do not require elevation. Request files are read using the client's restricted identity, with a 64 KiB limit. The supervisor opens sources and destinations using that identity's permissions, never taking advantage of SYSTEM rights over client-supplied paths.

Installation protects ownership and DACLs of binaries/slots, including against DACL changes by the ordinary account that installed the program. The antivirus uses delayed automatic startup and SCM recovery after failure. The unelevated account can query/start the antivirus, but cannot stop it or change its configuration. Previous slots are retained for recovery. The update neither disables Defender nor installs a driver.

## Protected vault

Service state resides in the Windows-known ProgramData directory, under `AegisProtection16`. State, policy, queue, and keys are not stored in the application folder or a client-controlled directory. Directories are protected and owned by SYSTEM. Existing directories/state owned by an ordinary account, links/reparse points, and state hardlinks are rejected.

The vault retains AES-256-GCM, byte/SHA-256 verification before removing the original, deletion through the handle, and restoration without replacing files. Its key now uses SYSTEM-context DPAPI. Source files and restoration temporary files are opened with the restricted user token; the database and key are accessed only as SYSTEM. Original ownership and ACEs remain preserved. The encrypted copy remains until explicitly authorized deletion. Restoration into an administrative directory does not use the client's elevation: restricted permissions must allow writing.

The GUI and `av clean`/listing use this broker. `guardian-quarantine.exe` remains a per-user vault diagnostic tool; it is not a shortcut to the SYSTEM key. Previous phase 15 vaults are neither removed nor silently converted. They can be recovered with the phase 15 tool, followed by explicit quarantine in the new vault. Migration, export, and credential recovery belong to the phase 18 update lifecycle.

This protects storage and administrative operations against an ordinary client of the same account. It is not PPL, kernel protection, or resistance against administrators, SYSTEM, already-authorized administrative credentials/channels, or control of Windows. It does not prevent tampering with source files the user can write.

## Monitor, queue, coverage, and scheduling

The monitor uses asynchronous ReadDirectoryChangesW, observes creation, modification, renaming, and directories, and coalesces changes for approximately one second. Buffers are 64 KiB. Empty or malformed notifications and queue overflow produce a **coverage-gap** and a recovery scan; they are never interpreted as a clean environment. The monitor is armed before the initial scan. Restarting the service rearms it and requests another scan of the configured scope. Folder identity is checked periodically; moving or replacing a watched root triggers reopening and a recovery scan.

SQLite retains policy, tasks, and events with DELETE journaling and synchronous FULL. Interrupted running tasks return to the queue with a recovery marker. This does not promise continuation of a filesystem snapshot: enumeration is repeated. Configuration and cancellation of old automatic tasks change in the same transaction. Missing/empty established state or markers and missing settings do not silently reinitialize an established database.

The queue admits up to 128 pending tasks, at most 16 of them manual. History retains approximately 512 completed tasks; status returns the 64 most recent. Automatic requests still queued are coalesced, without treating an ongoing scan as coverage of a later gap. The coalesced change queue is limited to 1024 paths. Limits/failures are visible and trigger scope recovery. Excess load may delay analysis; there is no latency guarantee against an unlimited producer of changes.

Quick scans use quickRoots, falling back to roots and known user folders. Full scans plan fixed local volumes and use only files readable with the user token. Custom scans accept up to 16 paths. Denied ACLs, quotas, reparse points, and time limits are distinguished from completed analysis. Exclusions are normalized paths/descendants compared independently of linguistic locale; they are not glob patterns or extensions.

Protected parsers ignore user-controlled rule configuration/cache: they use embedded rules and signatures from the installed slot, offline and without executing the analyzed file. The synthetic test marker is accepted as a small sentinel file, not as a source-code signature that merely quotes the marker. Validating these rules does not certify antimalware effectiveness; effectiveness/false-positive campaigns remain in phase 19.

Schedules are periodic, use UTC seconds, range from 60 seconds to seven days, and allow up to 16 entries. Advancing nextRun and enqueueing work are transactional. After downtime, one pending occurrence runs without generating a storm of old occurrences. Windows supplies the owner's session without the UI being open; without a usable session, tasks wait and status reports it.

Notifications retain up to 4096 events, with a cursor and retention-gap indication. Detections/gaps/failures also attempt to publish a Windows Application event and record whether publication succeeded. Native events use the filename; the complete path remains available through the owner's authenticated query. The application queries these notifications and the actual status without inventing positive states.

## Limits and validation scope

Monitoring happens **after a change**. There is no minifilter or preventive execution blocking: a process may start before analysis. A driver should be added only when needed and with supported deployment/signing. The parser is limited to 32 MiB per file and its inspection budget; the vault retains its 64 MiB limit. Policy permits up to 100000 files and 86400 seconds per scan; defaults are 10000 files/1800 seconds. Reaching a limit leaves an incomplete result. The state database uses at most 32768 SQLite pages; insufficient storage is an error, not permission to discard data. Phase 15 path, DACL, and source-format limits still apply.

Tests use new, inert files. They validate actual administrative permissions, IPC, an administrative file unreadable by the client, broker quarantine/restoration, retained copies, collisions, exclusions, notifications, renaming, scans, concurrency, schedules, and work recovery after terminating the service process. Integration runs against services installed in Release and Debug. Each configuration passed **58/58** integration cases and **25/25** native cases. [Per-case results](PHASE-16-RESULTS.json). Native cases cover notification decoding and protocol separation. Malformed/overflow buffers use synthetic vectors; service termination and NTFS changes are real. No PC reboot/logoff, physical power cut, unlimited-load certification, malware effectiveness evaluation, or full GUI click suite was performed. The GUI was compiled and its service contracts exercised.

## Reproduction and tools

```text
guardian-engine-service --protection-status
guardian-engine-service --protection-config policy.json
guardian-engine-service --protection-scan scan.json
guardian-engine-service --protection-cancel cancel.json
guardian-engine-service --protection-events cursor.json
```

Use [the policy example](../config/protection-policy.example.json) with schema 1. Scan requests use mode quick/full/custom and paths for custom scans; cancellation uses id; notifications use after. Schedules are a list of `{id, mode, paths, intervalSeconds, nextRun}`. Vault requests retain schema 2.

`tools/validate_protection.py` prepares fixtures and runs unelevated stages; `tools/validate_protection_admin.py` explicitly runs administrative stages in an already-authorized terminal. The tools do not attempt to elevate themselves. Stage order is prepare, basic, configure (admin), monitor, release (admin), schedule (admin), recovery-submit, crash (admin), recovery-check, restart (admin), cleanup (admin), final. Each stage uses the same fresh folder and --config Release/Debug. The previous policy is restored and only fixture entries are deleted. `tools/package_protection.py` requires both current proofs, retains owned source and binaries, and excludes databases, keys, local requests, Microsoft references, user state, and local runtime catalogs. The extracted package receives another hash check and execution of this phase's contracts.

Official references: [directory notifications](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw), [session token](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken), [access under the user's identity](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-impersonateloggedonuser), [service security](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights).
