# Phase 7 — Volume, files and persistent registry

Active base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. Image: `guardian-kernel-storage.gk`, version 0.7.0, x64, a vCPU, WHP. Status: completed in 16/10/2026. Phase 8 was not initiated.

## Implementation

The guest now has a persistent volume with files, directories, file links, permissions, share, and virtual registry. Logic runs on its own kernel. The host maintains a read-only basis and a separate overlay, receiving only block operations with offset and limited size. File and key names received from CPL3 do not reach the file APIs or Windows host registry.

`guest/Volume.cpp` implements freestanding volume. `StorageKernel.cpp` connects its ABI to the identities and memory spaces of phase 5 and to the block device of phase 6. `PersistentBlockStore.cpp` implements device persistence in the host. Previous images continue using their contracts, including the volatile disk of I/O. unique image

### Base and overlays

The base has exactly 64 KiB and is open with read access. Overlay is an exclusive file, identified by format, version and fingerprint of the base. It stores a limited journal of sector changes. Each record has a sequence, sector number, size and checksum. When reopening, valid records are applied on the basis. Two overlays of the same base remain independent.

The paths of the two files are controller options in the host. They need to be absolute, distinct and point to common files. Reparse point files, directories and device paths such as `PhysicalDrive` are rejected. Handles sharing prevents reopening the base for writing as overlay, including when two names point to the same file. An existing overlay needs valid format and fingerprint; it is not truncated for reuse.

`--create-disk` creates the base if it is missing, without replacing an existing base. A new overlay gets its own header. There is a limit of 4,096 records; exhaustion returns error without overwriting the history. Journal compression is not yet implemented. Test artifacts are in the build directory and are preserved.

### Flush and recovery

The volume serializes files, metadata, security descriptors, links, and record in a 24,064 byte image, within two separate slots on the disk. The initial space of 4 KiB remains reserved for the diagnosis of blocks of phase 6. Slots start in `0x1000` and `0x8800`; no request exceeds 64 KiB.

Flush first records the body of the new image in the inactive slot and calls the device flush. Then saves its commit header, with sequence and checksum, and calls flush again. The active version in memory only advances after success. The persistent device uses `FlushFileBuffers` over the authorized overlay. File and record belong to the same volume commit.

When mounting, the kernel validates the two slots and chooses the largest valid sequence. An incomplete recording in the inactive slot does not invalidate the previous slot. 47 failure positions were exercised before the header was published. The test also covers failure in the final flush: the result is not confirmed to the caller, although a new full image may already be visible in the reopen.

A partial overlay journal tail is removed only after validating its header and all previous complete records. A complete record with invalid checksum makes the opening fail; it is not silently ignored. Within the guest, two invalid and non-empty snapshots produce diagnostics; there is no automatic reformatting of corrupted data. Initial formatting occurs only when the two image areas are zeroed.

Checksums are integrity checks, not cryptographic authentication. Even with recalculated checksum, metadata are validated by limits, types, canonical names, IDs and generations, parents, depth, cycles, roots, links, descriptors and types of values. Image validity does not allow access to host paths.

### Files, directories and links

There is creation and opening with existing/new/open provisions or create, read, write with offset, EOF, truncation, enumeration, renaming and deletion. Expanding a file by truncation or writing after EOF preserves zeros in the range. Limits and permissions are checked before changing bytes. A zero byte writing does not expand the file.

Handles belong to the process and retain granted rights and sharing masks for reading, writing and deletion. New openings check compatibility in both directions. A handle copied from another process is not valid. Changing a DACL restricts new openings without repealing rights already granted to existing handles.

Renameting validates the destination, collisions, sharing and depth before changing the tree; moving a directory inside is refused. Not empty directory is not deleted. Deletion of a file removes its name, but existing handles and links can keep the data alive until last reference. In assembly, volatile references from previous processes are not restored.

Links are hard links of files within the volume, destined for ID and generation. They share content, ACL and file opening control. The deletion of the original name does not destroy a file that is still reachable by another link. Directory links, symlinks and reparse points are not implemented; they are rejected.

