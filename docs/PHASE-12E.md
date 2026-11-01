# Phase 12E — environment of use and synthetic data

**23/10/2026 Windows Update:**The current matrix has passed on MSVC/WHP: 80 distinct tests Debug, with documented retests, and 80/80 Release in a complete suite, without ignoring. [Corrections and Evidence](WINDOWS-PHASES12B-13-REVIEW-20261023.md). The Linux records below are historical; the scope and comparison pending versions remain open.

Phase 13 was later authorised in 22/10/2026: [registration](PHASE-13.md). The results and limits of this 12E delivery are preserved below.

**State in 22/10/2026:**implementation completed and accepted in the native Linux/KVM matrix: **56 Debug, 56 Release and 34 ASan/UBSan**, all approved without ignored. The product remains Windows/MSVC/WHP; was not used Wine or QEMU. The native Windows acceptance of this review and the pending 12B/12C phases remain open. Therefore, the technical result keeps `phaseComplete: false` and `prerequisitesComplete: false`; local acceptance of 12E does not meet these requirements. Phase 13 has not been started.

The code is in the original folder of the HD. The previous base is preserved in `build-phase12e/baseline-20261021/original-source.zip`, with 460 hashes files in `baseline.json`. The builds of this phase are outside the NTFS in `/home/gabri/Projetos/GuardianAV-12E-build-20261021`.

## Profile versioned

The `GuardianProfile/1` profile uses a 32-bit seed, v1 content and three x64 scenarios: Win7/7601, Win10/19045 and Win11/22631. Your synthetic `lab-SEMENTE` account corresponds to the actual guest token: user 100, group 10. There are no passwords, Windows login or accounts copied from the host. The profile page is read-only, linked to the hardware NT and validated in the restoration. Hardware and profile identity are synthetic and reproducible.

The installed catalog contains only **Guardian Docs 1**, a proper executable running in CPL3 and reading text/CSV by guest APIs. The executable is in the kernel's unchanging catalog with real bytes hash; a fake `.exe` is not created in the 512 bytes file volume. Catalog and log files declare that storage explicitly. Office, browser or other program is not declared that the profile does not run.

The documents are `C:\welcome.txt`, `C:\budget.csv` and `C:\notes.txt`. A proper shortcut `GuardianShortcut/1` opens the text by the registered association. This format is not a Windows `.lnk` file. `.txt` and `.csv` associations resolve for the reader actually executed by `CreateProcessA` in the guest. The history is the reader's own; it does not represent browser navigation.

All initial content is generated within the guest from the seed and the scenario version. The profile only receives typed parameters; it does not receive accounts, documents, credentials, personal directories or host navigation. No import channel is added at this stage; unsupported transfers continue to be refused by existing channels.

## Dates and evolution

Dates of creation, access and writing will be persisted in an identified extension of the volume header. The size of nodes, slots and existing commands remains the same. Old volumes with zero extension remain readable; old unknown dates are returned as zero.

`GetFileTime` and `SetFileTime` cover files with already supported subset handles, resolution of a second and representative dates in 32-bit positive Unix seconds. Precision/values special unsupported are refused. Zero pointer or FILETIME keeps the field, following the documentation of [SetFileTime](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfiletime) and [GetFileTime](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletime). Initial dates are explicitly set to compose the synthetic scenario; subsequent readings/written update dates by the actual guest clock. The host clock remains intact.

The final supply marker is confirmed along with files and record. The history records real reader openings, keeps a limited window and a accumulated counter. The persistent profile keeps notes and evolution in boot, clones and snapshots; the disposable starts again in a new boot. The playback of the initial base requires the same seed, scenario, version and image as the catalog.

## Validation and delivery

The stable review `acceptance-20261021-full-02` passed in the full matrix:

| Configuration | CTests approved | Failures / ignored |
|---|---:|---:|
| Debug | 56 | 0 / 0 |
| Release | 56 | 0 / 0 |
| ASan/UBSan | 34 | 0 / 0 |
| Total | 146 | 0 / 0 |

The specific tests of 12E verified:

- Core: 89 configuration checks, including version/seed,
  repetition, actual dates, rights, unsupported values and aborted commit recovery.
