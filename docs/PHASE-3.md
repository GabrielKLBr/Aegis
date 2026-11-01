# Phase 3 — Memory in the guest kernel

Base: `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. Image: `guardian-kernel-memory.gk`, version 0.3.0, x64, a vCPU, WHP. Status: completed in 16/10/2026. Phase 4 was not initiated.

## Implementation

The manager executes in CPL0 within the guest. The host provides RAM and applies the physical permissions of the WHP; address reservation, choice of frames, page tables, and virtual protection are the guest's responsibility. Freestanding code does not use Windows host allocators, containers, or APIs.

`guest/MemoryManager.cpp` adapts the rules of existing models `src/microvm/host/GuestVirtualMemory.h` and `GuestHeap.h`. The previous code remains as semantic reference and continues to be tested. Phase 1 and 2 images remain available; the new image reuses CPU tables, CPL0/CPL3 transitions, syscalls and phase 2 exception treatment.

### Physical frames and tables

- Allocator only admits `Available` boot map regions
  previously validated. Boot, image, kernel stack, user bootstrap region and the first MiB are off the free list.
- Data frames and table frames have different accounting and types.
  Invalid release, with incorrect or repeated type, is refused. Pages are zeroed in the allocation and release. There is allocation of contiguous physical blocks to internal arenas and a deterministic quota for lack of resources tests.
- Each space has PML4, PDPT, proper directories and tables.
  it has a supervisor identity mapping; user bootstrap code is shared as RX and stack/data bootstrap are private by space.
- The private user arena starts in `0x40000000`. Commit creates tables and
  Data frames as needed. Exchange space writes CR3; changes in active space reload CR3 to invalidate old translations. There is no PCID, global pages, multiple vCPUs or need for TLB shootdown in this step.
- Destroying active space is forbidden. The final test deliberately preserves
  a space with five frames of tables and 16 of stack/data. These 21 frames are living resources accounted for, not leak.

### Virtual operations

| Operation | Behavior |
|---|---|
| Reserve | Granularity of 64 KiB at the base; rounded extension in pages of 4 KiB. Registers the region without consuming frames. Conflicts and metadata limit have explicit failure. |
| Commit | Rounds the range in pages; requires a unique reserve that contains the range. Allocation and zeros frames. Recommit preserves content and protection of already committed pages. |
| Decommit | Returns pages to the allocator and saves the reservation. Zero size at the base of the reservation covers the entire region. |
| Release | Requires exact base and size zero; removes empty pages, and tables. |
| Consultation | Distinguish free, reserved and committed; informs allocation base, original protection, current protection and extension until next change. |
| Protection | Validates the entire interval before changing any page. Requires all compromised pages and returns the previous protection of the first. |

Supported protections are without access, read, reading/writing and implementation/reading, with optional save in the last three. Simultaneous writing and execution are refused. Invalid intervals, overflow and crossover of reservations do not change the previous state. Lack of frames during commit only undoes new pages and removes tables that have been empty. Reservations and existing data survive the failure.

### Page faults and isolation

The processor performs readings, writing, and calls in CPL3 to test permissions. The guest's RTD receives the default page, records address and error code, and terminates user entry in a controlled manner. A new entry continues to work. Unexpected kernel Fault follows the diagnosed fatal closure of phase 2.

Guard is a single use condition. The first access receives page fault and consumes guard; the operation is closed and registered. The next entry can access the page with restored protection. There is still no delivery of `STATUS_GUARD_PAGE_VIOLATION` to a dispatcher of NT exceptions in user mode.

Two spaces map the same virtual address for different data. User code cannot read the kernel image or physical aliases of data from the other space. CR3 exchange and access removal are exercised after the address has already been used, checking the invalidation of previous translations.

### Heap and border with WHP

The internal heap uses an arena of frames obtained by the guest allocator, alignment of 16 bytes, division and union of blocks, zeroed allocation and realloc that preserves the original allocation when it cannot grow. Its metadata stay in the supervisory arena without CPL3 access. It is not yet the implementation of the Windows Rtl/Heap exports within the new kernel.

Virtual permissions in the PTEs and physical permissions WHP are separate controls. The `0xED` device, defined in `shared/guardian/Memory.h`, accepts fixed requests from a page in `0x108000` and receives the report in `0x109000`. The host validates version, size, session, sequence, alignment, limits, R/RW/RX rights and belongs to a `Available` region. No pointer provided by the guest is converted to host pointer. Boot pages, kernel and reserved regions cannot receive new permissions from this device.

Transitions to RX change both PTE and physical permission; before zeroing and recycling the frame, physical permission returns to RW. Invalid request or failure of the WHP terminates execution without simulating success. In native tests, transient concession failures also exercise state reversal. Existing physical protections of the image and boot data are preserved.

## Compilation and execution

```powershell
.\Build-Kernel.ps1 -Configuration Debug -FullBuild
.\Build-Kernel.ps1 -Configuration Release -FullBuild

