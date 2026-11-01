# Phase 12A — Hardware profiles and reference driver

**Completed for the reference matrix in 19/10/2026.** The Windows/WHP and Linux/KVM checks were approved with the retests registered below.

The delivery closes the 12A for the matrix described below. Phase 12 remains open; 12B-12E and phase 13 have not been initiated. Initial delivery and its results remain in `build-phase12a/`; the evidence of this conclusion remains in [validation.json](../build-phase12a-complete/validation.json).

## Implemented matrix

| Item | Behavior validated | Limit |
| --- | --- | --- |
| Profile by VM | Editable models of CPU, GPU, disk, BIOS and motherboard; persistent synthetic IDs | Requested model and effective capacity are distinct fields |
| CPU | Name up to 48 bytes in CPUID, topology of a vCPU and same identity in firmware/log | Preserve instructions and resources real; virtualization remains visible |
| Firmware | Table SMBIOS 3.2 synthetic, RSMB consultation by `GetSystemFirmwareTable` and MiniNt adapter | Do not implement a full PC, PCI or firmware boot |
| Registration | 15 value projection including CPU, BIOS, motherboard and effective devices | Profile values are read-only; general Win32 enumeration remains limited |
| GPU and disk | BGRA own and 64 KiB block device in their contracts | Select GT 1030 or other model does not implement this hardware/driver |
| Driver | Project reference x64, `ROOT\GUARDIAN_PROBE`, INF System, declared target NT 10 or 11 | ABI experimental software driver; without complete WDM/PnP/KMDF/WDDM |
| Resources | Driver running with 16, 32 and 64 MiB; a vCPU | RAM, cores and disk expansion belongs to the next deliveries |

The NT target is a profile compatibility declaration, not an integral implementation of that Windows version. The name chosen for CPU does not emulate a different architecture or set of instructions.

## A source of identity

The v1 inventory in GPA `0x138000` remains the common source. The page is read-only in the physical permissions of the VM. The kernel gives size, version, fields, topology and CPUID name before publishing the hash of the read inventory.

The same content generates the registration values and SMBIOS structures of BIOS, system, motherboard and processor. UUID and serials come from the synthetic identity of the profile. Host reference reads template fields, without consulting disk serial fields, firmware UUID, MAC, credentials or personal files. An unavailable field keeps the Guardian model.

The CPL3 test program calls the firmware API, consults the registry and compares the CPU with CPUID. It checks size queries, small buffer, part reading and write refusal. In Windows, another test performs real syscalls from the MiniNt adapter to query registration, firmware, and the fields implemented from RAM/vCPU. `MiniNt::ConfigureHardware` explicitly receive the inventory; the historical utilities without this adapter did not automatically become catalog profile managers.

## Driver import and execution

Historical `.sys` import remains inert. INF v2 packages preserve optional INF, image and catalog, validate limits and re-drift metadata when opening the package. The implementation checks architecture, NT decoration, dependencies, device, service and imports. Paths that escape the package, missing files, divergent metadata and unsupported actions are refused or are diagnosed with incompatibility.

CAT only receives limited inspection of DER SignedData envelope and inert storage. **Certificate signature, catalog membership and certificate trust are not authenticated. CAT packages are ineligible for execution in this laboratory policy.**Checksum controls integrity; does not authenticate or classify security.

The matrix executable package is `guardian-hardware-probe.inf` with `guardian-hardware-probe.sys`, compiled from the project sources. It imports four operations from the own kernel API: creation/exclusion device and allocation/release pool tag. The API is a separate module loaded into the guest; it does not use the `ntoskrnl.exe` from Windows host.

The `hardware` contract starts `guardian-kernel-hardware.gk`. The guest loader maps and protects the two PEs, resolves imports and calls the actual `DriverEntry` of `.sys`. The driver creates resources, performs four DWORD writings and four readings on the virtual device and performs its unloading routine. The guest checks results and release of resources; the host witnesses the operations of the device and refuses a functional claim without these operations. This test is proof of the operation of the reference, not attested to the reliability of an arbitrary driver.

States have separate meanings:

- **Imported:** copied and audited package; no code was executed.
- **Configured:**package/API selected in `installed.gdi`; still no boot proof.
- **Installed:**loaded image and imports linked in this experimental boot in memory.
- **Inicializado:** `DriverEntry` He's back. success.
- **Functional:** operations provided for in the contract concluded with proof of the device.
- **Unloaded:** Unload routine released the features and the loader removed the images.

There is no complete SCM/PnP installer or persistent registry of Windows services in this contract. The `drivers` report retains the specified boot observation and shows download separately. The reference with `DriverEntry` failed proves that import/loading did not see boot success. No imported driver is installed or executed in the host.

## Snapshots and clones

Snapshots link identity, CPUID, inventory, options, driver/API images, memory protections and device status. The test pauses after a writing, with pending response, restores to new candidates and completes operations and unload. Image changes, descriptor, profile and permissions are refused.

Clones preserves the identity to maintain consistency with your checkpoint, copy the selected package and need to produce their own execution observation. Hardware editing or installing another package requires VM stop, no named checkspoints or snapshots. Snapshots depend on the supported, profile, images and CPU format compatible; there is no promise of migration of mid-review checkpoints. Previous files are not automatically deleted.

## Use of the reference

In the binary folder of the chosen configuration, keeping `guardian-driver-api.gkd` next to the manager:

