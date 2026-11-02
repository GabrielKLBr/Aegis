# Phase 18 — Authenticated portable distribution and recoverable updates

The implementation provides signed portable packages, verified service deployment, recoverable activation, explicit rollback, signed definition bundles, versioned storage migrations and separate antivirus/virtualization diagnostics. It builds on the phase 17.1 portable layout. **Clean-Windows acceptance remains pending:** no test VM or ISO is available in this session. No CA-backed Authenticode code-signing certificate is present. The current delivery is a validated development distribution, not a certified production release or a declaration that every phase 18 acceptance criterion has been met.

## Run and update

Extract the complete Release ZIP and open `Aegis.exe`. Python, an MSI and a separate application installer are not required. `runtime/` contains WinUI and the self-contained Windows App SDK, including platform localization resources. `engines/` contains the service payload and initial definitions. `notices/` contains component inventories and the supplied third-party notices. The two `aegis.manifest` files are required.

The bootstrap verifies the signed inventory before loading WinUI. On Settings:

1. **Install engine services** or **Update / repair services** installs the authenticated engine payload after explicit Windows administrator authorization.
2. **Apply portable update** accepts an extracted, signed portable package folder. It copies a separate version and changes the active selection only after verification. Restart Aegis to use it. Engine services are updated separately through their repair/update button; these are independent transactions, with the same supported service protocol.
3. **Use previous portable version** selects the retained previous signed version. It does not lower the remembered update sequence.
4. **Update rules and hashes** accepts an extracted, signed definition bundle. **Use previous definitions** restores its retained previous selection. Both require administrator authorization.
5. **Use previous engine services** restores the recorded previous service slot, startup modes, running states and service permissions, after administrator authorization.

Updates are local and explicit. This version does not silently download code or extract arbitrary ZIPs. Extract a downloaded archive before selecting it. No authorization dialog is opened automatically at startup, during diagnostics or during ordinary analysis. Interrupted privileged deployment is recovered when the next install/repair command runs; recovery does not silently request elevation from an ordinary desktop client.

Service removal unregisters both services and retains the versioned code, settings, prepared local runtime references and encrypted vault. Moving the portable directory does not move installed services. Restore/delete individual vault records through the existing explicit vault actions. No bulk data purge is performed by uninstall.

## Publisher signatures and trust boundary

Each package contains `aegis.manifest.json` and `aegis.manifest.sig`. The exact manifest bytes are authenticated with ECDSA P-256/SHA-256. The public verification key is compiled into the bootstrap, service installer and verification tool. The private publisher key is non-exportable in the current publisher user's Windows CNG software key store and is never written into the repository or build archives. This is a software-backed key, not a hardware security-module claim.

The signed contract binds product, kind (`portable`, `engines` or `definitions`), release sequence, version, configuration, architecture, supported service/data contracts, member sizes and SHA-256 values. Verification bounds metadata to 2 MiB, JSON depth, file size to 256 MiB, total payload to 2 GiB, member count to 4096 and directory entries to 8192. Traversal, alternate streams, reserved Windows names, case aliases, reparse points and hard-linked members are refused. Files, metadata, directories and ancestors stay locked through launch/copy. Missing, changed and unlisted files are refused. The bootstrap never accepts a key supplied by the downloaded manifest.

An application manifest signature is **not Windows Authenticode**. The bootstrap EXE itself needs an initial trust decision: verify the ZIP checksum against the publisher's independently obtained build record before first execution. Replacing the verifier and every trust reference under the same compromised user account is outside this protection. Windows may continue to identify the EXE as an unknown publisher until a suitable code-signing certificate and signing process are configured. No certificate is installed into Windows trust stores and no Windows security feature is disabled. Key rotation requires an independently verified replacement bootstrap; key lifecycle/backup remains a publisher responsibility.

The read-only diagnostics/verification tool is included:

```powershell
.\engines\guardian-distribution.exe prerequisites
.\engines\guardian-distribution.exe verify <EXTRACTED_PACKAGE> portable
.\engines\guardian-distribution.exe status <PORTABLE_ROOT>
.\engines\guardian-distribution.exe rollback <PORTABLE_ROOT>
```

The root bootstrap also accepts `--rollback` if the selected UI cannot start. It verifies the retained target before selection. Signature/hash failures are not ignored or converted into successful launches.

## Recovery and permissions

Portable versions live below `versions/update-<manifest-identity>`. The original distribution stays in place. New files are copied and flushed, verified again, and only then selected through an atomic, flushed pointer replacement. A pending journal distinguishes an incomplete preparation from completed activation. Termination before activation leaves the prior version selected; termination after activation preserves the fully verified new version. No partially copied directory is executed. Inactive/staging directories are retained, not recursively deleted. A free-space check and a 16-directory limit prevent unlimited accumulation; remove only inactive directories after checking current/previous selections.

Service deployment authenticates the source before stopping or reconfiguring either service, verifies the protected copy again and checks antivirus IPC health before committing. Program Files directories, copied payloads and journals are SYSTEM-owned from creation, with standard users limited to read/execute. A protected, flushed journal captures the previous SCM configuration and permission descriptors. Repair recovers an interrupted deployment; rollback verifies the recorded previous slot before restoring it. The first adoption of a pre-phase-18 installed slot records its existing protected hashes for local recovery. Newly supplied unsigned engine packages are rejected. Equal-sequence signed service repair is supported; older package sequences require explicit rollback.