.\build-codex\Release\guardian_boot.exe --check .\build-codex\guest\Release\guardian-kernel-memory.gk
.\build-codex\Release\guardian_boot.exe .\build-codex\guest\Release\guardian-kernel-memory.gk --memory --ram-mib 64 --repeat 2 --report .\build-codex\phase3-cli-Release.json
```

`--memory` explicitly selects the phase 3 contract and enables the CPU base. The image is loaded into the WHP; it is not executed as Windows process. There is no QEMU or change in the host boot or security settings. Compiler and SDK are the same as in Phase 2. The LNK4281 warning is expected for the kernel fixed base in 2 MiB, defined by the loader contract.

## Validation and evidence

Criteria playable: `config/phase3-acceptance.json`.

The complete build and the 21 tests passed in Debug and Release, without ignored tests. The two configurations performed 80 phase 3 boots and other 160 phase 1 and 2 regression programs. The seven reference programs passed in each configuration. The three production images were inspected by the strict loader; a new Debug compilation, without BUILD_TESTING, also compiled and audited the three. The memory kernel occupies 430080 bytes in Debug and 421888 in Release. Each configuration produced 50 executables and five kernel images (three production and two negatives in Phase 2).

| Configuration | RAM | Phase 3 Boots | Time by boot observed |
|---|---|---|---|
| Debug | 16 MiB | 20 | 76,175–88,307 ms |
| Debug | 64 MiB | 20 | 295,859–311,536 ms |
| Release | 16 MiB | 20 | 73,655–86,793 ms |
| Release | 64 MiB | 20 | 268,526–330,636 ms |

The times include host preparation and self-testing, including zeroing and exhausting free RAM. They are measurements of this machine without guarantee of real-time latency. The suite took 15.43 s in Debug and 16.29 s in Release.

- `guest_memory`: runs the same freestanding core with RAM backing
  native. Cover rounding, query, protection, guard, lack of frames and metadata, rollback with previous content, failure of physical concession, real exhaustion, space limits, 1,000 cycles, heap and channel contract.
- `guest_memory_boot`: 20 boots with 16 MiB and 20 with 64 MiB per configuration.
  Each boot runs 150 entries in CPL3: 142 complete and eight page failures are contained. The final check mask is 4095.
- Each boot contains one guard consumed, 30 allocation failures expected,
  exhaustion and recovery of all available frames, 128 cycles of reserve/commit/release and five verifications of the heap. The frame peak is equal to the initially free total; the final state retains only the 21 frames of the still active space.
- Host channel is exercised with 18 invalid external scenarios by
  configuration: format, version, session, replay, invalid or protected GPA, W+X rights, size, reserved bits, IN, OUT wide, order and report.
- The regression preserves 40 phase 1 and 40 phase 2 boots by configuration,
  including their negative scenarios and the ten MiniNt tests.

New reports use the `phase3-` prefix, including in the regressions. JSONs, logs, and manifests from previous phases are not overwritten. The backup of the eight previous files changed is in `snapshots/codex-before-phase3-20261016-170923/`; the `historical-evidence.json` file records previous hashes for final comparison. The 99 verified historical files keep the previous hashes.

Evidence ed `build-codex/`:

- `phase3-full-debug-script.log`, `phase3-full-release-script.log` and o logs
  correspondent `phase3-*-tests-details.log`.
- `phase3-boots-Debug.json`, `phase3-boots-Release.json` and your
  `.negative.json`; `phase3-regression-phase1-*` regressions and
  `phase3-regression-phase2-*`.
- `phase3-reference-programs.json`, `phase3-smoke.json` and
  `phase3-cli-Debug.json` / `phase3-cli-Release.json`.
- `phase3-image-audit.json`, `phase3-build-boundaries.json`,
  `phase3-clean-configure.log` and `phase3-clean-debug-build.log`.
- `phase3-change-manifest.json`: modified and new fonts, hashes, backup and
  The verifier can be repeated without running programs: `python tools/validate_phase3.py --snapshot snapshots/codex-before-phase3-20261016-170923`.

## Limits and Stop

The current contract supports up to four spaces and 64 reserves per space, with a 16 MiB private arena. The hardware matrix covers RAM of 16 and 64 MiB. The structures accept the RAM ceiling of 1 GiB from ABI, which was not measured in that matrix. The user stack remains fixed at 60 KiB usable, with a guard of 4 KiB. The heap has an arena size defined by the caller.

Operations are kernel internal APIs. Processes, owner identification, handles, NT compatible memory syscalls and NT exception processing stay in the corresponding steps. The commit is immediate and zeroed; there is no swap, pagefile, copy-on-write or file mapping at this stage. Kernel identity mapping remains supervisor, with physical image protections applied by the host. There is a vCPU and there is no competition between threads.

The completion of this phase does not imply compatibility with Windows applications or drivers or completion of the antivirus. At completion of phase 3, stop. Phase 4 only starts after new user request.

## Technical references

- [Intel Manuals: pagination, CR3, permissions and page failures](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Establishment of virtual pages](https://learn.microsoft.com/en-us/windows/win32/memory/page-state)
- [Book and commit with VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)
- [Decommit and release with VirtualFree](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree)
- [Guard pages](https://learn.microsoft.com/en-us/windows/win32/memory/creating-guard-pages)
- [WHP: Guest physical memory mapping](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange)
