# Phase 17.1 — Portable desktop app and public hash queries

This follow-up adds an integrated title bar, a clean portable folder, in-app engine service registration, and explicit SHA-256 reputation queries. It retains the existing scanner and MiniNT compatibility limits. It does not complete phase 18's signed updates or clean-machine distribution acceptance.

## Portable layout

Extract the entire ZIP and open `Aegis.exe`. There is no separate application installer:

```text
Aegis.exe
runtime/       WinUI app, self-contained Windows App SDK and its resources
engines/       Engine service registration payload and required helpers
notices/       Third-party licenses/notices
README.txt
manifest.json  Package member hashes
```

The root launcher starts the fixed relative `runtime/Aegis.exe`, forwards arguments, uses an explicit working directory and returns the UI's exit code. It uses the static C++ runtime. The SDK's localization directories stay inside `runtime/`, out of the main EXE directory. Keep this internal folder intact; removing individual SDK resources can break platform dialogs and language fallback. Aegis's English, Brazilian Portuguese and Spanish strings remain embedded in the executable.

The Release package includes app-local Visual C++ Release redistributables. Debug is preserved only as a development build and requires the Visual Studio debug CRT; debug redistributables are not shipped. No guest Windows DLLs, local prepared runtime catalog, personal state, credentials or vault content are packaged. Settings and reputation cache use per-user LocalAppData; this portable distribution is not a claim of zero writes outside the ZIP folder.

## Window and service controls

WinUI content extends into a custom draggable title area. Windows retains caption buttons, resizing, maximizing, the system menu and Snap behavior. The Windows 11 border color is suppressed where supported. Caption padding follows DPI and the reserved native button area; caption foreground follows theme and defers to system colors in high contrast. Physical multi-monitor scaling and assistive-technology appearance still need manual acceptance.

Settings shows **Install engine services** when registrations are missing and **Update / repair** and **Uninstall engine services** when installed. Installation and removal run only after a button click; Windows then requests administrator authorization. Removal has an additional in-app confirmation. Cancelling Windows authorization is reported without claiming success. The application itself starts as an ordinary user; no elevation is requested at startup or during a hash query.

The two services remain required for their respective analysis flows. Installation copies their payload into a versioned SYSTEM-owned, read/execute-only location for standard users under Program Files. The antivirus starts on a fresh installation; the sandbox can be started separately. Updating preserves prior running states. Removal unregisters the services while retaining versioned files, protection settings and the encrypted vault for recovery. A protected runtime-slot locator preserves locally prepared guest references across uninstall/reinstall without redistributing them. The existing runtime verifies its pinned member/helper hashes before use.

Two malformed pre-existing security descriptor literals were corrected: the installed file permissions and restricted worker process/thread permissions. Regression cases validate their actual Windows parsing and allowed principals/rights. Existing antivirus-stop, policy and vault authorization rules continue to apply.

## Public databases

On **Scan → Public hash databases**, select a regular local file or paste a SHA-256. The default computes/accepts the hash locally. Turn on **Allow this query to contact public databases** for that query to contact the fixed CIRCL and MalwareBazaar endpoints. This is a separate manual lookup; ordinary quick/full/custom scans do not silently enable online queries.

- **CIRCL hashlookup:** no API key; known-file presence is context, not proof of safety.
- **MalwareBazaar:** configure its free Auth-Key in Settings. The masked input stores it in the current user's Windows Credential Manager, outside the portable folder. The app exposes presence only and provides explicit save/remove controls. Blank input does not overwrite a saved key. Without a key, its result is `authentication_required`.

Requests contain only the SHA-256 and, for MalwareBazaar, its authentication header. There are no sample uploads, downloads, file names, file paths, arbitrary URLs, redirects, certificate bypasses or implicit Windows credential forwarding. Public VirusTotal is not added as an antivirus connector because its public API terms restrict commercial/product workflows; the existing separately licensed scanner connector is unchanged.

Hashing runs in a restricted per-user service worker, streams at most 1 GiB, rejects remote/device/reparse paths and directories, refuses concurrently writable files, and has a 30-second read deadline. The service call has a 60-second deadline and closing the app cancels its IPC. Provider JSON is limited to 1 MiB and 32 nesting levels; returned hashes must match the query. SQLite cache retention is six hours for matches and 15 minutes for misses, with per-provider limits of six network queries per minute and 100 per day. A 429 response adds a cooldown. No API key is stored in cache or request JSON files.

Results distinguish disabled, found, not found, unavailable, authentication required and rate limited. MalwareBazaar matches are supporting suspicious evidence; CIRCL matches remain unclassified. Missing results and provider errors never become a clean verdict and never trigger automatic quarantine. `complete` means the local operation completed; `providersComplete` separately records whether all requested online providers responded.

CLI equivalent:

```powershell
guardian-engine-service.exe --hash-lookup request.json
```

```json
{"schema":1,"sha256":"<64 hexadecimal characters>","online":true,"providers":["circl","malwarebazaar"]}
```

Use `source` with an absolute local path instead of `sha256` to calculate the digest from a file. Both together, unknown fields and unapproved providers are rejected. The operation uses the authenticated antivirus pipe and restricted worker; the privileged broker does not open the caller's file.

## Build, package and validation scope

Build `guardian-engine-service`, `aegis-portable`, `reputation_tests` and `engine_services_tests` in the configured native project, then run `tools/build_winui.ps1` for the matching configuration. `tools/package_portable.py --backend-build <NATIVE_BUILD> --config Release --output <NEW_FOLDER> --stage-only` prepares a development folder. Omitting `--stage-only` requires `--proof <VALIDATION_JSON>` matching current source and binary hashes before a ZIP is produced. `--build-archive <NEW_FOLDER>` additionally preserves source and validation for the private historical build repository.

Validation covers this follow-up and the service/package dependencies it uses, without rerunning historical phase suites. It includes native hash/provider/cache/credential cases, real restricted service calls, service update/uninstall/reinstall, the current protocol, embedded resources, six WinUI locale/theme/width runs, a live query of a public documented CIRCL hash and extracted portable entry-point acceptance. MalwareBazaar live authenticated lookup remains untested until an Auth-Key is configured; request/response handling is tested with inert fixtures. No malware sample is needed or executed.

Native UI tests exercise the actual hash command, service/key control presence, integrated caption/native resize styles, keyboard focus and saved preferences. Administrator consent dialogs are not automated; the fixed elevated registration path is exercised separately. RenderTargetBitmap diagnostics may omit compositor/native caption details and do not certify final physical appearance. The existing clean-machine, signed-update, rollback and platform compatibility work remains in phases 18–19. [Recorded results](PHASE-17.1-RESULTS.json).

References: [Windows title bar](https://learn.microsoft.com/en-us/windows/apps/develop/title-bar), [self-contained Windows App SDK](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-self-contained-apps), [CIRCL hashlookup](https://www.circl.lu/services/hashlookup/), [MalwareBazaar API](https://bazaar.abuse.ch/api/), [VirusTotal API terms](https://docs.virustotal.com/reference/public-vs-premium-api).
