# 12B resource contract — continuation of 20/10/2026

**23/10/2026 Windows Update:**The current matrix has passed on MSVC/WHP: 80 distinct tests Debug, with documented retests, and 80/80 Release in a complete suite, without ignoring. [Corrections and Evidence](WINDOWS-PHASES12B-13-REVIEW-20261023.md). The Linux records below are historical; the scope and comparison pending versions remain open.

The 12B remains open until the Windows/MSVC/WHP acceptance of the final review and the revision of the full scope. The 12C was later authorized in 21/10/2026: [matrix and current state](PHASE-12C.md). The reference implementation of the 12B is in `/home/gabri/Projetos/GuardianAV-12B-complete-20261020`; the previous delivery was preserved in `/home/gabri/Projetos/GuardianAV-12B-20261020`.

After user authorization, NTFS was disassembled, its diary and incomplete closing state were fixed and the volume was mounted with reading and writing. The accepted revision was copied to the original folder on the hard drive and the 368 hashes of the fonts were checked. The previous backup is preserved. Details in [Transfer to HD](HD-TRANSFER-20261020.md). No Windows execution has been replaced by Wine.

## What has been implemented

The new `resources` contract, with own image `guardian-kernel-resources.gk`, runs from 1 to 4 real processors in a single VM/partition, sharing RAM. KVM and WHP backends create each processor; CPUID features a socket, a thread per core and APIC IDs. The test executes a competing interlocked barrier and increments. Each core sets up its own MXCSR. Checkpoint captures loggers, XSAVE/FPU and events from each processor, with shared RAM captured once in persistent representation. Cancellation and pauses interrupt and collect all run threads.

The image declares its capability explicitly. Selecting the new contract for an old image is refused before initializing the VM. PE profiles, graphics, network, hardware, and the previous `storage` contract continue with their CPU matrix. There is no declaration of a multiprocessor PE scheduler in this continuation. This point needs to remain explicit in the scope review.

RAM accepts 16 to 1024 MiB. The maximum mode respects this implemented ceiling, available memory and operating system reserve. The global reserve per user remains common to catalogs, limits four workers and preserves a logical CPU in hosts with more than one CPU. The cost of the new contract is eight times RAM plus 128 MiB, plus four times the backing growth quota to cover snapshot materializations and replacement. The representation of the mapped blocks is also limited by the growth quota, keeping snapshot buffers within the reserve. The disk extraction of a checkpoint uses streaming when the RAM does not need to be restored. The old profiles retain their previous calculation without this additional disk cost.

The new own filesystem accepts logical capacity from 1 to 8 GiB, with standard of 2 GiB, 32 files in flat namespace and up to 256 MiB per file. It uses 64 KiB blocks, two metadata sets with checksum, COW publication, formatting intention and recovery of orphaned blocks after interruption. Extensions without data read zeros. The directory and log of the old filesystem were not silently converted to this format. Guest data did not view host paths.

Backing is sparse, with configurable physical quota from 4 to 256 MiB, 64 MiB standard. This quota limits the data that can be materialized in logical volume; logical capacity does not constitute promise of integral physical allocation. Aggregated storage control includes state, image, backing, active generations, snapshots and temporary. Preserves 64 KiB control and 256 MiB free in the host unit. Spacious files are charged by physical allocation; snapshots and common files by the largest measure between logical and physical size.