App and service activation are separate transactions. The current protocol/data contract remains version 1; an unsupported future contract is refused rather than guessed. Process-interruption testing does not certify sudden hardware power-loss behavior. SCM recovery restores the documented image/startup/permission/running-state fields, not an arbitrary third party's service configuration. Unrelated service registrations are left untouched.

The integration work also corrected elevated-client worker creation: it prefers the linked standard-user token, restricts privileges/integrity and gives the reduced identity access to its own default IPC objects. Worker process/thread objects remain supervisor-protected. Terminal worker replies wait for consumption before exit so short results/errors are not lost. Tests verify that analyses requested by an elevated client still execute without administrator membership or elevation.

## Rules, hashes and storage

The initial signed definition bundle contains only `signatures.txt` and `rules.json`. Independent updates use the same contract and publisher key, with a separate monotonic sequence initialized from the installed signed baseline. Validation checks signatures, parses hash records and compiles the licensed YARA package before activation. New bundles are staged in SYSTEM-owned directories, verified, renamed into place and selected atomically. Termination during copy or before selection is recoverable by retrying the same package; incomplete directories are retained without being selected. Even the first independent update retains the installed bundled definitions as a rollback target. The store checks free space and limits retained definition directories to 128. Both protected scans and the service's general scan consume the selected signed definitions. No untrusted sample is executed during validation.

The default definitions remain Aegis's limited owned/test signature set, not a claim of a comprehensive malware database. Public hash lookups from phase 17.1 remain explicit supporting evidence over authenticated HTTPS; provider responses are not misrepresented as publisher-signed offline databases. Their API credentials and usage terms remain separate.

Existing SQLite layouts had `user_version=0`. The migration records explicit versions (scanner/protection 1, vault 2), after creating a consistent SQLite backup when legacy tables exist. All records and encrypted vault payloads remain in place; the migration does not change their existing physical table layout or re-encrypt/remove keys. Future/unsupported versions are refused rather than reset. Preferences migrate to schema 1 while retaining an original `preferences.schema0.backup.json`. No personal state or schema backups are included in distribution archives.

## Prerequisites and component distribution

The current application requires x64 Windows 10 build 19041 or later. The antivirus does not require a running MiniNT service or hypervisor. WHP imports in the engine and scan worker are delay-loaded; the absence of an optional virtualization DLL cannot prevent their basic antivirus startup solely through an eager import. MiniNT separately requires the running Windows hypervisor, WHP and NX. Diagnostics read these conditions and never enable features, change firmware or restart Windows automatically. Actual acceptance on a clean host without WHP remains part of the pending machine matrix.

The Release package includes app-local Visual C++ Release redistributables and the self-contained Windows App SDK. Debug is a developer artifact requiring the Visual Studio debug CRT, which is not redistributed. `notices/components.json` lists the resolved NuGet build dependencies and native library families; the supplied license/notice files are preserved. This inventory is not a blanket license for third-party Windows components or commercial API services. **No guest Windows DLLs, local GPV/runtime reference catalogs, ISO/disk images, credentials, private publisher keys or vault files are distributed.** Locally prepared runtime references retain the existing hash validation and ownership controls.

## Build and reproduce

Use the configured native Windows build and build `guardian-engine-service`, `guardian-distribution`, `guardian-publisher`, `aegis-portable`, `distribution_tests` and the development-only `deployment_fault_driver`. Build WinUI with `tools/build_winui.ps1` for the matching configuration. The publisher tool is not included in the portable runtime:

```powershell
# Maintainer provisioning only; writes public material to the chosen header.
guardian-publisher.exe --create-key <PUBLIC_HEADER_PATH>
# Rebuild after changing the public verification key.
guardian-publisher.exe --sign <FOLDER> definitions <SEQUENCE> <VERSION> Release
```

For an existing publisher key, do not replace the trust header casually. Losing the private key prevents signing releases trusted by the existing bootstrap.

```powershell
python -B tools/package_distribution.py --backend-build <NATIVE_BUILD> --config Release --output <NEW_FOLDER> --stage-only
# Validate the exact staged bytes, then seal without regenerating ECDSA signatures:
python -B tools/package_distribution.py --backend-build <NATIVE_BUILD> --config Release --output <SAME_FOLDER> --seal --proof <VALIDATION_JSON>
```

The proof binds the current source/binary hashes and the exact signed package identity. `--build-archive <NEW_FOLDER>` additionally preserves source and validation for the private historical build repository. Fault injection exists only in separately compiled test targets; those targets are not shipped in the app payload.

Validation is limited to phase 18 and the dependencies used by these flows, without replaying historical compatibility suites. The current workstation tests cover signature/tamper rejection, interrupted updates, migrations, actual service rollback/repair/reinstallation, signed hash/rule changes, restricted workers, metadata ownership, delayed WHP imports and native UI update/rollback. [Per-case results and outstanding acceptance](PHASE-18-RESULTS.json).

Remaining release gates are a clean Windows installation/update/removal matrix, a host with WHP absent, and the publisher's production code-signing/key-management process. The user has confirmed that no clean test VM or ISO is currently available. These gates remain open, and phase 19 is not started by this delivery.

References: [CNG persisted keys](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptcreatepersistedkey), [key export policy](https://learn.microsoft.com/en-us/windows/win32/seccng/key-storage-property-identifiers), [Windows hypervisor readiness](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvgetcapability), [self-contained Windows App SDK deployment](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-self-contained-apps), [SQLite backup API](https://www.sqlite.org/backup.html).
