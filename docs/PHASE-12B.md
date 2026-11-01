> Continuation of 20/10/2026: the new `resources` own contract implements SMP,

**23/10/2026 Windows Update:**The current matrix has passed on MSVC/WHP: 80 distinct tests Debug, with documented retests, and 80/80 Release in a complete suite, without ignoring. [Corrections and Evidence](WINDOWS-PHASES12B-13-REVIEW-20261023.md). The Linux records below are historical; the scope and comparison pending versions remain open.
> scalable volume and independent folder. The old limits described below
> continue referring to previous profiles. 12B remains open; see
> [implementation, limits and current validation](PHASE-12B-RESOURCES.md).

# Subphase 12B — resources and quotas

Status: implementation in validation. The subphase is not completed for the entire scope of the roadmap. SMP and large volumes remain pending; the 12C has not started.

This delivery expands the RAM of the own matrix of 16–64 MiB for 16–1024 MiB, conditional on actual admission of the host. Maximum mode chooses the highest permissible quantity within that limit. The default reserves at least 512 MiB or 25% dthe memory of the system. `--host-reserve-mib` allows an explicit reservation of at least 512 MiB; zero selects the automatic pattern. Admission also counts eight times the RAM of each VM and 128 MiB This conservative account covers live RAM, capture, coding and restoration. A process reservation, common to the catalogs of the same user, limits the set to four workers and preserves a logical CPU in hosts with more than one CPU. When catalogues choose different reservations, the largest reserve requested between active sessions prevails. One session does not reduce the reserve of another. There is no exclusive affinity or promise of latency for the operating system.

The kernel continues with a real vCPU. Requesting more than one produces unsupported SMP diagnosis. The interface presents requested, admitted and provided; a stopped VM does not announce provisioned RAM or vCPU. Increasing only CPUID or inventory would not implement the operation of additional cores.

The block disk maintains 64 KiB and the reference volume maintains 32 512 bytes allocation units, with three initial units for the roots. Each file, directory, key or value occupies a drive; files and values remain limited to 512 bytes. The volume quota can range from 1536 to 16384 bytes, in 512 units. Creating a new entry without space returns `STATUS_DISK_FULL`. Deleting an open file only releases your drive when the last handle closes. Overwrite data from an already allocated drive remains allowed.

`QuerySpace` and `GetDiskFreeSpaceExA/W` refer to the same count used by allotments. Reference APIs accept the root `C:\` or the current drive; additional paths and drives remain outside the matrix. Volume capacity, free space and available to the quota are separate values. Available also reaches zero when backing does not contain a new complete transaction.

The journal receives its own growth limit, respecting the host allocation unit, the finite ceiling of 4096 records and a 256 MiB reserve in the host unit. Lack of quota crosses the transport of I/O with specific status. The aggregated VM storage limit includes base, journal, active copies, images, drivers, audits, snapshots and temporary replacement files. The default is 4 GiB; the setting accepts 1 MiB to 64 GiB. The billing considers the highest between the added logical lengths and the added physical allocation. The interface displays both. A 64 KiB margin allows control and closure messages when the data quota is exhausted.

Save a file requires space for the previous and temporary. Rejecting a quota operation preserves the previous state. Delete snapshots recovers aggregate storage; raise a backup quota, with VM stopped, allows you to resume recordings. Reduce a limit below the current usage is rejected.

`--root` chooses the catalog folder and its disks. Different catalogs can stay in different drives and share the user's RAM/CPU reserve. A disk folder independent of the status folder of each VM is still pending. The 12A profile disk template remains only reference: the device does not expose or access the physical disk or personal files of the host.

Examples of the command line interface:

```text
guardian-vm --root D:\Laboratorio create teste caminho\guardian-kernel-storage.gk storage --ram-mib 128 --vcpus 1 --volume-quota-kib 8 --overlay-limit-kib 2048 --storage-limit-mib 512 --host-reserve-mib 1024
guardian-vm --root D:\Laboratorio resources teste
guardian-vm --root D:\Laboratorio resource-config teste --maximum --host-reserve-mib 1024
guardian-vm --root D:\Laboratorio storage-config teste --volume-quota-kib 8 --overlay-limit-kib 2048 --storage-limit-mib 1024
```

Old settings remain readable. Previously imported kernels maintain original behavior; the kernel capacity statement prevents selecting a custom volume quota on an image without implementation. Old checkpoints of this unquoted contract maintain their original compatibility. In new images, the volume limit and protection of the configuration page are linked to the checkpoint; restoring with another limit is rejected.

## Validation

Final results and hashes will be recorded after native acceptance. The snapshot prior to editing is indicated in `build-phase12b/baseline.txt`. Compilation failures and preliminary results are preserved in this folder.

Management tests explicitly choose 1 GiB of operating reserve, maintaining the multiplier of eight and the margin per worker. The automatic reserve of 25% recused sessions during the memory pressure observed in this Windows; this refusal is also preserved in preliminary results. The deterministic tests cover both policies, the minimum of 512 MiB and the preservation of the largest active reserve. The basic kernel checks real access to the last available RAM page, as well as previous layout and boot checks.

## Remaining work of 12B

- Real SMP in VMM and kernel, with topology, startup and synchronization
  coherent; then admit multiple vCPUs in supported applications.
- Scalable volume, larger files and configurable growth in GiB;
  the physical storage quota does not increase the capacity of the current filesystem.
- VM independent disk folder, including migration, recovery and clone.
- Optional reference of capacity/manufacturer from the host disk without doing the
  volume announce space that you can't yet use.
- Expand the RAM matrix and stress as these implementations progress.

The stoppage of this delivery must keep 12B open, without advancing to 12C or declaring that pending resources are already functioning.
