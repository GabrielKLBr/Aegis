# Build and run native to Linux

Update: 18/10/2026. Request for this continuation: Build for Linux as well, avoiding Wine. Windows/MSVC/WHP support remains in the project.

**Continuation after enabling NX:** now there is KVM executable and recompilation of the guest images on Linux. Use, limits and matrix: [LINUX-KVM.md](LINUX-KVM.md).

## Available

`guardianav` is an executable **ELF x86-64**, with Linux input point. Uses OpenSSL 3 for SHA-256 and POSIX interfaces for reading. Do not start Wine, DLLs, parsed executables or subprocesses to calculate hashes.

- recursive scanner with SHA-256 and substring, with `signatures.txt` format
  existing and the internal EICAR test rule.
- Hash SHA-256 of entire file, including UTF-8 names.
- Static inspection of `.gk` images by the existing parser without initializing VM.
- Native compilation of the same memory algorithms, staggering, processes,
  handles/rights, I/O, volume, runtime, loader PE and unwind used in the guest.

The scanner reads only the files. A lack of correspondence means that the rules consulted did not match; it is not general security rating. There is no automatic quarantine, directory monitoring, integration with Security Center or graphical interface in this Linux version.

## Build

Dependencies: Linux x86-64, GCC/Clang with C++20, CMake 3.22+, make, Python 3.9+ and OpenSSL 3 with development headers. In Mint/Ubuntu, the usual packages are `g++`, `cmake`, `make`, `python3` and `libssl-dev`.

```bash
./Build-Linux.sh Release
./Build-Linux.sh Debug
./Build-Linux.sh Sanitize
```

The binaries are in `build-linux/<configuração>/`. `Sanitize` uses Debug with AddressSanitizer and UndefinedBehaviorSanitizer. Windows trees and previous evidences are not replaced.

In this machine, CMake and OpenSSL headers were prepared in the user cache, in `~/.cache/guardian-native-deps`, without administrative installation. The script recognizes this cache; in a common installation it uses the system dependencies. It also accepts `CMAKE`, `GUARDIAN_OPENSSL_ROOT`, `GUARDIAN_BUILD_JOBS` and `GUARDIAN_LINUX_BUILD_DIR` for alternative installations.

The Windows stream continues to be `Build-Kernel.ps1`, with CMake 4+ and MSVC. CMake chooses the flow by the target system; it does not run a Windows compilation below the Linux compilation.

## Use

```bash
./build-linux/Release/guardianav --capabilities
./build-linux/Release/guardianav scan "/caminho/da/pasta"
./build-linux/Release/guardianav scan "/caminho/arquivo" --json
./build-linux/Release/guardianav scan "/caminho/arquivo" --signatures signatures.txt
./build-linux/Release/guardianav hash "/caminho/arquivo" --json
./build-linux/Release/guardianav inspect-kernel "/caminho/imagem.gk"
```

The default base is searched next to the executable or `../share/guardianav/` for the installation. It does not depend on the current folder of the terminal. The package in `dist/guardianav-linux-x86_64-20261018/` includes the base and instructions.

| Output code | Result |
| --- | --- |
| 0 | Reading completed, no matches; or hash/inspection operation completed |
| 1 | One or more signatures coincided, without incomplete reading/coverage |
| 2 | Arguments, signature base or invalid configuration |
| 3 | Reading error, ignored input or partial coverage, including if detection occurred |

Content search covers up to 4 MiB; SHA-256 covers the entire file. Larger file without hash matching is marked as partial. Symbolic links, FIFOs and other special files are ignored in the scan and rejected in the hash, without waiting for data from a pipe. Insufficient permissions and changes detected during reading produce error. There is a depth limit of 128 levels. Names with bytes of control are escaped in the reports.

The base has up to 4 MiB, 10,000 rules and 4,096 bytes per rule line. Invalid rules cause explicit error in this Linux implementation. The results are issued during scanning, without hoarding the entire tree in memory.

## Validation and scope

```bash
python3 tools/validate_linux_native.py
#  Full acceptance of the current step, with KVM:
python3 tools/validate_phase9.py
```