On Windows, measurement of sparse files uses `GetCompressedFileSizeW`, as [Microsoft](https://learn.microsoft.com/en-us/windows/win32/fileio/obtaining-the-size-of-a-sparse-file) documentation. The implementation NTFS validation remains pending. Host allocation units need to divide 64 KiB. Existing tablet backups, physical links, symbolic links and redirect points are not adopted as disks.

`resources` informs capacity, quota, physical allocation, total charge and CPU/RAM requested, admitted and provided. Filesystem measurements are obtained by only reading inspection of the metadata in the VM proprietary process, linked to the checkpoint generation. They are published when known and stable; otherwise, they appear as `null`, with `guestSpaceKnown: false`. Available is again limited by quota, backing, aggregated budget and current host space.

## Independent folder and recovery

`--disk-folder` chooses an existing folder. The manager creates a subfolder of its own with random identity and property marker. The chosen folder and its other files are preserved. Short internal names do not enter the VM enumeration, even when the chosen destination is the catalog folder. Without the option, the catalog keeps these subfolders in `.disks` outside the VM status directory.

With the VM stopped, `disk-folder` copies the persistent image to a new subfolder itself, scans reserves/quotas and publishes the last configuration. Durable migration intent allows you to recover a interruption by choosing the location referenced by the confirmed configuration. Origin and destination are checked by property before any cleaning. It is not allowed to create the disk within another managed disk or the state of a VM. The generation wiper validates its owner and refuses unknown directories, links, and files. Clones receive independent location and owner. The deletion of VM removes only its own subfolder.

The worker uses own generations in this location. If he dies, recovery preserves the active disk before removing the abandoned generation and resumes only a complete checkpoint. Checkpoint and backing are validated as the same generation. If there is no quota to publish the recovered image, the state remains in recoverable failure and the active generation is preserved. The status query and deletion of snapshots remain available to release space; a write failure does not clear the only recoverable copy. The test reproduces this block with an aggregate quota of 40 MiB, releases a snapshot, recovers the disk and resumes after adjusting the budget.

## Use in Windows

In an x64 terminal of Visual Studio 2022, at the root of the updated copy:

```text
python tools\validate_phase12b.py --output build-phase12b\windows-full-20261020 --generator "Visual Studio 17 2022"
```

The output directory needs to be empty or not yet exist. The script compiles Debug and Release with Microsoft compiler and executes all registered tests, preserving logs, JUnit, hashes from the fonts and the result of each configuration. It refuses changes in fonts during acceptance and does not end the phase alone.

Example with the compiled Windows executable and an existing disk folder:

```text
guardian-vm.exe --root D:\Laboratorio create recursos caminho\guardian-kernel-resources.gk resources --ram-mib 128 --vcpus 4 --disk-folder E:\DiscosGuardian --disk-capacity-gib 2 --disk-growth-mib 64 --storage-limit-mib 1024
guardian-vm.exe --root D:\Laboratorio resources recursos
guardian-vm.exe --root D:\Laboratorio start recursos --paused
guardian-vm.exe --root D:\Laboratorio snapshot recursos inicial
guardian-vm.exe --root D:\Laboratorio resume recursos
guardian-vm.exe --root D:\Laboratorio disk-config recursos --volume-quota-mib 2048 --disk-growth-mib 128
guardian-vm.exe --root D:\Laboratorio disk-folder recursos E:\OutraPasta
```

The last two operations require the VM to stop. Changing the logical quota requires a VM without checkspoints that depend on the previous configuration. Reducing the physical quota below the recoverable disk is refused. The logical capacity is selected in creation; resizing an existing filesystem is not implemented.

## Evidence and closure

The pre-test of integration approved budget, volume, sparse backing, SMP boot, manager life cycle, and migrations with injected failures. A broad intermediate execution found an incomplete driver fixture configuration and image change during integration; it does not accept the final revision. These records are preserved without adding their results to those of a clean matrix.

The first frozen matrix approved 47 Debug, 47 Release and 30 tests with ASan/UBSan. The following review rejects hardware profiles that the new contract does not implement, including inconsistent configuration and import. The complete matrix, including ownership corrections, memory and recovery under full quota, was approved in `build-phase12b/acceptance-20261020-full-04`, with frozen sources, image recompilation and executables, Debug/Release with KVM and ASan/UBSan of the core. The effective result should be read in `result.json`, next to the XMLs of each profile. The cross-review of Windows syntax with LLVM-MinGW passed; it does not run WHP, does not use MSVC and does not constitute Windows acceptance.

To close the full scope, it is necessary to complete Windows acceptance of the same revision and complete the integration of new features into existing application profiles. Do not interpret the feature kernel as automatic SMP support for PE applications, commercial drivers or general Windows.

## Final result of this Linux run

In `acceptance-20261020-full-04`, the same revision of 368 source and test files passed flawless or skips on:

| Profile | Result |
| --- | --- |
| Debug, guest real KVM | 47/47 |
| Release, guest real KVM | 47/47 |
| Core ASan/UBSan | 30/30 |

Hashes have been checked before and after each step. Images and executables have been rebuilt in this folder without reusing the historical Windows result. The [result.json](../build-phase12b/acceptance-20261020-full-04/result.json)] file consolidates commands, times and XMLs. Previous attempts remain separate and do not enter those totals.

Delivery includes `GuardianAV-12B-fontes-20261020.zip` to continue on Windows, `GuardianAV-12B-linux-20261020.tar.gz` with ELF Release executables and own images, and `GuardianAV-12B-20261020-SHA256SUMS.txt`. Files are in `/home/gabri/Projetos` and `build-phase12b/delivery-20261020` in the original folder. ZIP is checked against 368 hashes of accepted fonts. It was produced before transfer and saves the record of that moment; this report and transfer log have also been updated on the HD.

12B** is not closed**: you need to run MSVC/WHP acceptance on Windows and complete the integration of new SMP and filesystem to previous PE profiles. Preserve the full scope decided by the user. The new kernel own contract does not replace these to-ed. Do not start 12C.
