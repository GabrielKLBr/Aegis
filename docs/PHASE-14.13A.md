# 14.13A — Automatic preparation and integration of runtime

Delimited continuation of 14.13 in 29/10/2026. The profile remains NT 10.0.19045, with partial contracts of DLLs already described in the previous stages. This delivery does not start the broad acceptance of 14.14.

## Behavior

An EXE without individual registration can use one of the local templates provided. The controller reads its PE32/PE32+, confers normal and delayed imports, names, ordinals and export forwardings. Selects an architecture-compliant model, rebuilds the volume in memory with the exact bytes of the EXE and remakes the integrity links. The original catalog remains unaltered.

The three standard models cover the clipping of frameworks/CRT/GDI x64, basic USER32 x64 and compat32. The first includes the selected dependencies of MSVCP, MSVCRT, WinMM, shell and controls. This provides DLLs for resolution; does not make your APIs complete. A function may exist in another package and not be available in the selected model. The `unresolved` list, limited to 128 entries, identifies the unavailable requirements. Dynamic imports still depend on validation during execution.

There is no compilation, Python, or search for DLLs on Windows during the analysis. There is no EXE execution or upload of your DLLs on the host. The only helpers started are still known executables next to the controller, with the hash expected. Application auxiliary files and particular DLLs are not automatically copied from the chosen folder; they require a prepared package.

On the Sandbox page of the app:

- **Analyze a file** prepares and executes for the service;
- progress shows connection, preparation, conference and execution;
- **Cancel** interrupts the connection and closes the job with the session processes;
- **Save point**starts an EXE with basic USER32 and saves after 12 requests
  native, or when reaching the five-second window;
- **Retake point** reuses the last saved point, or allows you to choose
  `recovery.json`. The required payload is stored as `payload.bin` next to it.

The **Bundle button prepared** keeps the previous explicit operation of the 14.10G. It is not a fallback for EXEs that failed in the unified runtime.

Saving and resuming are operations that are specific to the service protocol. Automatic restoration preserves the model used originally, including when another model would also resolve EXE imports. The controller's identities, catalog, application, policy and snapshot remain mandatory.

The installer copies the catalog to the services protected versioned folder, with member limits and size. Without explicit `sandboxRuntime`, the controller uses `runtime` next to the executable. The explicit configuration continues to take precedence. The service runs the restricted worker in the user session.

The external request accepts up to 60 seconds, but the kernel execution is limited to 30 seconds and informs `executionBudgetMs`. Save/resume use temporarily up to 1536 MiB per process and 2048 MiB per job to accommodate copies of the 256 MiB RAM during serialization. Normal analyses keep their quotas.

## Local preparation

After compiling the dependencies of this delivery, provide a new directory:

```text
python tools/provision_runtime.py --build BUILD --config Release --registry BUILD/Release/runtime --work WORK --reference CRT_LOCAL --schema-reference SCHEMA_LOCAL
```

Microsoft references should be local and legally obtained. The catalog generated does not go to the public repository or build file of this delivery. The existing installer uses the catalog next to the service package. New versions of helpers or kernels require re-provisioning.

```text
guardian-runtime --input PROGRAMA.exe --runtime-root CATALOGO --output NOVA_SESSAO
guardian-engine-service --sandbox PROGRAMA.exe
guardian-engine-service --checkpoint PROGRAMA.exe
guardian-engine-service --resume CAMINHO/recovery.json
```

## Batch validation

`tools/validate_runtime_integration.py` checks only the altered flows and dependencies used: **20/20 Release and 20/20 Debug**. Includes provisioning, malformed PE, reconstruction under ASan, x86 and CRT x64 without individual registration, checkpoint/restoration of GUI, integrity, crossing paths, refusal of GDI checkpoint, deadline, scanner queue and independent programs. There are 1500 mutations and 192 truncations per configuration in the PE reader test.

`tools/validate_runtime_services.py` verifies **11/11** cases in the services installed in Release: current hashes, catalog/helpers, worker restrictions, automatic package, progress, save/resume, cancellation, independent controls, antivirus and stability of source code/binary. The two services were effectively updated. The interface was compiled in Release and Debug; there was no automated battery of clicks across the entire interface.

`tools/package_runtime_integration.py` requires these current proofs and generates the file for the private builds repository. The extracted package goes again through the 20 batch cases in both settings, with local references. The complete suites of the old phases have not been repeated.

## Real programs and next blockages

The local EXEs have been kept unchanged. Their exact identification is contained in the SHA-256 in the private results. Partial compatibility does not count as completed execution:

| Program | Result of this round | Next block observed |
|---|---|---|
| Hello World ed C independent | Finished no MiniNT x86 | Small corpus, compilation described in 14.12 |
| 7-Zip, `7z.exe` | Refused before execution | OLEAUT32 ordinals, token APIs, console, files and processes |
| Notepad, `notepad.exe` | Refused before execution | Dialogues, menus, controls, contracts API-set and printing |
| Flappy Window 1.5 | Loaded and executed partially; inconclusivo | `settings.txt` missing from CRT volume and limitations, including `HeapSetInformation` |

Proposed order for the following work: explicit packaging of the observed CRT auxiliary files and blockages; resolution of contracts/ordinais and central APIs pointed out by the corpus; continuity of 14.12 to x86 and GDI; expansion of corpus and profiles in 14.14. Each new step requires its own acceptance.

GDI/DirectX/audio live, timers or pending sents and connected network remain outside the checkpoint contract. A complete result continues to mean execution completed within the declared coverage, without certifying that the file is secure or that all DLL functions have been observed.