The verifier requires Linux x86-64, compiles Debug, Release, and Sanitize and requires **20 CTests per configuration** (host algorithms, scanner, and disk; the actual KVM test is added by the separate verifier), either flawless or ignored. Checks that the executables are ELF x86-64, records native libraries and hashes from sources and artifacts, and requires capabilities compatible with this subset.

Historical result prior to the KVM backend: the 17 CTests passed in each of the three settings (51 executions, no faults or ignored). The CLI test approved 89 checks; the PE parser approved 1.353; the search for handlers kept the 3.059 checks. The package was extracted in temporary folder and executed capabilities and scan with the base located next to the executable.

Evidence: `build-linux/validation-20261018-032855-683697/validation.json`, with logs per configuration, hashes and identification of ELF executables. The previous Linux stream originals are in `snapshots/codex-before-linux-native-20261018/`.

Scanner tests compare SHA-256 to `hashlib`, cover block borders, large files, content/hash signatures, paths with spaces/Unicode, recursion, inaccessible entries, links, FIFOS, JSON and bytes preservation. Sentinels of `wine`, `wine64`, `cmd.exe` and `powershell.exe` are created in the PATH of the test to detect any undue call.

Three own frozen images in `test/fixtures/native/` are used only as data in parser tests. They have been copied from previous MSVC outputs and originate from and SHA-256 in the local manifest. No PE code is executed in these parser tests. The separate KVM matrix compiles the images and executes the PEs itself within the guest.

Unwind tests expressly state that the Windows oracle has not rotated. Algorithms and parsers tests do not replace kernel execution in CPL0/CPL3. After NX enablement in BIOS, the KVM backend has been implemented and executes its own kernels. `--capabilities` distinguishes compiled support (`vmExecution: true`) from local availability (`vmAvailable`). Physical protection and validation differences are in [LINUX-KVM.md](LINUX-KVM.md).

The remaining implementation of phase 8 was completed for the console matrix itself: exceptions in CPL3, non-local unwind and dynamic DLLs. Linux/KVM validation passed with 20 CTests in each configuration (including the KVM test), 132 real bootes and four expected refusals. Native Windows acceptance of this review remains pending; phase 9 was not started. Evidence and limits in [PHASE-8-COMPLETION.md](PHASE-8-COMPLETION.md).

## Continuation of Phase 9

The Executor `guardian-vm` adds full snapshots, management, cloning and recovery. The suite without VM now has 20 CTests; KVM matrix has 23 by configuration. Current state, commands and limits in [PHASE-9.md](PHASE-9.md). The phase 10 It wasn't initiated.

The complete matrix of phase 9 passed on Linux: 69 CTest plays, including ASan/UBSan and actual execution, plus regression of the previous 11 images. Evidence: `build-phase9/validation-20261018-103055-f78507d1/validation.json`.

## Phase 10 continuation

Politics, parser IPv4/IPv6, simulated services, laboratory and UDP by relay have common sources for Windows and Linux. `Build-Linux.sh` without KVM executes 22 CTests and produces 22 ELF executables, including new compiled tests. The matrix with KVM enabled has 27 CTests and is executed by `tools/validate_phase10.py`. Results and commands on [PHASE-10.md](PHASE-10.md).

Final acceptance of the phase 10: 81 executions CTest and 132 Regression boots approved on Debug/Release/Sanitize, without Wine/QEMU. Evidence and limits: [PHASE-10.md](PHASE-10.md). Native Windows acceptance remains pending.

## Continuation of phase 11

The current stream adds `guardian-gui`: native viewer of own applications within the guest. It is not the full interface of the antivirus. With available development SDL2, there are 24 CTests without KVM and 32 with KVM; the inventory without KVM contains 24 ELF executables. The current gateway is `python3 tools/validate_phase11.py`. Acceptance has passed: 96 CTest plays and 132 regression Boots in Debug/Release/Sanitize; pending Windows Gate.

The development machine uses SDL2 headers extracted in the cache without changing the installation of the system. In a normal Linux installation, `libsdl2-dev` enables the same compilation; the package uses the native SDL2 library of the system. Instructions and limits: [PHASE-11.md](PHASE-11.md). There is no Wine/QEMU.