The file namespace only accepts absolute `C:\...` paths, internal to the guest. Components are ASCII, with no distinction between A–Z, internal spaces and size limit. `..`, empty components, dot/space railing, relative paths, ADS, UNC, other drives and device namesspaces are denied. There is no normalization that allows you to exit the volume.

### Registration, permissions and notifications

The record has `HKLM` and `HKCU` separated by the guest's numerical identity. Creating a key tree is transactional: permission or capacity failure undoes the components already created. Keys and values have independent names, including when they share the same text. The empty name represents the default value.

There is creation, query, definition and deletion of values, enumeration of values and subkeys and deletion of keys without subkeys. Delete a key removes their values. `None`, `Binary`, `DWORD`, `QWORD`, `String`, `ExpandString` and `MultiString` have size and termination validation; numbers use little endian, and strings store bytes UTF-16LE. Expansion of variables and complete Unicode parity are not declared in this ABI.

Notifications are volatile signatures by process, with filter for names, values or security and subtree option. A scheduler completion event allows you to wait without occupying the CPU. The query returns the changes and closes the signature; the caller registers another if you want to continue. Cancellation or exit of the process releases the event and slots. Subscriptions are not persisted.

Files, directories, and keys use DACL's tokens, groups, canonical DACLs, and phase 5 integrity policy. Crossing/enumeration, reading, writing, deletion, child rearing, and reading/Amendment have rights of their own. Changing DACL does not change owner, group, or integrity by that way. The process receives its kernel token; the call CPL3 does not provide an identity.

### ABI and integration

`shared/guardian/Storage.h` defines the image, commands, and versioned report. The internal syscall `0x80` receives a command of 880 bytes. The kernel validates the entire input and output range in the process space before performing the operation. A supervisory or non-recordable destination is refused before any file or registry changes. Flags, types, versions, and fields reserved by the contract are validated.

Volume operations use the Phase 6 block driver with transfers limited to 512 bytes. Mount and flush are serialized synchronous operations in CPL0; the Phase 6 user I/O queue remains asynchronous. The bus enumeration announces the persistent capacity for this image. The phase report uses the `0xF1` port and fixed GPA `0x10F000`, after process reports and I/O and before the CPU report.

## Acceptance and regressions

The same set of 784 volume checks performs natively and in CPL0 for each storage boot. Includes sharing, ACLs, integrity, rights already granted, tombstones, hard links, truncating, invalid paths, registration, types, enumeration, HKCU identity, notifications with real waiting, quotas, rollback, interrupted snapshots and malformed metadata. There are 128 process creation/exclusion/exit cycles, no handles or notifications remaining.

The 32 host backend-specific checks cover overlay isolation, unaltered base, exclusivity, fingerprint, tail recovery, corruption, limits and actual persistence of file/registry after reopening the disk file, including aborted flush.

The WHP matrix executes 20 reboot pairs by configuration: ten with 16 MiB and ten with 64 MiB. The first boot saves a file counter and log; the second needs to recover both with corresponding values before changing them again. Each pair uses a new overlay from the same base; the base hashes remain the same. The CPL3 program also creates/write/read a file, altera/consultation HKCU, enumerates the directory, forces a pointer refusal and flushes. Previous scenarios of I/O and processes continue in the same image.

There are 18 negative external cases by configuration, including invalid report, wide order/replay/IN/OUT, written in protected enumeration, removed device, corrupted volume, invalid bus and no timer limited by watchdog. A corrupted volume keeps the original records without reformatting. Criteria: `config/phase7-acceptance.json`.

## Check result

| Scan | Result |
|---|---|
| Build complete Debug / Release | approved / approved |
| CTest by configuration | 31 approved, zero failed and zero ignored |
| Core do volume | 784 native checks and on each boot |
| Backend persistent do host | 32 verifications by configuration |
| Boots positive of storage | 80, ed 40 pairs of reboot |
| Phase 1–6 positive regression boots | 480 |
| Cases external negative | 240 (36 storage and 204 regression) |
| Examples reliable | seven per configuration, 14 approved executions |
| Build Debug clean independent | approved, seven pictures auditadas |
| Artifacts by configuration | 60 executable and nine pictures |
| Borders freestanding verified | 32 combinations of target/configuration |
| Evidence historical preserved | 288 files with SHA-256 unchanged |

