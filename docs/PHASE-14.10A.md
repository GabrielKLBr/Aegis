# Phase 14.10A — native GUI protocol and auxiliary

This batch implements the first complete transition of **Gui Commands from MiniNT to actual Windows host APIs**. The minimum program creates a window, paints two regions with `FillRect` and checks their colors with `GetPixel`. Windows executes the drawing; MiniNT does not provide a rastered window. The legacy graphical channel can register an empty desktop for its compatibility checks; it does not draw or provide the contents of the native window.

The application uses `guardian-native-gui.dll` itself DLL and its typed call. This still does not adapt the import USER32/GDI of existing applications: windows, controls and messages correspond to 14.10B; wider drawing to 14.10C; external graphical acceptance including Flappy in this backend, to 14.10D. [12F](PHASE-12F.md) pixel mode remains available and separate.

## Contract and execution

`guardian-kernel-native-gui.gk` reuses the 12F/14.9 base and announces `GuardianNativeGui/1/guest`. A CPL3 call is only accepted by the `0xAA` service when part of the executable section of the declared DLL gateway. The kernel copies and validates the structure, associates real PID/TID and sends a package via the F8 channel, operation byte OUT 4. The enumeration is in the 256 offset of the existing graphical page and remains protected against written by the guest.

The fixed protocol size, version, boot, random session, sequence, authorship, command and response. It does not contain addresses, callbacks or Windows handles. The typed object table belongs to the session and requires the PID/TID creator. IDs are not reused during the session. A numerical reference to an external object or other type does not grant access to it.

The set of this batch is: create window, create RGB brush, get DC from this window, fill rectangle, consult a pixel from its surface, release DC, delete brush, destroy window and complete. `Hello` is unique from IPC startup, without equivalent service for the guest program. Invalid types or parameters return error; meeting/sequence/envelope corruption closes the channel.

The helper creates a DIB and a DC **in the host** to save the drawing during repaint, uses `FillRect` of GDI and displays by `BitBlt`. The color query uses `GetPixel` on this own surface. It does not receive pixel arrays from MiniNT. The window receives the `[MiniNT native 14.10A]` prefix.

## Auxiliary process and IPC

The manager initiates `guardian-native-gui-host.exe` by the absolute path next to its own executable. The manifest selects `win32-gdi-helper`, fixes the protocol version and SHA-256 of that helper; exchanged file requires new package/link. The reading of JSON refuses duplicate keys, invalid syntax, excessive depth and incompatible type values.

The creation uses restricted token derived from the current user, group Administrators in deny-only, privileges removed save `SeChangeNotifyPrivilege` and low integrity level. The list of Restrictor SIDs allows the normal accesses of the user reading/startup and Windows resources required for the interface. The helper checks your token and Job membership before accepting commands. This profile is not AppContainer or a blocking declaration of all user file reading. The contract does not offer file/network/COM operations, DLL loading chosen by the guest or guest code execution in the auxiliary.

Only two anonymous pipe tips are inherited by an explicit list of handles. Stdout/stderr share the same answer tip. The environment contains only `SystemRoot` and `WINDIR`, without personal variables or credentials of the management process. The process starts suspended and enters the Job before running.

Job limits private memory to **64 MiB** and active processes to **one**, terminates its processes by closing and applies UI restrictions to clipboard, external handles, global atoms, desktops and system configuration changes. There is no fallback for an auxiliary without these restrictions when creation fails.

There is a single request in transit. Each package fits into the pipe buffer; a read-out tip held only by the father verifies that the previous request was consumed before the next writing. Extra replies, disconnection, envelope change and invalid counters are refused. Initialization has a ten-second limit; each response, two seconds. When closing, the father closes the channel, waits until 500 ms and, if necessary, ends Job and waits for up to two seconds.

## Lot limits

| Resource | Limit/status |
| --- | --- |
| Windows | Four by meeting |
| Objects, including windows/brushes/DCs | 64 by meeting |
| Commands | 1.024 by meeting |
| Window client | 16×16 up to 1.280×720 |
| Position start | −4.096 up to 4.096 by axle |
| Title guest | Up to 63 characters ASCII |
| Cores | RGB of 24 bits |
| Reference guest | Windows 10 x64 of existing matrix |
| USER32 general, controls and callbacks of messages | Pending ed 14.10B |
| Fonts/text/pictures general and profiles NT additional | Pending ed 14.10C/matrix |
| DirectX and audio | Not started |

The closing of the window and the native entry are not yet forwarded to the application by this contract. The modal loop of moving a window may exceed the time of an operation and terminate the session. These behaviors belong to the USER32 batch. Applications that leave with live objects fail to complete; the supervisor collects the helper. No general GUI compatibility or GDI equivalence between NT versions is announced by the presence of this backend.

## Usage

Provide legal CRT/schema of the reference already used in previous phases:

```powershell
python -B tools/native_gui_runtime.py --build BUILD --config Release --reference CRT --schema-reference SCHEMA --output app-native
BUILD/Release/guardian-desktop.exe BUILD/guest/Release/guardian-kernel-native-gui.gk app-native/runtime.gpv app-native/manifest.json app-native/binding.gnt sessao-native --native-gui --seconds 8
```

`--native-gui` must match the backend of the manifest. It does not match `--integrated`, `--interactive`, `--script` or `--window-script`. An existing output folder is refused and its reports remain intact.

`native-gui.json` records events by sequence/PID/TID/operation/result, restrictions, exit of the auxiliary and the last known features. `nativeCalls` contains, in this order, window creation counts, brush, GetDC, FillRect, GetPixel, ReleaseDC, DeleteObject and window destruction. Normal completion requires zero objects and zero output of the auxiliary. After a fall, the last counters may contain living resources: the process output and the absence test of their windows proves the recall, without inventing a destruction response.

## Acceptance and continuity

The new tests are `native_gui_core`, `native_gui_broker`, `native_gui_integration` and `native_gui_runtime_tools`. They include invalid envelopes, expired types/author/IDs, quotas, manifest, versions, two sessions with real DCs, colors obtained by GDI, partial boot failure, dead/suspension helper, incompatible modes, SHA link and preservation of previous reports. The auxiliary test also captures only the client of his own HWND with `PrintWindow` and gives the color drawn, in addition to the internal consultation with `GetPixel`.

There are five MiniNT scenarios per configuration: two normal executions with different sessions, guest drop, watchdog and assistant death. Acceptance requires **143 Release regression tests and 68 Debug acceptance tests**, without faults/ignored, with fonts, binaries and stable inputs. The extracted package is checked by hash and executed in both settings; its total scenarios also include rejected invalid states inherited from the previous stages.

The first acceptance Debug found five inherited network failures: the laboratory chose a TCP port and assumed availability of the same number in UDP, although Windows maintains distinct reserves per protocol. The laboratory starts to reserve the pair before starting threads, with limited attempts and closing of rejected sockets. Tests exercise specific reserve, collision, exhaustion and spread of other errors. They do not change firewall or system reserves. The original attempt report is preserved at delivery.

The package does not include Microsoft input DLLs, Flappy, ISOs, credentials or VM states. Host debug requires the debugging runtime of Visual Studio. Linux has not been revalidated. Delivery ends at **14.10A**; **14.10B requires new request**.

References: [restricted tokens](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken), [process with token](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw), [handles inheritance](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes) and [GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/device-contexts) DCs].
