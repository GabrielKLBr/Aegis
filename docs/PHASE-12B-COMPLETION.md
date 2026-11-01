# Continued 12B: full scope

**Current status:** Full-04 revision approved on Linux: 47/47 Debug, 47/47 Release and 30/30 ASan/UBSan, with 368 stable sources. 12B remains open for Windows validation and integration to PE profiles. [Current report and delivery](PHASE-12B-RESOURCES.md). The draft descriptions below are historical. The review has already been transferred to the original HD: [copy and backup record](HD-TRANSFER-20261020.md).

In 20/10/2026, the user confirmed that closing should keep the full scope: real SMP, larger volume and independent disk folder. The subphase remains open. Do not start 12C or label the old profile of 1 vCPU/16 KiB as complete closure.

Before the subsequent writing authorization, the original NTFS was mounted normally and remained read only because Windows left metadata in cache. At that time the cleaning was not forced. The copy that preserves the previous delivery is in /home/gabri/Projects/GuardianAV-12B-20261020. The new implementation is in /home/gabri/Projects/GuardianAV-12B-complete-20261020. Until that time, the original sources and Windows records remained intact in NTFS. The later transfer is registered at the end of this file.

Existing Windows XMLs contain 62 distinct settings by combining complete and retest executions. Their 347 hashes checked with copied delivery. This does not constitute new Windows execution of this continuation.

LLVM 18.1.3 was extracted in the cache, without installation in the system, to reproduce the native flow and avoid conflict of intrinsics with the LLVM 23. The images recompiled. The GCC also refused an omitted aggregate field: budgetRoot received explicit default initializer, preserving its behavior. This change requires a new identification of the sources; the previous Windows hashes are historical.

## Work history in progress

The scalable volume uses blocks of 64 KiB, redundant metadata and COW extensions. The initial code is in guest/ScalableVolume.* and shared/guardian/ScalableStorage.h. It is a draft not yet accepted, not enabled in the product builds. There is no validation of atomity, lack of space, deletion and failure recovery.

SMP needs to run real vCPUs with coherent topology, synchronization and checkspoints of all processors. The independent disk folder needs to isolate ownership, migration, recovery, clone and aggregate collection of both locations. None of these items has been declared completed.

After integration, repeat the Windows matrix and acceptance of the final revision. The original source should only receive the changes when the volume is released for writing by Windows.

## Intermediary validation preserved

The reference copy ran 42 Linux/KVM tests Debug. 41 passed the first execution. The guest_kvm test contained the old expectation of rejecting 65 MiB, incompatible with the documented limit of 1024 MiB from 12B. The negative case began to request 1024 MiB + one page. The retest passed. XMLs in ../GuardianAV-12B-20261020/build-phase12b/linux-baseline-debug.xml and linux-baseline-kvm-retest.xml. These are evidences of the reference, not of the new implementation.

The ScalableVolume core has passed ASan/UBSan: 65931 content checks, physical quota, persistence, truncation, deletion and prefixes of failures. SparseDisk has passed 252 checks with ASan/UBSan, using real sparse file in ext4: logical volume of 2 GiB, guest file of 5 MiB, physical allocation less than 8 MiB, reopening, snapshot, independent clone and space return. Results in /tmp/guardian-phase12b-scalable-volume.log and /tmp/guardian-phase12b-sparse-disk.log; will be copied to the evidence folder.

The new ResourceKernel and ResourceSession are in integration. SmpMachine creates additional processors in the same partition/VM and captures the state of each one. This still does not validate SMP in the scheduler of existing PE applications. Windows acceptance of the final revision and independent folder remain pending.

## SMP integrated to the feature kernel

The first execution revealed the absence of subsheet 1 of the CPUID 0xB topology in the table offered by the KVM of this host. The projection began to materialize the subsheets 0, 1, and ending, with significant indices, different APIC IDs, a socket and thread per core. There is no fictitious count increase. ResourceBootTests passed with 1, 2, 3 and 4 real processors on the same VM: shared barrier, interlocked increment counter, 5 MiB file, checkpoint after 120 requests and restoration of all processors. Intermediate result: 21 approved integration checks. Logs will still be preserved next to the final matrix. This proves the kernel of resources; the legacy PE scheduler still uses a CPU and has not received SMP acceptance.

## Integration and frozen matrix

The six pretest groups passed: resource_budgets, scalable_volume, sparse_disk, resource_boot, scalable_resource_manager and disk_location. The later review added orphan block recovery, stable space query, backing rejection with physical links, checkpoint during competing increments and MXCSR verification of each CPU. The final matrix compiles everything.

