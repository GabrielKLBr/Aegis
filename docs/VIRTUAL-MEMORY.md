# MiniNt virtual memory — 16/10/2026

This step continues the existing implementation in `D:\Desktop Folders (DO NOT ERASE)\AntiVirus`. It uses directly the WHP, without QEMU, without booting ISO Windows and without modifying the installation of Windows from the host. The manager is own and meets syscalls intercepted by the existing `MiniNt`. It is still an NT personality implemented in the host, not a complete NT kernel that bootes itself into hardware.

## Behavior implemented

`src/microvm/host/GuestVirtualMemory.h` maintains the metadata of regions in the host and the state of each page of 4 KiB. The default arena continues from `0xA00000` to `0xE00000`; `SetArena` allows setting up another arena aligned before use. Reservations begin in multiples of 64 KiB. The arena cannot override the heat configured in `MiniNt`.

| Operation | Effect verifiable |
|---|---|
| `NtAllocateVirtualMemory` | Reserve addresses, commit pages or combine the two operations; validate limits and flags. |
| `NtAllocateVirtualMemoryEx` | Use the correct order of arguments of this API; accept zero extended parameters and refuse extended options not implemented. |
| `MEM_RESERVE` | Keeps the area occupied and inaccessible to the guest. |
| `MEM_COMMIT` | Zera newly committed pages and applies protection; new commit of already committed pages preserves data and protection. |
| `NtFreeVirtualMemory` / `MEM_DECOMMIT` | Removes access to the pages while maintaining the reservation; later commits to zero the old bytes. |
| `NtFreeVirtualMemory` / `MEM_RELEASE` | Requires the original base and zero input size; releases the entire reservation and allows you to reuse your addresses. |
| `NtProtectVirtualMemory` | Requires committed pages from the same reservation, rounds the range and returns the previous protection to exactly 4 bytes. |
| `NtQueryVirtualMemory` | Implements `MemoryBasicInformation`, with structure x64 of 48 bytes, for addresses within the arena. Distinguished free, reserved and committed, protection, reservation base and type. |
| `NtMapViewOfSection` | Shares address control with private locations, no collisions; registers views such as `MEM_MAPPED` or `MEM_IMAGE`. |
| `NtUnmapViewOfSection` | Remove the entire view, including when you receive an internal address, and remove your access to the WHP. It does not release private allocations. |

The new allocation, release, protection, query, and mapping paths check the entry/exit pointers, RAM limits, and permissions before accessing the data. They only serve the current process (`HANDLE(-1)`). Output fields are preserved when the operation fails; short buffer query can inform the required size. Aliasing between output fields and fields that would be protected/released is refused in this subset. Values and ranges that would cause overflow are refused.

## Permissions applied to implementation

`MicroVmMonitor::SetMemoryAccess` uses `WHvMapGpaRange` to replace page permissions and `WHvUnmapGpaRange` to remove access. Access control is not just a value returned by syscall: WHP blocks guest instructions.

| Accepted protection | Reading | Writing | Implementation |
|---|---|---|---|
| `PAGE_NOACCESS` | No | No | No |
| `PAGE_READONLY` | Yes | No | No |
| `PAGE_READWRITE` | Yes | Yes | No |
| `PAGE_EXECUTE_READ` | Yes | No | Yes |
| `PAGE_EXECUTE_READWRITE` | Yes | Yes | Yes |

`PAGE_GUARD`, copy-on-write, unreadable execution, `ZeroBits` other than zero, large pages and other unimplemented flags return error. `MEM_TOP_DOWN` is supported within the arena. A page committed to `PAGE_NOACCESS` remains distinct from a page only reserved.

The first use of the manager removes the mapping of the entire free arena. The RAM of the host remains pre-allocated: commit/decommit do not implement physical memory demand, pagination or swap. The initial page tables continue using existing identity mapping; the additional restrictions are applied at the second translation level of the WHP.

A prohibited access ends execution with the type of access, GPA and RIP. No exception to access to the guest code is yet delivered nor is there SEH recovery. If a mapping update fails, the manager marks the VM as invalid and prevents new executions: it is necessary to recreate the VM. It does not return success with uncertain permissions nor tries to continue after a partial update.

## Integration with current sections and limits

Data views respect the maximum permissions of the section also in later protection calls. Close the section handle does not destroy an already mapped view. The data copy respects the view size and does not write in the neighboring reserve. Offset is different from zero, partial section commit and additional mapping flags are refused at this step.

