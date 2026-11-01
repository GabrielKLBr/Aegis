# Phase 14.6 — registration, security and guest services

This delivery adds own `advapi32.dll`, `sechost.dll` and `userenv.dll` DLLs to the `guardian-kernel-security.gk` kernel. An external MSVC `/MD` application persists settings, query your identity, check a DACL and initiates a separate process, account and service life within the MiniNT. The batch is bounded to the Windows 10 x64 profile and CRT references already fixed on 14.5. It does not represent ADVAPI32, Windows or complete binary compatibility.

## Implementation and authorisation

- Registration: open/create HKCU/HKLM keys, A/W values, query sizes,
  W enumeration, deletion, flush, DACL and synchronous notification. Each handle holds owner, granted access and node generation. Close a handle, use it in another account/Case or delete/recreate key does not redirect access to a new object. NT registry routes also check access and generation in this kernel. The required sizes are returned in the small buffer errors.
- Tokens: `OpenProcessToken` of the process itself, restricted duplication and queries
  User, Groups, Privileges, Owner, PrimaryGroup, Type, SessionId, Elevation and IntegrityLevel. Handles retain real references to process manager objects and are closed by `CloseHandle`. Duplicate does not extend rights.
- SIDs: own accounts `MiniNT` and `LocalService`, admin/user groups,
  Everyone and integrity labels. Construction, comparison and textual conversion of delimited SIDs; absolute/relative descriptors and allow/deny canonical ACEs. `AccessCheck` uses the token retained by the kernel and the 5/7. phase rights model does not grant privileges or alter the caller identity.
- Profiles: private directories `C:\Users\MiniNT` and `C:\Users\LocalService`, HKCU
  separated by user and own environment block. The service account does not inherit `USERNAME`/`USERPROFILE` from the administrator account.
- SCM: Connect, create/open, consult configuration/state, start, send STOP
  and delete a `SERVICE_WIN32_OWN_PROCESS` service under `NT AUTHORITY\LocalService`. Administrative control depends on the actual identity of the MiniNT account. The service receives user 200, group 20, session 0, average integrity and no privileges. A common account cannot create, start, stop or delete services by forging an order or copying the number of a handle from another process.
- Events: register source, report string and read W records with identity
  the author, number, type, ID and time of the published clock. The circular buffer includes the eight most recent events of the session.

`SECHOST` forwards the entries of services/events and `USERENV` profile entries to the own DLL ADVAPI32. Packaging checks these forwardings and inventory exports. Host ADVAPI32/SECHOST/USERENV are not loaded.

The private `0xA6` protocol accepts only calls originating from executable pages of ADVAPI32 itself. The kernel copies and validates the complete package through the caller pages. Rights, tokens, handles, status and identity are verified in the kernel. Callbacks `ServiceMain`/control continue in CPL3. The IPC of this batch is the service's control/status channel; PRC and named general pipes remain out of it.

## Persistence and failures

The changeable state uses the two-slot journey volume of Phase 7. The 64 KiB region is reserved in GPA `0x5000000`, out of the volume of DLLs and private page allocater; the host a unrunned map. The program does not have direct access to it. Flush/export do not expose actual host files, services or registry.

`guardian-pe` maintains its five previous arguments and accepts an optional input argument, `SECURITY_STATE`, only for the kernel of this phase. A successful execution exports `security-state.bin` in the new session folder. The file can be provided to a later execution. Incorrect size or corrupted metadata fail; an invalid state is not replaced by an empty record and no successful export after refusal. Checksum treats corruption and consistency; it does not authenticate a file deliberately rewritten by its owner.

SCM settings stay in `HKLM\System\CurrentControlSet\Services\<nome>\Config`. Create/delete settings use transaction with memory image rollback and commit by the journal. Lack of handles is detected before creating a key/configuration. The deletion tag is also persisted. Definitive removal waits for the process to end and the handles to close.

