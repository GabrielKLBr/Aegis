# Phase 14.13 — integration, analysis and recovery

## Delivery Scope

The Service Worker's EXE Scan and Analysis Operation use the `microvm::runtime::Run` input. The queue ceases to use legacy MicroSandbox/NT. The same path meets `guardian-runtime.exe`, with manifest, profile, modules, policies, telemetry and closure linked to the package selected by SHA-256.

The target of this delivery is **NT 10.0.19045**, with three existing families: USER32 x64 basic, PE32/x86 core and frameworks/audio. runtime x64 The input is unified; the kernels and protocols appropriate to each family remain separate. There is no universal kernel with all the announced compatibility. 14.11 and 14.12. to-ed are not promoted to full support.

Each EXE needs a package of dependencies previously prepared and registered in the local registry. There is no automatic search of DLLs for the host disk. File without package, incompatible import, wrong architecture, failed and exceeded deadline produce**inconclusive** result. There is never fallback to run EXE on Windows or for the legacy executable.

## Packages and analysis

`tools/unified_runtime.py` prepares the bundle using existing builders and enrolls its SHA-256 in `index.json`. The registration binds manifest, binding, volume, kernel and hash of the auxiliary. The registration requires legal local references when the x64 builder needs Microsoft CRT/schema. The program is not run by the builder.

The worker checks the bytes received from the scanner against the volume root and NT identity. It keeps archive files open for reading, preventing its replacement during the session. The guest code remains in VM CPL3. The package does not choose an arbitrary host executable: only the known wizard next to the installed program can be started, and its hash needs to match.

The result registers profile, architecture, package identity, modules and policy. Events keep PID/TID, author, intention/result, status and effects on virtual files/log. `virtualFiles` describes observed changes, does not list the entire file system. Functions that perform entirely within DLLs do not receive automatic coverage; no interception of any API or universal malware detection is announced.

`unknown` means that no rule has found anything in the observed range. Incomplete execution, loss of coverage or failure of the helper prevent this complete result. Full reports stay in a separate session folder; the queue saves the result. Retention of the queue is limited to 64 session folders, requiring explicit filing before growing further.

## Basic USER32 Real Checkpoints

Checkpoint saves RAM, processor status, mappings, processes, external modules, batteries, quotas, VFS/log in memory, clock and telemetry. The immutable volume and NT identity page are checked in restoration. The policy and identity of all package files also need to match, including the hash of the executable controller. The checkpoint file has internal checksum and SHA-256 on the receipt; these hashes check integrity, do not constitute the signature of an emitter.

For basic USER32, an exclusive supervisor channel captures windows/controls, text, geometry, styles, button/selection status, focus, opaque IDs, events and counters. These operations do not belong to the accepted VM command list. The host broker blocks them even if a guest tries to forge the order.

Resumption creates a candidate VM and a new restricted helper. Rebuilds only the windows belonging to the session, preserves the IDs used by the guest and restores memory; no HWND or Windows pointer is serialized for VM. When recording the checkpoint, the original windows are closed. A refused restore does not change the original session or checkpoint.

Basic USER32 offline checkpoints, x86 and x64 are accepted. **GDI, DirectX, audio, pixel presenter, open connections, active USER32 timers and pending sents have no restore in this batch** and are refused. The family of frameworks remains executable without checkpoint. Normal security status export has 64 KiB; it does not replace the full execution checkpoint.

## Usage

Ready/enroll an x86 program:

```powershell
python -B tools/unified_runtime.py --build BUILD --config Release --application APP.exe --registry REGISTRO --work PREPARACAO_NOVA
```

For x64, providing `--reference CRT_LOCAL --schema-reference SCHEMA_LOCAL`; `--family user32` selects basic USER32, and `auto` selects the x64 family of frameworks. Imports need to be attended by the package chosen.

In the scanner configuration, add `"sandboxRuntime": "CAMINHO_REGISTRO"`. Relative paths are resolved in relation to the configuration:

```powershell
guardian-scan.exe scan APP.exe --config scanner.json --json
guardian-scan.exe sandbox-next --config scanner.json --json
guardian-runtime.exe --input APP.exe --runtime-root REGISTRO --output SESSAO_NOVA
guardian-runtime.exe --input APP_GUI.exe --runtime-root REGISTRO --output PAUSA_NOVA --checkpoint-after 12
guardian-runtime.exe --input APP_GUI.exe --runtime-root REGISTRO --output RETOMADA_NOVA --restore PAUSA_NOVA/recovery.json
```

`--checkpoint-after` counts completed USER32 orders; the point depends on the application. Zero output with `checkpointCreated` means recorded checkpoint, while paused analysis remains inconclusive. Resuming execution needs to complete its own scenario to produce `complete: true`.

## Validation and reproduction

**Windows/WHP in 29/10/2026:**Release **23/23** and Debug **23/23** with stable source code/binary. The ASan verifier approved nine targeted checks and 2,000 mutations per configuration. The matrix includes the actual path of the queue and resumed with text confirmation, click and file/registry. status

`tools/validate_unified.py` only covers this phase and dependencies used: queue entry, absence of fallback, independent console, WinRT family, checkpoints/resume x86 and x64 with input directed to the auxiliary, preservation of file/registry and authorship, profile/architecture/volume adulterated, unauthorized host assistant, checkpoint of another entry/policy, corruption, deadline, module policy and GDI checkpoint refused. The native state verifier uses ASan and 2,000 mutations that are not executed as code.

```powershell
cmake --build BUILD --config Release --target guardian-runtime guardian_unified_probe guardian-scan guardian-engine-service guardian-checkpoint-tests
python -B tools/validate_unified.py --build BUILD --config Release --reference CRT_LOCAL --schema-reference SCHEMA_LOCAL --corpus CACHE_LOCAL --asan-runtime ASAN_LOCAL --output PROVA_NOVA
```

Repeat in Debug. Preparing the proper binaries of the families used remains necessary in a new build. The packager requires current stable proofs of the two configurations. The extracted ZIP is validated within the same scope and checked before/later; RAM snapshots, Microsoft and source/binary references of the external corpus are not redistributed. The tests do not update the installed services or require administrator prompts.

## Continue

Snapshots with GDI/DirectX/audio features, connected transport and complex native dialogues remain pending. Wide compatibility, proportion of applications and detection effectiveness require the respective corpus; this delivery does not declare these goals achieved. Phase 14.14 is not automatically initiated.
