# Phase 14.2 — External PE charger and runtime volume

Windows/MSVC/WHP implementation of 24/10/2026. This delivery expands the existing charger; does not start at 14.3. The executable contract remains Guardian, with its own test DLLs. Wide compatibility with Windows binaries and Flappy execution are not declared.

## Implementation and limits

The new `guardian-kernel-external-pe.gk` receives an external read-only volume (`runtime.gpv`). EXE, DLLs and manifests do not need to be embedded into the kernel. `guardian-pe` confers SHA-256 kernel, EXE, volume and manifest against the 176 bytes link of the 14.1 before preparing the execution. The volume and its metadata are reserved on the memory map and protected against writing/implementation. Parsing, relocations, imports and execution of the code take place on the guest in CPL3; there is no execution of the file provided as the host process.

`GUARDIAN_SCALABLE_PE` selects these limits in the new kernel, tests and corresponding runtime DLL. Previous kernels retain their limits and the size of their commands. Do not mix Guardian DLLs from different contracts.

| Resource | Contract external 14.2 |
|---|---|
| PE file and mapped image | Up to 8 MiB each |
| File Catalogue | 16 entries, including manifests |
| Modules PE | Up to 16, subject to available memory |
| Volume of entry | Up to 64 MiB |
| Pages private by Case | Up to 16.384 pages / 64 MiB |
| Arena virtual / regions | 256 MiB / 256 regions |
| Image sections | 32 |
| Relocations / exports / functions of unwind | 8.192 / 8.192 / 4.096 |
| TLS | Up to 4 KiB per module, 128 KiB TEB per thread |
| Callbacks of startup | Up to 144 by lot |
| API alias / assembly identities | 16 / 8 |
| Thread Activation / Depth Contexts | 8 / 8 |
| Manifest / tree of resources | 4 KiB / budget of 256 directories |
| References of resources | 32 by Case |

Limits are refusal checks, no promise that all maximums fit simultaneously. The reference application has a 2-MiB payload, image greater than 128 KiB, more than four modules and TLS of 2 KiB. CLI reserves 256 MiB of RAM and maintains the 30-second execution limit of the boot contract.

## Search, import and life cycle

The catalog only accepts canonical paths under `C:\App`, `C:\Windows\System32` and `C:\Windows\WinSxS`. Refuses host paths, crossing, alternative streams, duplicate entries and overlapping regions. The order of the contract is: explicit guest path; registered API alias; active assembly directory; EXE directory; System32. `guardian-runtime-api.dll` and `user32.dll` are reserved to the guest System32. There is no search for PATH, current folder or Windows host directories. The search ignores uppercase/lowercase and adds `.dll` when the requested name has no extension.

The API Sets map is explicit in the package of each NT profile and linked by SHA-256. It is not a parser of native ApiSetNamespace v2/v6. schemas The three profiles of 14.1 remain available; changing the profile requires another package and link. DLLs and new contracts are the responsibility of the next subphases.

There are imports by name and ordinal, data exports, forwarders by name/ordinal, features and delay imports. Delay imports RVA are resolved in advance before sealing the sections; there is no promise of lazy binding, delay-load hooks or recovery of IAT by `__FUnloadDelayLoadedDLL2`. Unused forwarders do not load their destination; `GetProcAddress` can load and boot new dependencies. Resolution cycles and graphs of cyclic DLLs are refused.

`LoadLibraryA`, `FreeLibrary` and `GetProcAddress` use the existing lock loader. Dependencies start before dependents; TLS callbacks precede the corresponding DllMain. The release runs through the reverse order, respects references and maintains dependencies of modules that are still reachable. DllMain and backend crashes mappings, references and TLS. The Windows reference also confirms PROCESS_DETACH of DllMain that refused PROCESS_ATTACH. Sections are sealed as read, reading/writing or reading/implementation; W+X is refused. Module Generations prevent resource reuse after downloading and reloading the same base.

## Resources and demonstrations

The parser validates the PE tree of features in three levels and provides query for numerical IDs. The runtime itself provides `FindResourceA/ExA`, `LoadResource`, `LockResource` and `SizeofResource`. Unicode, MIU names and the full language fallback policy are not implemented; omitted language chooses the first available numerical entry. The certificate directory is treated as a delimited file metadata without validating Authenticode.

EXE built-in manifests and external manifests registered in the volume accept an XML ASCII/UTF-8 subset of assemblyIdentity, dependency/dependentAssembly and compatibility/application/supportedOS. DTD, entities, unknown tags and duplicate attributes are refused. Assemblly selection requires name, four-component version, amd64 and win32. Activation settings maintain stacks by thread, LIFO cookies and references to the last deactivation. Version query itself can derive EXE supportedOS mode; this does not implement GetVersionEx or a full native PEB/TEB.

There is no complete resolution of WinSxS policies, subscriptions, publisher policy, COM/classes/windows or UAC/trustInfo. These requirements are rejected without simulating success. The volume is an unchanging Guardian input format, not NTFS or the integration of changeable files from the `resources` profile. ARM, x86/WoW64, managed PE, DLLs without relocations when displaced and PE directories not yet met are refused. Creating child processes and restoring snapshots on this new path remain unexplicited; own threads have been exercised. The previous GUI remains covered by phase 12 tests, without 12F advancement.

## Reproduction and evidence

Configure on Windows with `BUILD_TESTING=ON`, `GUARDIAN_GUI_COMPATIBILITY_ONLY=ON`, `GUARDIAN_BUILD_PE_GUI=ON` and `GUARDIAN_BUILD_EXTERNAL_PE=ON` using Visual Studio 2022 x64. Build `guardian-pe`, `guardian_external_application`, `guardian_external_widget2`, `external_pe_loader_tests`, `external_pe_native_tests` and desired regression targets.

`python tools/pe_volume.py sample --build BUILD --config Release --kernel KERNEL --output NOVO_PACOTE --profile win10` produces the reference. `create --spec SPEC.json --kernel KERNEL --output NOVO_PACOTE` accepts the same specification format used by `sample`. `verify PACOTE` confers the structure, hashes per file, manifest and link.

`guardian-pe KERNEL PACOTE/runtime.gpv PACOTE/manifest.json PACOTE/binding.gnt NOVA_SESSAO` executes and saves `boot.json`, copy of the manifest and graphical reports. The static inventory of PEs is saved in `inventory.json`; it does not affirm execution or coverage of unobserved dynamic imports.

`tools/validate_phase14_2.py --build BUILD --config Release --output NOVA_ACEITACAO` performs 15 mandatory tests, refuses any skip and fixed SHA-256 from sources and binary before/later. Repeat with Debug. Tests include real Windows comparison only of own DLLs, external PE execution in all three profiles, worker TLS, ordinal/data/forwarders, delayed startup, DllMain refused, delay imports, features, selection of two versions of assembly, invalid cookies, refusal of tampered package before creating the session and injection of crashes in the loader. An error or limit may not leave modules or processes alive.

Local evidence: `build-phase14.2/acceptance-Release/validation.json`, `build-phase14.2/acceptance-Debug/validation.json` and `build-phase14.2/acceptance-Release.xml` for complete regression. Only files with `status: PASS`, source code/binary stable and no unknown test confirm their acceptance. Builds are outside the public repository.

References: [PE](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) format, [search for DLL](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order), [DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain) and [activation context](https://learn.microsoft.com/en-us/windows/win32/sbscs/activation-contexts).
