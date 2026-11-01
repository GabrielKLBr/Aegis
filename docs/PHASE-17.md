# Phase 17 — WinUI 3 application and internationalization

The existing C++/WinRT desktop project was imported into `src/winui` and connected to the current AegisAntivirus and AegisSandbox services. Nine source/resource files totaling 72,649 bytes were copied and hash-checked before adaptation. The original Desktop project remains intact. Old build trees, artifacts, the previous IPC/backend, and QEMU components were not copied. [Import provenance](PHASE-17-IMPORT.json).

## Build and run

Configure the native Windows project as documented in the main README, then build the new desktop client:

```powershell
.\tools\build_winui.ps1 -Configuration Release -BackendBuild <NATIVE_BUILD>
.\build-phase17\bin\Release\Aegis.exe
```

Alternatively, `cmake --build <NATIVE_BUILD> --config Release --target guardian-winui` builds the explicit WinUI target. It is not part of every native/kernel build. Visual Studio C++ v143, Windows SDK 10.0.26100.0, CMake, build-time Python, and NuGet access for missing packages are required. Windows App SDK 2.5.1, C++/WinRT 2.0.250303.1, and SDK BuildTools 10.0.26100.4654 are pinned in the project. `-PackageSource` can select a local NuGet package cache. Generated code, intermediates, executables, and the self-contained WinUI runtime stay under `build-phase17` on the repository drive.

Run the complete output directory, not an isolated EXE. The executable requests ordinary user privileges and PerMonitorV2 DPI awareness. Installed services must belong to the same installation owner. Service installation, distribution updates, and clean-machine acceptance remain in phase 18. The previous `avui` client remains available during migration.

## Product flows

| Page | Current behavior |
|---|---|
| Overview | Actual protection status, latest service scan, quick/full scan entry points, visible incomplete coverage. |
| Scan | Persistent quick/full/custom requests, file/folder selection, progress, counts, cancellation, and actual service results. |
| Protection | Monitored folders, exclusions, enable/pause, automatic quarantine, scan limits, and a quick-scan interval. The UI-owned schedule preserves other schedules. |
| Sandbox | EXE execution, prepared bundles, USER32 checkpoint/resume, progress, cancellation, and reports from the sandbox service. |
| Virtual machines | Controls and state of the owned MiniNT session; no fabricated QEMU or arbitrary passthrough controls. |
| Behavior | The current/last session's observed events, findings, differences, coverage, and errors, with JSON export. |
| Quarantine | Protected broker listing, explicit isolation confirmation, restoration confirmation, and permanent-copy deletion confirmation. Existing restore targets are refused. |
| History | Persisted scan jobs, protection notifications, vault audit, and export. |
| Settings | Immediate language/theme preview, persistent UI preferences, and independent real service controls. |

The app does not load or execute selected EXEs/DLLs. Parsing/scanning and guest execution remain in restricted service workers. The client preserves the existing service-authentication/PID checks. Administrative policy changes, restoration, deletion, and antivirus stop retain their installed authorization rules; the UI never requests elevation automatically. A connected service rejecting an operation is not reported as a successful operation. Closing the client cancels its owned interactive MiniNT session; queued scans and monitoring remain service-owned.

Requests are bounded, use private files, and are removed after IPC. Preferences and the last scan/session summary use an ACL-protected per-user `AegisUI17` directory under LocalAppData. Restored summaries never auto-execute anything. JSON export uses exclusive creation and cannot replace an existing file. Displayed diagnostic details are bounded and rendered as text; complete service results remain available for export. Nothing is uploaded.

## Languages, layout, and accessibility

English is the default. English, Brazilian Portuguese, and Spanish are validated JSON sources compiled into C++ data. The runtime needs no external language JSON or Python. Validation rejects duplicate keys, missing locales/keys, empty text, inconsistent placeholders, and malformed plurals. Unknown stored locales fall back to English. Counts use plural variants and Windows regional formatting; timestamps use the selected locale and local time.

Native NavigationView, controls, dialogs, InfoBar, Mica where available, theme resources, light/dark/system selection, and responsive action groups are retained from the imported project. Controls have accessible labels and standard WinUI keyboard behavior. Alt+1 through Alt+9 select navigation pages. The application uses actual service state and distinguishes incomplete coverage from a clean result.

## Scope and limits

This phase migrates the supported product matrix. It does not expand the MiniNT Windows-API compatibility surface, remove phase 16 scan limits, add a minifilter, or certify malware containment. The VM page manages one owned interactive session, including the supported basic USER32 checkpoints; it is not a universal multi-VM manager. History displays the service's bounded job/event pages; archived evidence and the full CLI remain available. Notification retention gaps remain visible.

Validation is limited to this UI, its package, and the service flows it uses. It does not repeat the historical phase suites, replace manual screen-reader/high-contrast testing, certify clean installations, or claim validation on every Windows version. The existing service builds are used without reinstalling them. See the results file for configuration-specific evidence.

References: [self-contained Windows App SDK deployment](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-self-contained-apps), [WinUI accessibility](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-overview).

## Verified results

Release and Debug each passed **50 integration cases and 7 administrative cases** against the installed services. Eight resource-compiler tests and 21 publication-tool cases passed, with one platform-specific symlink case skipped on Windows. Six native WinUI runs covered the three locales, light/dark/system themes and widths of 720, 960 and 1200 pixels. Each run rendered all nine pages, checked keyboard focus, exercised a scan through the actual UI command handler, and changed/saved language and theme through the controls. The display used 96 DPI; physical multi-monitor scaling, Narrator and high-contrast appearance were not manually certified. Diagnostic RenderTargetBitmap images can omit compositor-backed chrome and are not a substitute for full visual acceptance.

[Per-case results](PHASE-17-RESULTS.json). The backend/service client was tested in both client configurations; existing services were not reinstalled. Test policies and service state were restored; only new inert fixtures were isolated or restored. Private packages preserve a self-contained WinUI output and source; Debug needs the developer's Visual Studio debug CRT. Packaging verifies source/binary hashes and ZIP members before extracted-package acceptance.
