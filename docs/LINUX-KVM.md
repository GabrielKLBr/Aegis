# Execution of the guest on Linux with KVM

Update: 18/10/2026. The NX has been enabled in BIOS and confirmed by Linux. `/dev/kvm` is now user accessible; the API has returned version 12. The Linux executable has been implemented and runs the kernels itself in CPL0/CPL3. The Windows product continues using MSVC/MASM/WHP.

## Build and execute

The host needs Linux x86-64, C++20, CMake, make, Python and OpenSSL 3. The guest can be recompiled on Linux with a native installation of LLVM containing `clang`, `lld-link` and `llvm-ml`. No Windows SDK or CRT is connected. In this machine LLVM 23.1.3 has been prepared in the user cache; it is not necessary to install it on the system. The compiler is not included in the execution package.

```bash
#  The tree itself preserves caches and evidence from previous builds.
GUARDIAN_LINUX_BUILD_DIR="$PWD/build-linux-kvm/Release" ./Build-Linux.sh Release
python3 tools/build_guest_linux.py --config Release

./build-linux-kvm/Release/guardianav --capabilities
./build-linux-kvm/Release/guardianav boot-kernel \
  build-guest-linux/Release/guardian-kernel-complete.gk storage \
  "$PWD/resultado-kvm.json" --disk "$PWD/demonstracao.gvd" "$PWD/demonstracao.gvo" --create-disk
```

`GUARDIAN_LLVM_ROOT` or `--llvm` select another installation of LLVM. `--output` selects the guest folder. The script generates the twelve production images, two failed images, the reference executable/DLLs and the headers that incorporate their bytes. Each build records the compiler version and the hashes of the images. The tools executed in the host are ELF.

PE/COFF remains the format of the guest images. The PEs themselves run exclusively within the VM. The scanner does not start the files you read. There is no Wine or QEMU in either stream.

The same executable is available as `guardian-kvm <imagem> <contrato> <relatório>`. Contracts are `basic`, `cpu`, `memory`, `threads`, `processes`, `io`, `storage` and `network`. Images `loader`, `runtime`, `lifetime` and `complete` use the `storage` contract and require explicit base/overlay. Zero return requires Hello/Diagnostic/Stop protocol, zero status at CPL0 and reports/counters of the selected contract.

Each VM has a vCPU and 16–64 MiB of RAM. `--ram-mib`, `--boot-id` and `--timeout-ms` select the limits published in the help. The maximum time is 30 seconds; a watchdog also closes guests who do not produce outputs. The JSON report records backend, status, timer, default, resource counters and disk results. base/overlay files are own data of 64 KiB; they are not physical disks. The base remains only for reading.

## Implementation and limits

`src/linux/KvmMachine.cpp` creates the VM directly by the KVM API, installs RAM, x64 records, CPUID with NX and the floating point initial state. The timer interrupts the execution by signal directed to the vCPU thread, meets the clock with the vCPU stop and injects the interruption only when the guest allows. HLT, expired time, over budget, unknown I/O and improper physical accesses are not converted to successful completion.

Boot uses the same `KernelBoot.cpp`, protocol validations and `IoBackend.cpp` from Windows. Windows snippets of the four changed shared files were compared with snapshot and preserve previous content. This inspection does not match a new compilation/test on Windows.

POSIX storage maintains the previous base/overlay format, with unique locking, base fingerprint, sequential records, checksum and incomplete tail recovery. Special files, end links, base and overlay with the same identity and already open overlay are rejected. Incomplete writings interrupt new operations; they are not published in memory as success.

There is a protective material difference: the public KVM memory API used here provides physical pages only for reading, but not an independent execution permission per page. NX is applied by the guest tables. JSON informs `secondStageExecuteProtection: false`. Therefore this backend serves the execution and validation of the own kernels; the physical protection equivalence with WHP is not declared. Fixed loader fonts remain unsearched in paths/DLLs of the host. No arbitrary EP execution was added.

The adaptation of the assembler is textual and occurs before generating objects: expands the MASM macros, adjusts Directives/labels and expresses the numerical offsets of FS/GS as absolute addresses. The output and input of each step stay in the build. There is no byte patch in machine code. `ClangIntrinsics.asm` provides the instructions declared by LLVM headers that this compiler does not expand.