- Boots in Debug and Release: six pairs NT/seed, two runs each
  base, 12 resumes and 12 rejections of profile change by configuration. The whole initial volume was compared. In total, there are 24 new bases, 24 resumes and 24 rejections of altered profile.
- Python independent Oracle: bytes of documents, account, associations,
  catalogue against actually compiled PE and dates in volume. There were 514 checks in Debug and 584 in Release, including progress queries; the difference in counting depends on the duration of VMs.
- Opening of the three documents by the reader's actual processes; notes and
  history preserved in restart, clone and snapshot, correct rollback and disposable profile restarted without data from the previous session.
- personal canary created only in the absent RAM host and guest volume;
  Host control file intact. Personal base import by profile creation was refused. This test covers these channels and canaries without representing a general proof of absence of all possible personal data.

The 405 source input files remained identical during and after the matrix. `source-inputs.json` SHA-256 is `21ec810bb30a745b919eaa5a9191875ce6d27c419e3c7b907a757ed44cdf522c`. The first attempt was replaced before finishing, after Windows check detect a collision of the `Yield` macro in the order of inclusion of a test header. No partial result of this attempt was counted in acceptance. The final revision passed the cross-check of ten Windows units syntax. This check does not run `.exe` and does not equal to the MSVC compilation or native WHP execution, which remain pending.

Builds and matrix started in 21/10; directories and packages keep that date. In the original HD folder:

- Selected evidence: `build-phase12e/acceptance-20261021-full-02/`, with
  result, inventory, logs, JUnit and reports specific to each configuration.
- Revision of Windows changes and syntax: `build-phase12e/review/`.
- Source: `build-phase12e/delivery-20261021/GuardianAV-12E-fontes-20261021.zip`.
- Linux: `build-phase12e/delivery-20261021/GuardianAV-12E-linux-20261021.tar.gz`,
  with ten ELF tools, twenty kernels and the programs themselves as guest data. The kernel includes the catalog used during execution.
- Integrity and verification of delivery: `SHA256SUMS.txt`, `delivery.json` and
  `package-verification.json` in the same delivery directory. Additional verification extracts the package, executes the core and starts the profile by the delivered program, checking the opening of the three documents.

### Usage

```text
guardian-vm --root CATALOGO create-environment escritorio guardian-kernel-environment.gk --nt win11 --seed 42
guardian-vm --root CATALOGO environment escritorio
guardian-vm --root CATALOGO start escritorio
guardian-vm --root CATALOGO status escritorio
guardian-vm --root CATALOGO restart escritorio
```

Add `--disposable` in creation to restart every boot. Existing profiles do not receive data automatically. The seed and scenario are linked to VM; to another base create another VM. Changing the NT version of the hardware to diverge from the profile is refused. Session commands, snapshot, clone and restore from 12D remain available.

The boot console shows `12E DOC OPEN` three times and `12E DONE visits=N opens=3`. Text opening and validation of CSV occur in the reader process, with the identity of the guest account. History keeps the last 12 records and total visits; notes receive a line per visit, up to the actual limit of 512 bytes of the file. Lack of space or invalid document is explicit operation failure.

Play matrix in a new directory:

```text
python tools/validate_phase12e.py --output DIRETORIO_NOVO --llvm LLVM_NATIVO --openssl-root OPENSSL_NATIVO
```

On Windows, use Visual Studio's x64 terminal and omit Linux paths. The validator requires MSVC for Windows acceptance and saves comparisons and to-ed from previous subphases. Phase 13 is not part of this authorization.


## Preserved limits

The application profile uses a vCPU and the existing volume of 32 nodes, with 16 KiB of data and files up to 512 bytes. The expanded features of the 12B `resources` own contract have not been integrated into this profile. There is no Windows desktop, Office, browser or general compatibility with third party programs. The three NT versions are scenarios of the implemented subset. The reader, shortcut and history are unique and their formats are explicit. Import of accounts, credentials, personal folders, and browsing has not been added.

The completion of this delivery is the implementation and acceptance of Linux synthetic profiles of 12E. The next phase depends on a new request, without terminating Windows validations and the previous requirements still pending.
