# Phase 12D — long sessions, reboots and interaction

**23/10/2026 Windows Update:**The current matrix has passed on MSVC/WHP: 80 distinct tests Debug, with documented retests, and 80/80 Release in a complete suite, without ignoring. [Corrections and Evidence](WINDOWS-PHASES12B-13-REVIEW-20261023.md). The Linux records below are historical; the scope and comparison pending versions remain open.

**State in 21/10/2026:** Linux implementation and acceptance approved in stable revision `acceptance-20261021-full-01`: **53/53 Debug, 53/53 Release and 33/33 ASan/UBSan**, 139 CTest executions, no faults or ignored tests. 392 source files remained stable throughout the matrix. The explicit authorization of this subphase follows 12C. The product continues Windows/MSVC/WHP; local compilation and testing are Linux/KVM The 12E was subsequently authorised: [PHASE-12E.md](PHASE-12E.md).

The state prior to 12D is preserved in `build-phase12d/baseline-20261021/original-source.zip`, with hashes in `baseline.json`. Temporary builds are in `/home/gabri/Projetos/GuardianAV-12D-build-20261021`; code, documentation, delivery and selected evidence are in the original project folder on the HD.

## Behavior delivered

- Duration of 100 ms up to seven days per session, with recovery points each
  100–30,000 ms running. Reaching the duration suspends the VM and releases the executable process; `resume` opens another session from the saved point. The old limit remains available for previous catalogs, which maintain its configuration.
- `suspend` saves memory, processors, disk and device status and
  `pause` maintains the resident executable. Resumption in another process and cloning preserve the applications, waits and position of the script. Recordings remain atomic and subject to VM quotas.
- `restart` does a new boot: persistent volume conserves files/log,
  and the program recovers its recorded progress. RAM and uptime restart; a disposable disk remains disposable. To save RAM, use suspension/resume or restore a full snapshot.
- Screenplays up to 256 events contain time, ASCII key, click, move,
  loss of focus or closing. They are imported into the configuration and included in the snapshot; changing the policy does not allow silently restoring an old session. Entry is forwarded to the graphical device and guest windows. There are no commands to select windows/applications from the host.
- The native viewer offers manual interaction and suspension/resumption.
  Reopening redesigns the saved framebuffer and clears the guest's focus; host system input queues are not persisted. Event filters from the window itself remain active. Scripts do not simulate entry through the host operating system.

## Time and effect on analysis

The only mode available is `real`. Each stretch uses the real monotonic clock; QPC, ticks, virtual UTC and `Sleep` deadlines keep the 12C semantics. While VM is suspended, or the host records a checkpoint, the guest's active time gets frozen. A resume preserves the uptime/anchor UTC and pending deadlines. Restore a previous snapshot restores that same state, including your watch. The host clock is never changed.

The budget uses the guest execution time, excluding the copy/save of RAM. The expiration of a snippet requires pause, without treating cancellation as a failure or as a program completion. The accumulated snapshot limit goes from one hour to ten years, with overflow/order checks; the duration per session remains limited to seven days. No continuous seven-day trial has been performed.

`session.json` records duration, checkspoints, active time, time elapsed in the host and output reason. `graphics.json` records planned/effective time of each event and maximum delay, allowing to measure the effect of pauses and recording on the analysis. Host times cover the execution loop and its checkpoints; preparation/import/restoration prior to the loop stay out of this measure. The session file describes the latest execution; acceptance reports preserve the initial and resumed snippets. No event is anticipated to shorten execution. Acceleration and other experimental virtual time modes are not implemented; ask `--time-mode` other than `real` is explicitly refused.

## Usage

On Linux, use the ELF executables and kernels of the 12D package. On Windows, use the same commands with the executables natively compiled by MSVC. These examples only use the own graphical application and catalog files:

```text
guardian-vm --root CATALOGO create exemplo guardian-kernel-graphics.gk graphics --ram-mib 32
guardian-vm --root CATALOGO session-config exemplo --duration-ms 120000 --checkpoint-ms 5000 --script config/session-example.txt --time-mode real
guardian-vm --root CATALOGO start exemplo
guardian-vm --root CATALOGO suspend exemplo
guardian-vm --root CATALOGO resume exemplo
guardian-vm --root CATALOGO status exemplo
```

A new duration can be configured after suspending, saving the script. Enabling the session mode or changing the script of an old VM requires a new VM without checkpoints/snapshots, avoiding an incompatible restore. The script schedules are absolute from the start of the boot; the resume follows the saved position and a new boot restarts the script. The example file ends in 60 seconds, even when the configured budget is larger.

```text
guardian-gui guardian-kernel-graphics.gk SAIDA --interactive --duration-ms 120000 --checkpoint-ms 5000
guardian-gui guardian-kernel-graphics.gk SAIDA --interactive --resume --duration-ms 120000
```

