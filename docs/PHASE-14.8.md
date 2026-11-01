# Phase 14.8 — desktop components and services in MiniNT

This delivery implements a delimited batch of COM, PRC, automation, properties, Shell, controls and dialogs for the profile **Windows 10 x64**. Acceptable applications are executable and an independent DLL compiled with normal SDK Windows imports. The libraries themselves comply with the scenarios described below; they are not declared complete implementation of these DLLs or general compatibility with arbitrary Windows components.

## Execution and isolation

`guardian-kernel-components.gk` preserves the services of 14.7 and adds a communication channel between guest processes. The kernel copies limited messages, checks PID/TID, identity and integrity, assigns monotonic identifiers and controls the property of endpoints and answers. There are **12 endpoints, 16 simultaneous calls and 768 bytes per message**. Excess of features fails; cancellation and output of cases/threads collect calls and endpoints. Answers from another process or thread are not accepted.

The A8 entry is recognized only in executable code of `combase.dll` in the guest. The transport does not contain addresses of functions, object pointers or host handles. Factories, COM objects, window callbacks, PRC stubs and server loops run in CPL3. Activation uses virtual registry, DLL loader and process creation of MiniNT. No call is forwarded to COM, PRC, Explorer, file selector or host log.

`component_windows_*` reference tests run their own programs on Windows to compare contracts. They do not register persistent classes in the host or open personal dialogues. Acceptance applications run on MiniNT.

## Contracts implemented

| Family | Lot implemented |
| --- | --- |
| OLE32 / COMBASE | Initialization STA/MTA by thread, balanced count, rejected model change, apartment query, DLL activation via `DllGetClassObject`, process activation via `LocalServer32`, facts and locks, revocation and download conditioned to `DllCanUnloadNow`. |
| Marshaling | Limited Memory Streams, `CoMarshalInterface`, `CoUnmarshalInterface`, `CoReleaseMarshalData` and transfer between threads to `IDispatch`. Proxies send data to the owner's apartment; `CoWaitForMultipleHandles` and the USER32 message query respond to the guest's calls. |
| OLEAUT32 | BSTR with explicit length, including built-in NUL; VARIANT scalars, strings and copy arrays; ONE-dimensional SAFEARRAY limited to 256 elements of I4/UI4/R8/BSTR/VARIANT. Deep copy, bundles, locks and release. Ordinals of the OLEAUT interface used by SDK import libraries are preserved. |
| RPCRT4 | Local UUIDs, strings/bindings `ncalrpc`, limited PRC buffers, and explicit interface table 1.0 with NDR32 2.0 syntax. The application stub handles payload; there is no general NDR interpreter or arbitrary MIDL stub support. |
| PROPSYS | `IPropertyStore` in memory, up to 16 properties, enumeration, copying, reading and commit; PROPVARIANT scalars, strings up to 255 characters and blobs up to 512 bytes. |
| COMCTL32 | EDIT, column list, progress bar and page selection. Up to 32 text items per control; selection and notifications use Win32 contract structures. |
| COMDLG32 | Open/save by own dialogue in MiniNT, editable field, confirmation, cancellation, owner status refund and path validation. Selection returns only guest paths. |
| SHELL32 / SHLWAPI | Virtual known folders, limited path operations, execution of `.exe` present in the package, wait for the process and copy/movement/exclusion of unique file in the guest namespace. |
| UXTHEME | Own theme handles, window validation, basic design and release. Do not play full Windows visual features. |

Marshaling automation calls admits up to four scalar arguments or BSTR of up to 128 characters. `GetIDsOfNames` admits a name per call. Arbitrary interfaces, pointers, remote arrays, type libraries, ActiveX, aggregation, monikers, DCOM and remote authentication stay out of this contract. MTA bootings are met; a DLL registered as `Both` can be enabled in STA/MTA. Automatic transitions to an incompatible model still fail explicitly.

The STA server needs to meet your calls/messages. loop The default timeout for remote calls is three seconds and cancels the pending response; it does not arbitrarily terminate the code that has already entered the server. Ending or revoking the endpoint disconnects the corresponding proxies. The supported PRC listener is the synchronous one; the mode that would require automatic workers is refused. TCP, HTTP and other external transports are not enabled by this phase.

## Catalog and persistent status

GPV version 2 includes up to **32 files**, keeping the previous header and an additional input table. The kernel of this phase admits up to **24 modules** per process. The 8 MiB limits per file and 64 MiB per volume are maintained; the bundle includes only reachable dependencies and explicitly provided servers. The reader continues validating GPV version 1; the host requires the kernel of components for a volume version 2. Extensions, duplicities and hashes remain validated prior to execution. Runtime data protocol has not been expanded.

The changeable disk continues with 64 KiB, 32 nodes and files up to 512 bytes. Record keys with 38 characters GUID use previously unused payload from Key nodes, with specific flag and validation. Records size and two commit slots remain preserved. Query and enumeration return the entire GUID; two GUIDs with the same prefix do not become the same key. Previous states are read; states using the new feature should be used with this version of runtime, without mixing them with historical builds.

## Desktop Limits

The presentation continues in the 14.7 unique viewer. The file dialogue is a limited form; it is not yet the Explorer selector with full navigation. There is no printing, dialog hooks, custom templates, multiple selection, general file associations, URL execution, elevation or integration with Explorer. Confirmation of existing file overwriting is not implemented and is rejected. EDIT does not implement selection, complete default or IME; lists do not implement multiple columns, virtualization or sorting. There is no TreeView, toolbars and complete package of common controls in this matrix.

Themes use the drawing and the bitmap font itself. Graphical verification requires visible text in the dialog field, input of text/backspace, selection and movement of the progress bar. Non-fatal CRT diagnoses and tracking limits of the 14.7 continue to be documented in [PHASE-14.7.md](PHASE-14.7.md).

## Acceptance and reproduction

The eight new tests are `component_core`, `component_runtime_tools`, `component_windows_client`, `component_windows_controls`, `component_windows_rpc`, `component_com_integration`, `component_rpc_integration` and `component_controls_integration`.

The matrix covers component in DLL, factory in another process, return values and strings, execution in the proprietary STA, concurrent MTA calls, classes and values persisted in another session, missing class, component error, server crash, timeout, PRC scale, invalid procedure, endpoint closed, controls, confirmed/cancelled dialogue, editable entry and process initiated by Shell. The core tests quotas, property, falsified response, cancellation, cleaning, volume formats and corruption of long names of the registry.

`tools/validate_phase14_8.py` requires **124 Regression Tests in Release** and **49 Acceptance Tests in Debug**, No Fails or Ignored. Sources, Binarys and Legal Entries are compared before/later. The original 14.7 Flappy integrates the regression. Evidence remains in the `validation.json`, `ctest.xml` and logs files. The package can only be created with stable evidence and is executed again in both settings before private publication.

`tools/component_runtime.py bundle` prepares the volume from its own DLLs, CRT references and local schema. References Microsoft, Flappy, ISOs, GPV volumes and session data are not redistributed. Invocation uses the same `guardian-desktop.exe` with `guardian-kernel-components.gk`; complete commands and package tests follow the private build.

The 14.9 and 12F remain separate and are not initiated by this delivery.

Contract references: [Apartments STA](https://learn.microsoft.com/windows/win32/com/single-threaded-apartments), [COM initialisation]](https://learn.microsoft.com/windows/win32/learnwin32/initializing-the-com-library), [marshaling]](https://learn.microsoft.com/windows/win32/api/combaseapi/nf-combaseapi-comarshalinterface) and [Imarshal]](https://learn.microsoft.com/windows/win32/api/objidl/nn-objidl-imarshal).