**Object update:** the [handles and objects] step](KERNEL-OBJECTS.md) has started to maintain file references in the section and section in the views, as well as checking the handsle rights when mapping. Close handles does not invalidate the remaining references; the last reference releases the file object.

The section template continues to copy data for each view; there is no consistency of shared pages, persistence of file changes or copy-on-write. Tokens, ACLs and page sharing remain pending. The `SEC_IMAGE` upload maintains the previous entire image policy RWX; protection by PE section has not yet been implemented. The old PE parser has not been fully audited.

CPU permissions and validation of the new ABI do not mean that all old syscalls are reviewed: there are still mass copies and provisional responses on other paths of `MiniNt`. The query of regions does not cover modules, stacks and heaps outside the arena. There is a single address space and a vCPU. `NtKernel` (Model A) remains separate.

`SnapshotManager` saves RAM and registrars without capturing the metadata of heap, regions, sections or WHP permissions. Your restor can't be used to restore the full state of `MiniNt`; this integration remains pending.

## Build and Validate

Requirements: Windows x64, Visual Studio 2022 with C++, Windows SDK and CMake.

```powershell
.\Build-Kernel.ps1 -Configuration Release
.\Build-Kernel.ps1 -Configuration Debug
.\Build-Kernel.ps1 -Configuration Release -FullBuild
```

The compilations are in `build-codex`. The script performs four tests:

- `microvm_memory`: Heap limits and operations, including 2,500 combined operations.
- `microvm_heap_guest`: Execution of the own heat in ring 3 by the existing dispatcher.
- `microvm_virtual_memory`: states, rounding, preservation/zeragem data,
  permissions, reuse, views and induced failures in the mapping backend.
- `microvm_virtual_memory_guest`: syscalls in ring 3, ABI canaries, consultation
  of regions, reading/writing/implementation allowed and blocked by the WHP, execution of RX code, coexistence of views and private memory and invalidation of the VM.

New test programs are generated in the code itself. They do not use external samples. Watchdog limits executions; guest tests return 77 when WHP is not available, and CTest records this as ignored.

The existing examples used as regression are `vmntdll`, `vmntsect`, `vmntimage` and `vmwininit`. Run system DLL stubs or small `test/imgtest.dll` DLL within the guest. `vmntimage` now finds this fixture in the source folder configured by CMake, without depending on the path of another checkout.

## Results on this machine

Windows x64 validation with available WHP, MSVC 19.44 and SDK 10.0.26100.0:

| Scan | Debug | Release |
|---|---|---|
| Four CTest tests | 4 passed, none ignored | 4 passed, none ignored |
| `vmntdll`: real allocation stub and reading/writing in guest | It's over. | It's over. |
| `vmntsect`: Virtual file content read by view | It's over. | It's over. |
| `vmntimage`: relocation and execution of `Probe()` from test DLL | `0xC0FFEE5`, correct | `0xC0FFEE5`, correct |
| `vmwininit`: real exports attended by the own heap | It's over. | It's over. |
| Build complete do project | Not executed | It's over., 35 executable |

The guest tests actually ran; they were not ignored due to the lack of hypervisor. The `Build-Kernel.ps1 -Configuration Debug` script also successfully completed without warnings on compiled targets. The Full Release build maintains previous types conversion warnings in model A and other modules.

Evidence ed `build-codex`:

- `virtual-memory-release-tests.log`: the four tests in Release.
- `virtual-memory-debug-script.log`: compilation and four tests in Debug.
- `virtual-memory-regressions.json`: complete exit of the eight executions of the examples.
- `virtual-memory-full-release-build.log`: compilation complete Release.
- `virtual-memory-change-manifest.json`: modified files, backup and hashes.

## Next parts of the core

The next step is to consolidate handles and objects, including sections/files. rights, references, and service life Then come threads, waits, and synchronization with real effects, as well as the evolution of page tables and the delivery of exceptions. This still does not establish full compatibility with NT, Win32 or drivers, nor secure malware execution.

The files prior to this step are preserved in `snapshots/codex-before-virtual-memory-20261015-233931/`.

## Semantic references

- [ZwAllocateVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwallocatevirtualmemory)
- [ZwFreeVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwfreevirtualmemory)
- [VirtualProtect and views mapped](https://learn.microsoft.com/pt-br/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect)
- [](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/managing-memory-sections) Section Management]
- [WHvMapGpaRange](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/funcs/whvmapgparange)