```powershell
guardian-vm --root D:\VMs create teste caminho\guardian-kernel-hardware.gk hardware
guardian-vm --root D:\VMs hardware-config teste --nt win11 --cpu "CPU de referencia" --board "Placa virtual"
guardian-vm --root D:\VMs driver-import teste referencia caminho\drivers\guardian-hardware-probe.inf win11 board
guardian-vm --root D:\VMs driver-install teste referencia
guardian-vm --root D:\VMs start teste
guardian-vm --root D:\VMs status teste
guardian-vm --root D:\VMs drivers teste
```

`driver-install` prepares the package for the next boot. Execution states appear after the guest effectively executes the test. The same flow exists in the Linux ELF manager.

## Validation

| Platform/configuration | Different end result | Evidence |
| --- | --- | --- |
| Windows Debug | 59 validated tests, including retests | Suite complete and retestes preserved |
| Windows Release | 59 validated tests, including retests | Suite complete and retestes preserved |
| Linux Debug | 39 validated tests, including retests | Guests LLVM natives and host GCC/KVM |
| Linux Release | 39 validated tests, including retests | Guests LLVM natives and host GCC/KVM |
| Linux AddressSanitizer/UndefinedBehaviorSanitizer | 27/27 approved | Cores, parsers, network and the seven hardware tests |
| Artifacts do guest | 16 images per configuration in each toolchain | Manifestos and hashes in the report |
| Validation processes remaining | None | `process-cleanup.json` |

The complete suites and retests are preserved separately. A clean suite of 59/59 or 39/39 is not assigned to attempts that have failed: the structured report associates each final result to its execution. On Linux Debug, they passed 37/39 before network repairs; network cases were repeated. In Release, they passed 37/39 before API loading correction in the driver's negative scenario; these two cases were repeated in Debug and Release. The sanitizers passed clean, 27/27.

The complete Windows suites and retests also remain separate. The first Debug passed 58/59: the MiniNt adapter still allowed to open an empty key removed device; the configured profile started to refuse missing keys. The first Release passed 58/59: catalog exceeded 60 seconds during concurrent execution with other tasks. The isolated retest completed in 22 seconds; the final pass also approved catalog. No assertions were removed. The overall manager limits remained unchanged. The scenario of two laboratory VMs explicitly uses the budget already available 30 seconds due to the concurrent durable capture; short-term CPU tests remain active.

The comparison of the 32 MSVC images confirmed identical executable sections after the exclusive address read-out adjustment for LLVM. Subsequently, the two images of the hardware contract were updated and revalidated to explicitly load the API, including drivers without import after optimization. Windows final retests cover the ten MiniNt tests, the seven hardware, snapshots, network and catalog; additional tests run the driver on the NT 10/11 targets and RAM limits.

On Linux, initial validation with MSVC images revealed HLT dependence on the MSVC test. I/O, non-repeatable preparation of a test directory and time failures under competition. The test started to explicitly close after the three transfers, the preparation gained directory by execution and concurrent network startup gained barrier. Additional diagnosis identified the lab switch blocking expiring during durable publications. The acquisition wait has gone from 100 short attempts for a two-second monotonic window with VM diagnosis. A safe test for locking by 250 ms and proves recovery; another keeps the lock and confers limited refusal. 18 which truncated CR2; reading now has ABI explicitly 64 bits. The actual CPU test has again checked address failure 4 GiB. Optimized compilation of the negative driver removed its imports; kernel API is now explicitly loaded before the driver, ensuring that the test observes the actual return of DriverEntry, instead of a previous module resolution failure.

The Linux final images are recompiled on Linux itself with LLVM; the host executables are ELF compiled with GCC. The guest's PE/COFF formats do not depend on Wine, QEMU, Windows SDK or running Windows programs on the Linux host. KVM maintains the limitation already documented: NX depends on the guest's tables; there is no second-stage independent execution protection in this API.

Windows commands for a tree of its own:

```powershell
cmake -S . -B build-12a -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DGUARDIAN_BUILD_GUEST_CONTRACT=ON
cmake --build build-12a --config Debug --parallel 4
cmake --build build-12a --config Debug --target guardian_guest
ctest --test-dir build-12a -C Debug --output-on-failure
#  Repeat build, audit and release tests, no competing tests.
```

To recompile guest and host on Linux:

```sh
python3 tools/build_guest_linux.py --llvm /usr/lib/llvm-18 --config Debug --output /caminho/absoluto/guest-debug
cmake -S . -B build-12a-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DGUARDIAN_TEST_KVM=ON -DGUARDIAN_KVM_GUEST_DIR=/caminho/absoluto/guest-debug
cmake --build build-12a-debug --parallel 4
ctest --test-dir build-12a-debug --output-on-failure
```

Dependencies used: CMake, Ninja, C++20 compiler, Python 3, OpenSSL, SDL2 and LLVM with `clang`, `lld-link` and `llvm-ml`. Access to KVM/NX should already be available. Debug and Release use their own directories. The structured report contains hashes of source code/binary and test results; [changes.patch](../build-phase12a-complete/changes.patch) allows you to review delivery from the snapshot prior to 12A.

## Limits and Stop

Commercial drivers, direct access to physical devices, GPU NVIDIA/AMD/Intel, WDDM, KMDF, full PnP and CAT authentication are not declared supported. The 167 missing imports from Flappy were not implemented in this batch, and the program received no compatibility statement.

The 12A is a tested reference matrix for further development. General Windows compatibility, larger features and additional realism assessment continue in future deliveries. The execution is terminated in this subphase before 12B.
