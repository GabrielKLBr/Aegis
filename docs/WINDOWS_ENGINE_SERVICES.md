# Aegis Engines as Windows Services

Independent delivery of phase 14.11 frameworks, requested in 28/10/2026. The work of the frameworks remains in progress and does not integrate this delivery.

## Use by app

Open `avui.exe` in the complete folder of the package and enter **Configurations**. **Install services**Requests Windows administrator authorization and registers `AegisAntivirus` and `AegisSandbox`. Then each engine has its own button **Connect/Shutdown** and checked status in the real service manager. The initial installation leaves both off, with manual startup. Upon closing the app, the services that were connected remain available; window-linked analyses are cancelled when closing it. This does not add real-time monitoring or automatic analysis of the entire computer.

The antivirus service receives files/pastas analytics and delivers progress, detections and incomplete coverage. Sandbox service receives PEs in the MiniNT/WHP backend simple and, in build with graphic runtime, prepared bundles from 14.10G. ** GUI session** opens a folder containing `runtime.gpv`, `manifest.json` and `binding.gnt`, produced by existing `audio_runtime.py` tools. The kernel and helper used are those of the protected installation; the bundle needs to be linked to the same revision. Windows and audio remain in the user session.

**Analyze a file** uses the simple WHP backend already used by the scanner, whose import compatibility is limited. Arbitrary EXEs selection does not automatically prepare the graphic runtime. Unifying this flow, installers and complete frameworks continue in the respective steps of the roadmap. Single development tools retain their own commands; buttons control the services and analyses they send.

## Limits and isolation

- There are two processes registered in the SCM as `SERVICE_WIN32_OWN_PROCESS`, without
  There's an analysis per engine at a time.
- The supervisor uses LocalSystem to manage tokens and sessions. It does not open
  the sample path and does not execute this file as host program. The analysis occurs in a reliable executable with restricted token of the requester, removed privileges, average integrity and the original interactive session.
- The installation copies a fixed list of files to a folder versioned in
  `Program Files\AegisEngineServices`, with writing reserved for administrators and SYSTEM. An update validates the identity of existing records, preserves the connected/off state and tries to restore the previous configuration if a step fails. It does not replace services from other programs.
- The user you installed can consult, start and stop the services themselves;
  does not receive permission to change executable, account or ACL from them. This allows the requested controls and does not implement tampering protection by another process from the same account. Administrator authorization is required to install, update or remove the records.
- Pipes only accept local connections, with explicit ACL, PID validation
  Service, User Authentication and PID Validation of the Worker. The protocol has version, fixed operations, path up to 32,767 UTF-16 units and response up to 2 MiB. There is no command to perform an arbitrary binary in the host.
- Each request has Job Object with closure of children and quotas of
  cases/memory. Turn off engine, cancel analysis, lose client or reach deadline closes work. Sandbox: up to 60 seconds; file tree: up to one hour. Incomplete results do not equal secure file.
- Graphical session reports are in the user profile. Removing
  services preserves versioned folders and reports for recovery.

Services stay in session 0; separation between supervisor and user process follows the [indirect interaction model recommended by Microsoft](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services).

## Compilation and verification

In Windows/MSVC, use `GUARDIAN_GUI_COMPATIBILITY_ONLY=OFF` for the app and services. The simple backend requires `GUARDIAN_BUILD_GUEST_CONTRACT=ON`. Graphic forwarding also requires compatibility targets up to audio (and.g. `GUARDIAN_BUILD_PE_GUI=ON`, `GUARDIAN_BUILD_EXTERNAL_PE=ON` and its enabled dependencies). Targets are `guardian-engine-service`, `avui`, `engine_services_tests` and `guardian-service-wait-sample`. The minimum scanner build remains available without requiring the full graphical compatibility corpus.

`tools/package_engine_services.py` mounts the Release installation folder, with list of files and SHA-256, using the licensed x64 redistributibles of Visual Studio. Binary belong only to the private builds repository.

The `engine_services_protocol` test covers validation and limits, Unicode, partial transfers, and pending reading cancellation. The focused `test/EngineServicesIntegrationTests.py` test uses the already installed services, own samples, and a non-uplifted account to verify independence, YARA, coverage errors, permissions, WHP, and process cancellation. With `--bundle`, it also checks the forwarding of a prepared graphical session. Only this change and its dependencies enter into the validation of this delivery.

Service executable auxiliary commands: `--setup`, `--remove`, `--status`, `--start antivirus|sandbox`, `--stop antivirus|sandbox`, `--scan CAMINHO`, `--sandbox PE` and `--bundle PASTA`.

## Validation result

Windows acceptance of `r3` revision in 28/10/2026:

- Builds Release with graphic and minimal runtime without the graphic corpus: compiled.
- Protocol in Release, Debug and minimum build: passed, including ACL, identity
  server, limits, Unicode, read cancelled and end response with slow reader.
- Ten scenarios in the installed services: passed.
  Unicode files, YARA detection, incomplete coverage, protected configuration and executables, malformed request, WHP execution, independent stop, disconnect cancellation and graphics/audio session prepared from 14.10G in user session.
- Facade used by the app: passed results, cleaning between analyses, cancellation
  and explicit failure when the service is off, no alternative local execution.
- Token policy: filtered, without administrator and with only the privilege of
  The installation occurred with CAU; the tests of use were not high.

The pipe attributes read rights have been corrected and the final response discarded when the server disconnected before reading by the client. Closing now waits for the client for a limited period and can be cancelled by shutting down the service without indefinitely blocking the supervisor.

Publication audit continues to check all paths of all commits. Similar content is checked once per audit, with separate checks for PNG and text. Four focused tests have passed, including deleted artifact rejection from history and renamed content to an improper path. Previous 45 builds have been checked for preservation before adding the package of this delivery. Execution batteries of the previous phases have not been repeated.
