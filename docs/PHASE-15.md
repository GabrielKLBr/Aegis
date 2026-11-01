# Phase 15 — Quarantine and Threat Management

This delivery replaces the XOR storage and fallback that truncated the original. Quarantine works without MiniNT. Application operations use the antivirus engine and an auxiliary process with the restricted user token authenticated. SYSTEM supervisor does not open file paths sent by the client.

## Operations and integrity

The safe per user is in the actual LocalAppData known by Windows, in `Aegis/quarantine-v2`. The LOCALAPPDATA environment variable does not choose its destination. SQLite maintains content, metadata and history in transactions with DELETE journal, FULL synchronous and secure_delete. SQLite extensions are disabled in the build. Contents and metadata have authenticated AES-256-GCM encryption with independent domains and nonces. The key is protected by the user's DPAPI. Encrypted history hashes chain and keeps an authenticated head in the same transaction. The opening detects alteration, history truncation and disappearance/resurrection of entries in relation to registered actions.

Isolate keeps the file open without sharing to writing/exclusion, registers identity, SHA-256, size, attributes, schedules and DACL. For a detection, hash needs to match the bytes checked by the scanner. A encrypted copy is confirmed and reread before deleting the original by its handle. There is no fallback to truncate or overwrite the original. Subsequent failures save a recoverable input; prepared or pending states require revision and do not cause automatic deletion when opening the program.

Restore requires chosen directory and new name without overwriting destinations. The content is authenticated and checked by SHA-256, written in a private temporary that denies execution, downloaded and reloaded. Windows publishes the same file by renaming without overwriting. DACL, supported attributes and schedules are restored; the original ACEs are protected from additional inheritance of the new directory. The copy in the vault remains up to an explicit deletion. The program does not execute the restored file. If the history is full or its writing fails, the explicitly requested recovery can still produce a verified copy; the reply informs `complete=false`, `copyRestored=true`, `historyUpdated=false` and preserves the vault. It does not announce a fully completed operation in this case.

Delete removes only the entry of the vault into a transaction with its history action. It does not delete existing files in the source or copies already restored. This is logical deletion: it does not guarantee physical deletion in SSD, backups or previous versions.

## Interface and service

The Quarantine page provides update, manual isolation, item selection, restore to new destination, deletion with confirmation and paged history. Scan detections offer Isolate linked to the detection hash. The interface shows the effective response of the service, including failures and copies that require revision. Actions occur outside the graphical thread. Shutting down the engine prevents these operations in the interface; there is no alternative privileged execution in the UI.

The protocol adds operations 6–10 only to the antivirus engine. Change requests use JSON schema 2 up to 64 KiB. The safe directory is fixed by the worker; it cannot be chosen in the service request. Lists have limited previews of long fields, identified by `previewTruncated`; complete values remain encrypted. History returns up to 128 actions per response, with `nextBefore` to search for previous pages. All pages are checked against the full string when opening the vault.

The `guardian-quarantine.exe` tool allows an explicitly chosen safe for diagnostic and testing. Example with own and inert files:

```text
guardian-quarantine --root D:\teste\cofre --action isolate --input D:\teste\inert.bin
guardian-quarantine --root D:\teste\cofre --action list
guardian-quarantine --root D:\teste\cofre --action restore --id ID --directory D:\teste --filename recuperado.bin
guardian-quarantine --root D:\teste\cofre --action audit
guardian-quarantine --root D:\teste\cofre --action audit --before NUMERO
guardian-quarantine --root D:\teste\cofre --action erase --id ID
```

The service offers `--q-list`, `--q-audit [pagina.json]`, `--q-isolate pedido.json`, `--q-restore pedido.json` and `--q-erase pedido.json`. Requests include `schema: 2` and, respectively, `source/sha256/threat/reason`, `id/directory/filename`, `id`. History pagination uses `before`. The old console client also uses the new safe and hashless detection refusal.

## Limits and protection model

- Up to 64 MiB per file, 256 entries, 8192 actions and 262144 SQLite pages
  4096 bytes (1 GiB, in addition to temporary journal). Limits refuse new isolations before removing the original. Full history is not discarded automatically; explicit recovery remains available with incomplete history notice.
- Absolute local paths with less than 240 characters; fixed local volumes or
  removable. UNC, devices, traversal, ADS, junctions/reparse points, hardlinks, EFS, tablets/esparsos/offline files are rejected.
- Only files belonging to the current user with concrete DACL. There is no
  SACL restoration, other owner, alternate streams or backup privileges. Files blocked or without deletion authorization remain at source.
- ACLs limit access to user and SYSTEM. DPAPI is linked to user profile
  and usually to the computer. Losing profile/key can prevent recovery. A missing key is not restarted on an existing bank; missing, empty or no history head is also not restarted with old key.
- The safe is not a border against a process that already controls the same account:
  this process can use DPAPI credentials and user permissions. Protection against malware tampering with the same identity depends on a protected service broker/vault, to deepen in phase 16. Cryptography and audit chain are not presented as resistance to this opponent or full rollback of a consistent old copy of the bank and key.
- Private temporarys of abruptly disrupted restoration may remain
  in destination, no execution allowed; there is no auto scan that deletes user files. XOR legacy files are neither imported nor removed.
- The evidence includes abrupt closure of processes, not physical cutting of energy.
  Durability depends on NTFS/SQLite/storage complying with flush. There is no certification of antimalware protection or recovery in any physical failure.

## Validation and reproduction

The campaign uses new and inert files, real NTFS operations and a separate crash injection driver. The production tool does not accept these commands. The test driver is not installed as a service component.

```text
python tools/validate_quarantine.py --build BUILD --config Release --output PASTA_NOVA
python tools/validate_quarantine.py --build BUILD --config Debug --output OUTRA_PASTA_NOVA
python tools/validate_quarantine_services.py --build BUILD --output NOVA_PASTA_SERVICOS
python tools/package_quarantine.py --build BUILD --release PROVA_RELEASE --debug PROVA_DEBUG --services PROVA_SERVICOS --output NOVO_PACOTE
```

Bytes, SHA, schedules, DACL, safe ACL are validated, Unicode names, empty file, conflicts, exchange after detection, blocking, read-only, links/streams, quotas, SQLITE_FULL, interruptions in isolation/restoration/exclusion, GCM tags, lost key, inventory versus history and recovery with unavailable history. Integration uses the installed service, inert marker scanner, restricted token, isolation, listing, restoration, conflict, deletion, history and engine offline. Release and Debug settings approve **57/57** cases each; integration of the installed service approves **13/13** cases. The interface is compiled in Release/Debug and your data contract is exercised by the service client; it does not equate to the complete GUI click manual test. Acceptance is repeated from the extracted package, the bytes of which are checked. 12F compatibility batteries are not repeated–14.x. The [case results](PHASE-15-RESULTS.json) record the acceptance of this phase.

DPAPI keys, quarantine banks, user files, Microsoft references, and local execution catalogs do not enter the package. Sources go to the public repository; binaries and their proofs stay in the private builds repository.

References: [DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata), [renamed without replacement](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info), [SQLite syncronous](https://www.sqlite.org/pragma.html#pragma_synchronous).