The storage boot times of the matrix were 304,628–558,013 ms in Debug and 324,984–717,217 ms in Release on this machine; they are not guarantee of time. The image occupies 815,104 bytes of virtual space in Debug and 770.048 in Release, within the limit of 1 MiB of the loader. The seven production images of each configuration passed the audits of imports/TLS/relocations/W+X. The two additional bootes of the CLI by configuration also recovered the volume; they are not added to the 560 bootes of the matrix.

## Build and execute

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild
.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-storage.gk
New-Item -ItemType Directory -Path .\build-codex\meu-volume -Force
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-storage.gk --storage --disk .\build-codex\meu-volume\base.gvd --overlay .\build-codex\meu-volume\sessao.gvo --create-disk --ram-mib 64 --repeat 2 --report .\build-codex\meu-volume\boots.json
```

`--storage` requires explicit base and overlay. Reuse overlay recovers your commits; choosing another overlay from the same base creates an independent environment. These files are the proper format of this prototype, not ISO/VHDX/NTFS. WHP is used directly, without QEMU or changes in Windows settings. No malware samples have been executed.

## Evidence and recovery

New results only use prefix `phase7-`, including phase regressions 1–6. The 288 historical files have their hashes preserved. Backup of the 15 previous files changed is in `snapshots/codex-before-phase7-20261016-210839/`, next to the historical inventory. 14 new implementation files, tests, configuration and documentation.

The `phase7-full-*-script.log` and `phase7-*-tests-details.log` logs record build and CTest. `phase7-boots-*` and `phase7-regression-phase1-*` up to `phase7-regression-phase6-*` save matrices and negatives. The `phase7-*-volumes-*` and `phase7-boots-*-volumes/` directories store the test disks. `phase7-reference-programs.json`, `phase7-smoke.json` and `phase7-cli-*` record examples and CLI. Audits and clean build are in `phase7-image-audit.json`, `phase7-build-boundaries.json`, `phase7-clean-*` and `build-codex-phase7-clean/` logs.

`phase7-change-manifest.json` includes source hashes, backups, executables, images, bases, overlays and evidence. Revalidate without running programs: `python tools/validate_phase7.py --snapshot snapshots/codex-before-phase7-20261016-210839`. Intermediate attempts have been preserved. Excessive stack usage in Debug has been corrected by avoiding large temporary images and multiple temporary copies of commands. Guard page and actual stack probe remain active. LNK4281 is the expected warning of kernel fixed base in 2 MiB.

## Limits and Stop

| Resource | Limit of this step |
|---|---:|
| Virtual Disk | 64 KiB |
| We, including roots, files, keys and values | 32 |
| Content of a file/value | 512 bytes |
| Handles global / by Case | 32 / 8 |
| Signatures of log | 8 |
| Path/component, including NUL | 128 / 32 bytes ASCII |
| Depth | 8 components |
| Records by overlay | 4.096 |
| Image by snapshot / slots | 24.064 bytes / 2 |

The format is proper and limited. It does not implement NTFS/FAT, Windows hives, full NT ACLs, streams, directory links, external volume mount, mapped file memory, automatic disk expansion or overlay compaction. Flush persists the entire volume; there is no file page cache. Limits make the surface verifiable before capacity magnification and compatibility. The complete Windows product and kernel remain in development without promise of sandbox invisibility.

At the end of phase 7, stop. Phase 8 only begins after further request.

## Semantic references

- [Microsoft: FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
- [Microsoft: data flush in buffer](https://learn.microsoft.com/en-us/windows/win32/fileio/flushing-system-buffered-i-o-data-to-disk)
- [Microsoft: Record Value Types](https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types)
- [Microsoft: registry notifications](https://learn.microsoft.com/pt-br/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue)

These references guide semantics. The limits and differences of the ABI itself are described above; it is not declared general parity with Windows APIs.