The SCM maintains a reference to the service process object. Close the SCM or close the creator does not kill the service. A process failure is observed by the retained object even after its collection; the state passes to STOPPED/error 1067. Process output clears its tokens, keys, event sources and pending notifications.

## Reproducible validation

Five tests added to CTest:

1. `security_contract`: layouts confronted with SDK Windows.
2. `security_core`: accounts, DACLs, crossed/invalid handles, generations, exhaustion
   Handles, allocation failure, service lifetime, cleaning and recovery of the journal after each of 53 writing/flush. fault points
3. `security_windows_reference`: buffer references, token, SID, DACL and query
   to the SCM of Windows; installs zero services in the host. The temporary key belongs exclusively to the test, in HKCU.
4. `security_runtime_tools`: exports, forwarders, import inventory, bond
   with correct kernel and changed volume refusal.
5. `security_runtime_integration`: application with original CRT in guest,
   Real notification of registration between threads, LocalService isolation, event, reopened status in another session, continuity after creator exit and recovery after invalid instruction in service. Also refuses incorrect and corrupted size states. Checks absence of objects/handles/references alive at the end.

`tools/validate_phase14_6.py` requires the matrix of 109 configured tests, performs the complete Release regression and 34 acceptance tests in Release/Debug, and fixes SHA-256 of the sources, executables and references before/later of each execution. Results, logs and XML stay in `build-phase14.6/`; no omitted test counts as approval. `tools/package_phase14_6.py` requires these stable tests. The package smoke repeats four scenarios and two refusals in each configuration.

## Limits that remain open

- Three processes, four threads per process, three service records, 48
  Handles of this protocol, eight watches and eight events. The native limits of memory/handles and 16 files in the immutable volume are maintained.
- The test storage has 32 nodes and 512 bytes per file/value, total of
  16 KiB data. The status file has 64 KiB for including metadata and journal. It is not a Windows hive nor a complete Windows disk.
- Administrative Names/paths are ASCII, up to 127 characters of path and
  31 per component; W value strings use UTF-16. A variant accepts only ASCII content in this batch. There are no WOW64, log links, external hives or remote record. `RegNotifyChangeKeyValue` asynchronous remains refused. Notifications use read/list storage model permissions.
- ACLs use the compact canonical model of 5/7 phases; they do not cover all the
  Windows rights, ACEs of object/callback/inheritance, SACLs, owner exchange, full audit or arbitrary privileges. Operations outside this model fail; the corpus does not establish general equivalence of NT ACLs.
- Duplicate tokens support consultations/AccessCheck; do not implement impersonation
  thread, logon, change of privileges, domains or arbitrary creation of accounts. The null argument of profile helpers represents the current account.
- SCM accepts manual start, exclusive process, a LocalService account and STOP.
  There are no LocalSystem, credentials/passwords, autostart, dependencies, start arguments, pause, auto recovery or installing drivers. A locked service is not treated as stopped by artificially returning success.
- The event log is a ring in memory of this session, no message catalog,
  categories, binary data or durability between resets.
- Only the Win10 profile of this matrix has been accepted. 14.1 and
  lon references/versionamentos ed 14.3–14.5 remain in force; other arbitrary profiles and programmes do not gain compatibility by such approval.
- Broad external GUI, WinRT, DirectX, general network, arbitrary DLLs and SMP of
  This delivery to before 14.7 and does not alter the to-ed of 12F.

## Primary references

- [Service Accounts](https://learn.microsoft.com/en-us/windows/win32/services/service-user-accounts).
- [Service States ](https://learn.microsoft.com/en-us/windows/win32/services/service-status-transitions).
- [SCM and services rights](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights).
- [Information classes of token](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_information_class).
- [registration notification ](https://learn.microsoft.com/pt-br/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue).

Sources are published in `GabrielKLBr/Aegis`; compiled components and their proofs use only `GabrielKLBr/Aegis-builds`. Microsoft input DLLs, ISOs, GPV volumes and personal data do not integrate the published package.
