# Phase 14.4 — KERNEL32/KERNELBASE and basic contracts Win32

Windows x64 implementation of 24/10/2026. The matrix of this subphase runs external console programs with its own KERNEL32 and KERNELBASE, supported by native services of 14.3. Child processes were added along this path, files shared in the guest, thread pool and completion of I/O. This does not declare general compatibility with Windows, CRT, GUI or Flappy applications. 14.5 depends on another request.

## Libraries and execution

`guardian-kernel-win32.gk` maintains the format and image limit of 1.5 MiB from previous kernels. The states of up to three processes are allocated into guest frames when needed, released when closing boot. Each process has loader, pages, TEB/TLS, PEB, heaps and separate DLL data; four threads per process share that space. The scheduler has 16 global slots. These are the existing manager limits, not a promise that all maximum quotas fit simultaneously.

The external volume includes main EXE, EXE son, plugin, `kernel32.dll`, `kernelbase.dll`, NTDLL own, runtime Guardian and metadata API set. The code of applications and these DLLs runs on CPL3. KERNEL32 contains forwarders for KERNELBASE; it implements wrappers and user mode routines, calls NTDLL itself and reuses existing upload implementations, console, threads and exceptions. Host does not resolve imports from these programs nor forwards your requests to Windows host APIs.

The new PEB contains a process parameters prefix with image, command line, directory and environment. Children receive another copy of the parameters and environment, keeping the father's identity and his restrictions in the manager. Changing the environment in the child does not change the father. Children can start suspended, be resumed and awaited by real handles, including after the end.

There are **128 exported names** in Win10/Win11 and **127 variants in Win7** profile. `GetSystemTimePreciseAsFileTime` is omitted from Win7 export. The count includes aliases/forwarders and does not mean full coverage of each combination of arguments or the entire Windows API.

## Functional matrix

| Family | Exercised implementation |
|---|---|
| Memory/heaps | VirtualAlloc/Free/Protect/Query; RTL heaps, creation, size, zero, realloc and release |
| Cases | CreateProcessA/W in the unchangeable catalog, suspension/resume, waits, IDs and output codes; separate child and command line |
| Threads | CreateThread, output query, yeld, normal output, dynamic 64-index TLS and NTDLL FLS |
| Thread pool | Four work objects, limited queue, guest worker, callbacks, wait, cancellation of jobs still lined up and closing |
| Synchronization | Events manuals/automatic, waits simple/multiple; mutexes and critical sections reused; Sleep/SleepEx alertable |
| Console | GetStdHandle of exit/error and WriteFile on the guest console |
| Files | CreateFileA/W, Read/WriteFile, cursor, size, flush, creation/removal of directories and deletion of files |
| Mapeamentos | Anonymous and file-based sections; views share frames, keep backing after CloseHandle and write back on flush/last unmap |
| I/O asynchronous | Guest worker, OVERLAPPED, event, ERROR_IO_PENDING/IO_INCOMPLETE, GetOverlappedResult, CancelIoEx and ReadFileEx/WriteFileEx with APC completion |
| Environment/paths | A/W, copy/release block variables, environment heritage, process directory, point/point-point normalization and image path |
| Unicode/NLS | UTF-8/UTF-16, replacement pairs, Windows-1252/ACP, buffer sizes, invalid entry error, ordinal comparison and uppercase/lowercase in the clipping described below |
| System/time | Profile version, CPU/arena information, guest's total/available RAM, QPC, existing ticks and schedule |
| Modules/resources | LoadLibraryW/A, GetModuleHandle, GetProcAddress, forwarders, unload and resources PE for charger existing |
| Exceptions | SEH/RaiseException in child application and process, with existing dispatcher/unwind |

Changeable storage is a volume RAM shared by the services of each process, separated from the volume read-only EXE/DLLs. ACLs, hand-sharing, property and quotas continue to be verified by volume. Close the last process discards your files. CLI records `boot.json`, `analysis.json` and `analysis.html`; tests confer child file calls, process creation, results, no loss and frame return. Purely user mode routines, such as text conversion and TLS access, do not automatically become kernel calls in telemetry.

## API sets and NT versions

API sets are logical names: the loader solves the host by schema, without requiring a physical file called `api-ms-...dll` for each import. This organization is described in [Microsoft](https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets) document.