Interface reference: [KVM](https://docs.kernel.org/virt/kvm/api.html) API official document.

## Reproducible acceptance

```bash
python3 tools/validate_phase9.py
```

The verifier creates a new folder, recompiles the host and the guest, requires available KVM and records failure if any steps are missing. Do not ignore tests:

| Matrix | Criteria |
| --- | --- |
| Debug and Release | 23 CTests by configuration |
| AddressSanitizer/UndefinedBehaviorSanitizer | 23 CTests including real boot and disk host |
| Kernels own | 11 images × 2 configurations × 3 sizes of RAM × 2 boots = 132 boots |
| kernel and double fault/IST failures | 4 executions refused as success, with valid fatal report |
| Status persistent | Second boot uses the same overlay, recovers the previous sequence and keeps the base intact |
| Applications PE | DLL/TLS markers, 31 workers, children, rollback, explicit output and actual stack capture |
| Executor KVM | 33 verifications, including arguments, session, tampered boot, deadline and device errors |
| Storage do host | 32 Persistence, isolation, corruption, blocking and recovery checks |

The evidence remains in `build-linux-kvm/validation-<data>/validation.json`, with logs, JUnit, hashes of source code/artifacts and individual reports. Patents in PATH detect calls to Wine, Windows or QEMU programs during validation. The manifest only approves the matrix if the sources remain equal to the end.

## Phase 8 status

The remaining implementation has been completed for the console matrix itself: exceptions in CPL3, continuation, non-local unwind and dynamic DLLs. The application audit, refusals and limits are in [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md). The native Windows acceptance of this review remains pending. Phase 8 remains open for such acceptance and phase 9 has not been started.

Historical result of phase 8: **132 bootes, four expected refusals and 60 CTest executions**, no failed or ignored tests. Evidence: `build-linux-kvm/validation-20261018-050639-287554/validation.json`. The updated package is `dist/guardianav-phase8-linux-x86_64-20261018.tar.gz`. Includes ELF executables, 13 production (11 images and two failure), signatures, documentation and manifests. Historical packages remain.

Previous result of the backend, preserved: 120 boot and 57 CTest plays on `build-linux-kvm/validation-20261018-041930-839301/validation.json`. The historical `dist/guardianav-linux-kvm-x86_64-20261018.tar.gz` package was extracted, checked by SHA-256 and executed two additional `lifetime` boot.

## Continuation of Phase 9

Full snapshots and executable management `guardian-vm` have been added. The current verifier is `tools/validate_phase9.py`; the command and counts of the previous delivery remain identified as historical evidence. registers/XSAVE/interruptions, 16/32/64 MiB, resumes, disk restarts, simultaneous VMs, failures and isolation. See [PHASE-9.md](PHASE-9.md).

Phase 9 passed 23 CTests per configuration (69 plays), 132 regression boots, 81 resumes/boots snapshots and 24 bootes by the manager. Evidence: `build-phase9/validation-20261018-103055-f78507d1/validation.json`. Native Windows acceptance remains pending.

## Phase 10 — Guest Network

Profile on [PHASE-10.md](PHASE-10.md). For direct testing, after builds:

```bash
./build-linux/Release/guardian-kvm build-guest-linux/Release/guardian-kernel-network.gk \
  network resultado-rede.json --network simulated --capture "$PWD/capturas" --timeout-ms 20000
```

`network` starts without network. `guardian-vm network` configures simulated services, laboratories and relays with explicit destinations. Current acceptance is `python3 tools/validate_phase10.py`; scripts/results of 8/9 phases are historical.

Final acceptance of the phase 10: 81 executions CTest and 132 Regression boots approved on Debug/Release/Sanitize, without Wine/QEMU. Evidence and limits: [PHASE-10.md](PHASE-10.md). Native Windows acceptance remains pending.

## Phase 11 — Own graphics applications

The `guardian-kernel-graphics.gk` image adds a distinct contract, `graphics`, with a proper CPL3 application and two tools/windows. The build generates 13 production images and two fatal controls. `guardian-gui IMAGE OUTPUT [--interactive]` features the output by software; the manager admits `create ID IMAGE graphics`, snapshots and clones of the deterministic script. The interactive mode is direct, without snapshots.

Current acceptance is `python3 tools/validate_phase11.py`; old validators are historical. See [PHASE-11.md](PHASE-11.md) for support, libraries, permissions and separation between Linux/KVM and Windows/MSVC/WHP. validation

Final acceptance of phase 11: 96 CTests, 132 regression boots and 45 boots/resumes approved graphics, with nine graphical pauses and ASan/UBSan. Evidence `build-phase11/validation-20261018-191149-4baab81b/validation.json`. The native Windows gate remains pending.