Expired duration preserves `SAIDA/session.gvs`. For script, replace `--interactive` with `--script config/session-example.txt`, informing the same script when resuming. Normal completion removes checkpoint from that viewer. Each output directory admits only one executable at a time.

## Validation

The matrix reruns the 132 12C CTests in the 12D review and adds seven specific executions of this subphase (three Debug, three Release and one Sanitize). The entire settings were compiled and tested once on the same final review, without reusing the development results.

| Configuration Linux | Tests approved | Ignored |
| --- | ---: | ---: |
| Debug/KVM | 53/53 | 0 |
| Release/KVM | 53/53 | 0 |
| ASan/UBSan, core without KVM | 33/33 | 0 |

The 12C tests also repeated 1,590 invited observations (795 per KVM configuration), with no differences in the expected contract. The three native NT references remain missing; these results are not a comparison with Windows measurements. The Windows review only went through the cross syntax check of ten C++ units, without running a `.exe`, without MSVC and without WHP. The compilation identification of this check uses substitute values only to allow the reading of the units by the syntax compiler.

The three new tests cover:

1. Policies/limits, rejection of malformed events, unprepared deadlines,
   script position after restor, regressive clock and manual focus on reopening.
2. EP own programme in CPL3: progress persisted before `Sleep(1000)`,
   snapshot in the middle of the wait, restoration in new executable, pause of 250 ms in the host without completing the wait, and boot new recovering the volume progress.
3. Catalogue with successive durations, suspension/resume, cloning, reboots,
   modified policy rejection, real 31-second graphical session, oracle independent of pixels and native manual viewer restore. On Linux, this viewer test uses SDL with screenless test video; the event test checks the filtering of the window itself. It is not a human manual session in Windows.

### Measurements of actual execution

| Configuration | Event final no guest (s) | Host, sum of two loops (s) | Maximum input delay (ms) | Resumption of wait, host (ms) |
| --- | ---: | ---: | ---: | ---: |
| Debug | 31.000967 | 39.144 | 45.802 | 957.303 |
| Release | 31.000622 | 37.734 | 19.665 | 955.269 |

The closing event expected for 31 seconds was never anticipated. The final image of the two settings coincided with the independent oracle: eight pixels and the framebuffer integral hash, with SHA-256 of the PPM `4d26b5738133e4946a3d86aa6b7f246c25561d0a77df9f0c4c14ad4ded3c5c9b`. The wait for a second was paused after a 200 ms stretch, remained 250 ms without executioner and resumed with the deadline still pending. The program checked the time itself; the new boot recovered the progress persisted before waiting. The host times above include copies/checkpoints of the loop, as described in the time section; they did not include the pre-loop preparations.

### Evidence and Delivery

- Consolidated result: `build-phase12d/acceptance-20261021-full-01/result.json`.
- Inventory of 392 files: `source-inputs.json` in the same directory,
  SHA-256 `e51fe47fcb1daf5274d774c9379fb0738f1171ccba295367ae7b7fae069c39f2`.
- Configuration, compilation, JUnit and guest JSON reports logs
  are copied to this folder on the hard drive. The RAM images of the tests are only in the external build directory.
- Windows Differences and Checking: `build-phase12d/review/changes.diff`,
  `changes.json` and `windows-syntax-review.json`.
- Code: `build-phase12d/delivery-20261021/GuardianAV-12D-fontes-20261021.zip`.
- Linux: `build-phase12d/delivery-20261021/GuardianAV-12D-linux-20261021.tar.gz`.
  Contains tested ELF Release executables, 19 own kernels, drivers, example script and instructions. Guest data is not allowed to run in the Linux package.
- Integrity: `SHA256SUMS.txt` and `delivery.json` in the delivery directory.

Play full matrix in a new folder:

```text
python tools/validate_phase12d.py --output DIRETORIO_NOVO --llvm LLVM_NATIVO --openssl-root OPENSSL_NATIVO
```

On Windows, run in Visual Studio x64 terminal, omitting `--llvm` and `--openssl-root`. The validator requires native MSVC. Native references from 12C can be attached with `--reference ARQUIVO`, one per actual NT version.

## Limits and closure

Profiles continue with the base: the PE application and the chart are separate contracts; this does not implement Windows USER32/GDI, a complete Windows or transparent resume of arbitrary programs. The waiting program uses legacy volume; the EP integration with 12B scalable SMP/volume remains pending. The SMP path also receives suspension by deadline and the accumulated limit extended. They continue to be worth the RAM/storage quotas, eight VM-named snapshots, and the limit of 64 historical VM graphics from the catalog. Checkpoints within the same session reuse the history; they do not create a new one at each stretch.

The Windows/MSVC/WHP acceptance of this review and 12C's Windows references remain necessary. The 12B to-ed and previous ones are not closed by such authorization. The joint formal conclusion remains open until this validation. Stop at 12D; no 12E implementation has been included.