Consolidated status and current limits are in PHASE-12B-RESOURCES.md. The frozen matrix is in build-phase12b/acceptance-20261020-full-01, with 368 source files identified. Windows cross-review passed without notice to new modules, but does not equal MSVC/WHP. Do not close 12B before Windows acceptance and profile integration review. NTFS origin remains intact.

## Review after first clean matrix

acceptance-20261020-full-01 passed in Debug (47), Release (47) and ASan/UBSan (30), with stable source code. The additional revision found the hardware-config stream of the new contract accepting a profile that its startup does not apply. The manager started rejecting this configuration and the creation/import inconsistent, checking that the previous configuration file remains identical. The corrected fonts have been frozen again and the full matrix is in acceptance-20261020-full-02. The result of the first revision was not relabeled as evidence of the second. Without new Windows acceptance, the phase remains open.

## Property protection and recovery under full quota

Directory revision has started to reject nested disks within other disks or the state of a VM. The deletion validates the tree before moving the state to the removal transaction. Active generation also needs to have correct owner; the auto cleanup refuses unknown files/directories.

A 16 MiB RAM lock, 16 MiB snapshot and 40 MiB budget was played: the guest completed the 5 MiB file, but publishing its image exceeded the quota. After the worker exit, status and drop-snapshot failed to recover before releasing space. The query now returns a recoverable failure, saves the active generation and allows you to remove the snapshot that releases the quota. The test confirms recovery, budget increase, and new boot. The previous proof of correction is in build-phase12b/quota-recovery-repro.json.

The feature kernel watchdog repeats the cancellation until recognition, following the behavior of the previous boot, to cover the race with the hypervisor input. The test uses guest without exits with deadlines of 1 and 40 ms and four CPUs.

The full-02 and full-03 matrices were replaced during these corrections and did not accept the final revision. The next frozen matrix will be full-04. Do not add the results of these attempts to the results of a complete and stable execution.

## Freezing to full-04

The final pretest again approved the six groups: budget, filesystem, backing, SMP, life cycle and property/migration. Full quota recovery was tested by CLI without manually removing files from the catalog.

Temporary recovery now makes its own reserve of memory/CPU. The closure of a paused VM without worker extracts the disk by streaming; it does not materialize all RAM just to get backup. Reading and restoration limits accompany the growth quota configured before allocating buffers. Backing also limits the materialized representation of its mapped blocks, linking the size of snapshots to the booked reserve. Published availability considers this cost beyond physical allocation.

The internal names of the disks and generations have been shortened and disk directories are hidden from the VM enumerator. The choice of the catalog folder itself as a destination has been tested without confusing a disk with a VM. This also reduces the length of the test paths on Windows.

Fonts will not be changed during full-04. After that, publish only the effective result and check the hashes of the package. 12B remains open for Windows acceptance and integration to PE profiles; keep the full scope chosen by the user, without replacing these to-ed with an acceptance of the test contract.

## Log closing of this execution

The full-04 has passed in full (47 Debug, 47 Release, 30 ASan/UBSan), with stable sources from start to finish. The next work continues to be 12B: accept in Windows the revision delivered and integrate the resources to the PE application profiles. Do not declare the phase completed only with the `resources` kernel. The main report contains the paths of the packages, Windows procedure, implemented limits, XMLs and consolidated result. The original NTFS has not changed.

## Transfer to the original folder on HD — 20/10/2026

The user authorized writing and disassembly, confirming that it had not been important to preserve from the Windows session. A backup of the 409 existing files reached by the package was created and verified before any replacement. Normal mount continued to refuse writing; after administrator authentication, `ntfsfix --clear-dirty` corrected the incomplete state and restarted the NTFS diary. The following assembly confirmed `rw` in the volume of UUID `01DB04DC26902A20`.

The validated package contains 505 files: 126 were saved (30 replacements and 96 new ones), and 379 were already identical. All were checked after copying, including hashes of the 368 full-04 source files. Historical build directories and previous Windows evidences were preserved. Current Markdown reports were also updated in the original folder.

The packages, checksums, backup and your manifest are in `build-phase12b/delivery-20261020` in the original project. The detailed registration is in `build-phase12b/original-hd-transfer-20261020.json` and the summary in [HD-TRANSFER-20261020.md](HD-TRANSFER-20261020.md). 12B remains open for Windows/MSVC/WHP acceptance of the review and integration of resources to PE profiles.