The new reader validates namespaces v2 (Win7 profile and v6 (Win10/Win11), offsets, lengths, indexes, input budget and host names. Supports default host and replacement by importer module. In v6 format, solves previous revisions of the same family by the HashedLength prefix; above-selected revisions are conservatively rejected. Resolution cycles have depth limit. The final host needs to exist in the guest catalog; there is no search in the Host System32.

The actual reference is `apisetschema.dll` x64, version **10.0.19041.2788**, extracted from ISO Win10 22H2 pt-BR image 1 of the `install.wim`. SHA-256: `714abf6fb87298d04c3137fb2a53c86335fb6fbe1f3d922a000293482b21b576`. It is read as data without execution in the host. Parser test reads its **891 contracts**; the Win10 package recodes only **168** with KERNELBASE/KERNEL32/NTDLL hosts in the reference. It does not include code or copy of that Microsoft binary. Contracts whose host has not yet been implemented remain rejected.

The Win7/Win11 schemas are their own fixtures in the corresponding format, with limited contracts. Exact native references of these versions, additional schemas, all details of lookup/revision and the original KERNEL32/KERNELBASE DLLs were not accepted at this stage. The 19041 version of the reference and the Win10 19045 profile remain separately identified without claiming exact kernel compliance.

## Known limits

| Resource | Limit/clipping of this delivery |
|---|---|
| Changeable file | Up to 512 bytes per file; 32 nodes and RAM volume of 64 KiB, payload added up to 16 KiB |
| ReadFile/WriteFile | Request Win32 up to 1 MiB, divided into native operations of 512 bytes; read can finish before; write respects the actual file quota and informs failure/transfer partial |
| Handles | Up to 8 file handles per owner in the backend, including mapping backup; 32 native handles, plus existing manager quotas |
| File Mapping | Up to 512 bytes of content, backing with share allowed, view page rounded to 4 KiB; anonymous sections keep the limit of 256 KiB |
| I/O | 8 requests/work queue, a worker per process; high offsets, IOCP, full Windows drivers and IRPs pending |
| Cancellation | Requests still queued can be cancelled; RAM operation already dispatched can end before cancellation |
| APCs | Four pendants per thread; issuer needs to stay alive and enter alert waiting; dispatch failure remains in the status of OVERLAPPED |
| Thread pool | Four objects; serial default pool; environment/pool custom, priorities, self-referential closing callbacks and the entire Windows surface not implemented |
| Environment | 512-character UTF-16 block per process; names up to 127 characters in the supported case repertoire; values within the quota |
| Paths | Up to 127 characters; storage and module names restricted to ASCII; Unicode paths outside this cutout are refused, although text conversion support UTF-16/UTF-8 |
| CreateProcess | EXE in immutable volume, ASCII command up to 127 characters, basic/suspension flags; handle inheritance, custom environment blocks, explicit directory and additional security options rejected |
| NLS | UTF-8 and Windows-1252; ACP 1252; case Latin-1 and pairs selected of Windows-1252, locale invariable/pt-BR; repertoires of case and locales remaining recusados |
| Runtime | Only x64; CRTs, WOW64, GUI and general binary compatibility still pending |

I/O overlapped performs real work on the worker and records its completion; it is not a constant successful return. However, this queue in user mode does not implement all the I/O semantics of the Windows kernel. The possibility of completion synchronone/asynchronous and buffer care are described in the [Microsoft document on overlapped I/O](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o).

The remaining 14.2/14.3 quotas remain: PE images up to 8 MiB, 16 catalog entries, 256 MiB arena, 64 MiB of private pages per process, heat RTL up to 1 MiB per heat and partial native structures. The host uses 256 MiB of RAM and 30-second boot budget. There are no snapshots of this path or PE/SMP integration completed. LoadConfig code accepted as metadata does not implement CFG/CET. This is not a perfect malware isolation or detection certification.

## Validation and reproduction

Four new tests cover: native Windows reference; real managers with shared FS, backing and isolation; tools/parser API set; and bootes of the three profiles. Boots run threads, TLS, thread pool, files, mappings, I/O per event and by APC, cancellation, suspended child process, isolated environment, resources, dynamic load of DLL and SEH. They also test denied writing, missing directory, nonexistent child, buffer larger than the file, invalid UTF-8 input and refusal to case outside the repertoire. Change the volume invalidate the link before preparing the session.

Parser corpus includes v2/v6, original schema DLL, overrides by importer, revisions, invalid offsets/index, truncated PE and host crossing. The native Windows test uses only own test executable and host system libraries to compare Unicode, API set, thread pool, suspended child, waits and SEH. This essay does not replace exact references from the three target kernels.

Acceptance requires **25 tests per configuration**, without ignoring: 21 of 14.3 and four new. Complete Regression Release contains **100 tests**. Local reports in `build-phase14.4` and reports included in the build record effective results and SHA-256 stable source code/binary. preparation failures of previous DLLs and logs are preserved. The package is checked by inventory, hashes and new boot in Release/Debug prior to publication.

Configure with the same requirements as 14.3. by adding `GUARDIAN_BUILD_WIN32_NATIVE=ON` and `GUARDIAN_APISET_REFERENCE_DLL` to the legal copy of schema. Keep `GUARDIAN_NATIVE_REFERENCE_DLL` for the original NTDLL regression. Compile Release and Debug and use new folders for evidence:

```powershell
ctest --test-dir BUILD -C Release -j 1 --output-on-failure --output-junit REGRESSAO.xml
python tools/validate_phase14_4.py --build BUILD --config Release --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --output ACEITACAO-RELEASE
python tools/validate_phase14_4.py --build BUILD --config Debug --original NTDLL-LEGAL --schema-reference SCHEMA-LEGAL --output ACEITACAO-DEBUG
python tools/package_phase14_4.py --build BUILD --release ACEITACAO-RELEASE --debug ACEITACAO-DEBUG --schema-reference SCHEMA-LEGAL --regression REGRESSAO.xml --output PACOTE-NOVO
python tools/smoke_phase14_4.py --package PACOTE-NOVO --output SMOKE-NOVO
```

To mount the volume of the matrix: `python tools/win32_runtime.py bundle --build BUILD --config Release --profile win10 --schema-reference SCHEMA-LEGAL --output VOLUME-NOVO`. The distributed build already includes its volumes. Full fonts go to Aegis; builds, for private Aegis-builds. ISO, WIM and original Microsoft DLLs stay out of these posts.
