# 14.14 — Baseline for compatibility and readiness for phase 15

**29/10/2026: Replayable evaluation delivered; wide compatibility acceptance not achieved.**This delivery measures the unified flow of 14.13A. It does not implement APIs that are missing or initiate phase 15.

## Protocol frozen before the marketing year

The policy established in 14.1 has been preserved: at least 32 applications, four in each of the eight categories, eight by native architecture x86/x64, 75% funtionals per profile and 50% by category. Fixtures themselves do not count; alternative versions of the same program do not extend the denominator.

The [frozen corpus](../config/nt-compatibility/phase14.14-corpus.json) records 32 application identities, versions by `VS_FIXEDFILEINFO`, SHA-256, architecture, origin, steps and success criteria. They are **13 x86, 18 x64 and a AnyCPU**. AnyCPU is not counted as x86. The selection, freezing and the first campaign were done in that order. No program was replaced after failure. The plan identifier is `c49fc515bb21d1b0fbc84722f1c0f29577f0e9228823eb9f34970277e01b7ed1`.

The sample is of convenience: installed programs and Windows components available locally, with concentration on Microsoft products. They are distinct applications, not 32 independent suppliers or projects. It does not represent the Windows ecosystem and does not support a statement about most programs. Executables have been copied to a local cache, no changes, and checked before/later from the campaign. Neither they nor Microsoft DLLs are redistributed.

Hello World comes from the independent C font already fixed on 14.12. Its source is unchanged, but binary uses its own input and a literal subset of CRT. This adaptation is in the plan. It is not an original distribution Windows executable. The version without version feature is identified by hash and provenance.

The expectations of Sort, Findstr, More and Ping were consulted in the primary documentation: [Sort](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sort), [Findstr](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/findstr), [More](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/more) and [Ping](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/ping). These expectations do not replace a native reference executed.

## Method and measurement border

`tools/compatibility_campaign.py` freezes entries, executes controllers, and aggregates results by application, category, architecture, profile, and configuration. Uses stable torques of 14.13A, with registered hashes; does not recompile or update installed services. A fixture itself checks WHP/VM before each setting, outside the independent denominator.

Each EXE of the offered profile is submitted to the controller with the standard 14.13A catalog and four-second running budget. The preparation maintains its DLL limits, 8 MiB image and storage. Most are refused in preparation before entering the application. `dispatchAttempted` indicates call to the controller, no EXE execution proof in the guest.

The only automated oracle of full use of this campaign is the Hello World: exact expected line, zero output, full report and no live resources or default. The others have frozen complete scripts, but this campaign does an initialization scan. Harness still does not provide arguments, auxiliary files, laboratory servers or automation specific to these scripts. If startup is already rejected, blocking prevents the script; if the application runs, it remains partial without testimony to the intended use. Window created, exported solved or zero output code does not suffice.

These results are recorded:

- ** Functional:** the script has sufficient evidence of the intended use;
- **Partial:**loaded/executed, but the script has not been completed or verified;
- **uncompatible:** the current flow found a preparation barrier, format,
  ability or kernel before obtaining verifiable use;
- **Not-evaluated:** the profile is not offered by unified runtime.

A recognized infrastructure failure invalidates the campaign without artificially reducing the compatibility rate. Sources, binaries, plan and inputs must remain stable. Every cell belongs to the frozen denominator; unexecuted profiles retain zero observed rate in JSON (`null`), and not a measured rate of 0%. The NT identifier is not exchanged to fake another execution.

The selected executable and its DLLs are not executed/loaded in the host. Only controllers/helpers itself are started. There was no native execution of the reference programs, change of Windows protections or request for lifting. Collection of the version feature is metadata reading, non-validation of the subscription or supplier.

## Result

Application matrix and sanitized complete data: [table](PHASE-14.14-RESULTS.md) · [JSON](PHASE-14.14-RESULTS.json).

| Configuration | Profile | Functional | Partial | Incompatible | Not evaluated |
|---|---|---:|---:|---:|---:|
| Release | NT 10.0.19045 | 1/32 | 1 | 30 | 0 |
| Debug | NT 10.0.19045 | 1/32 | 1 | 30 | 0 |
| Release and Debug, each | NT 6.1.7601 | 0 | 0 | 0 | 32 |
| Release and Debug, each | NT 10.0.22631 | 0 | 0 | 0 | 32 |

**3,125% dthe selected corpus** completed the script in the Win10 profile, in both configurations. It is only the adapted Hello World. 31 executable from applications/components installed, none completed their script. Flappy partially executed; no playable match was proven.

In Win10, the barriers were repeated in both configurations:

| Barrier | Applications |
|---|---:|
| Imports unavailable nos templates automatic | 25 |
| Image larger than the current limit: CMake, qBittorrent and Audacity | 3 |
| reader/loader/kernel rejection: FileZilla and NASM | 2 |
| Partial execution: Flappy | 1 |
| Screenplay observed: Hello World | 1 |

FileZilla has reached `Import directory quota`. NASM has been rejected by the loader, with `error=7` and kernel status 50712; it was not a native NASM execution failure on Windows. Flappy bumps into CRT auxiliary features and limitations. Frequent occurrences include `kernel32!FormatMessageW` and MSVCRT x86 exports. A missing model symbol may exist in another package; the report does not automatically transform it into an API without implementation throughout the project.

Unified runtime offers only the Win10 profile. Native compliance of exact builds 7601, 19045 and 22631 has not been evaluated. Components extracted from a newer Windows installation may require that system; its results here do not demonstrate regression compared to actual Windows 10. Profile selection and native references need to be expanded in a future campaign.

As 192 cells are 32 applications × three profiles × two configurations; are not 192 applications or 192 executions of apps on the guest. 64 the controller’s assessment calls, and 128 profile cells not offered. No category reached 50%; console has reached 25%. 75% by profile has not been reached. Cataloged function count does not equal functional applications.

## Observation performance and limits

Each executed cell records total time, CPU, set/commit peaks, private I/O and bytes of the controller**, including preparation and reports. Data does not include RAM/helper processes, do not measure total physical RAM and should not be presented as total Sandbox consumption. Boots separately record configured RAM from VM, 256 MiB, and duration of the guest snippet.

The controller's median time was approximately 176 ms in Release and 172 ms in Debug, dominated by the preparation refusals. The controller's largest private commit peaks were approximately 23.81 and 32.79 MiB. They are observations of a round under host load, without native reference, statistical intervals or performance comparison between NT systems. They do not measure performance of a typical functional application nor guarantee absence of leaks in prolonged use.

## Previous pendencies and readiness for phase 15

The complete suites of the old phases have not been repeated. The Windows result of [12B–13](WINDOWS-PHASES12B-13-REVIEW-20261023.md) is historical; this campaign does not invalidate it and does not convert it into broad compatibility.

| Area anterior | Relevant limit preserved | Effect on the next phase |
|---|---|---|
| 8 and 14.2–14.12: PE, DLLs, cases | ABI, architectures, images and partial APIs; not a full NT | Require inconclusive result when no coverage |
| 9–10 and 14.9: devices/network | This stream is offline, no network fixture of the scripts | Do not claim real network behavior analysis in this campaign |
| 11, 12F and 14.10: graphs/entry | Window bridge and typed calls without full graphical compatibility | Product UI should explain resource unavailable |
| 12B-12E: environment/semantics | Exact comparison with NT versions and representative evaluation remain pending | No claims of sandbox invisibility |
| 13: Analysis | Telemetry of instrumented operations; not every local DLL function generates event | Compatibility does not measure malware detection |
| 14.13: recovery | Basic USER32, a vCPU; living GDI/DirectX/audio and open connections outside the contract | Quarantine cannot depend on a full snapshot of any app |

**Ready to start implementation of phase 15, with basic product scope; not ready to launch with broad compatibility promise.**Quanity can be developed/tested with inert files and static verification, without relying on running incompatible programs in MiniNT.

The `src/Quarantine.cpp` review found simple XOR storage, non-transactional metadata, and a truncating fallback of the original after deletion failure. Phase 15 should replace this flow before its acceptance: integrity, file identity, permissions, durable publication, conflicts, lack of space, and failures need to preserve the original. The campaign did not trigger this quarantine or modify the original files of the programs.

Next batches of compatibility are recorded: explicit arguments and asset; CRT/API-set/ordinais and central functions x86; quotas/loader for identified cases; then complete scripts, references and NT profiles. These batches have not been started. There is no measurement of antimalware efficacy, false positives, escape resistance or release certification here.

## Playout and package

```text
python test/CompatibilityCampaignTests.py RESULTADO.json
python tools/compatibility_campaign.py freeze --selection SELECAO --bindings CAMINHOS_LOCAIS --policy POLITICA --output NOVO_PLANO --cache NOVO_CACHE
python tools/compatibility_campaign.py run --plan PLANO --cache CACHE_CONGELADO --build BUILD --config Release --output NOVA_PASTA --reference CRT_LOCAL --schema-reference SCHEMA_LOCAL
python tools/compatibility_campaign.py report --plan PLANO --release RESULTADOS_RELEASE --debug RESULTADOS_DEBUG --output RELATORIO.json
```

The local cache requires the same bytes by SHA-256; different versions require a new plan, without overwriting the historical campaign. Local paths do not go to public GitHub. `test/CompatibilityCampaignTests.py` has approved **24/24** cases of policy, identity, denominators, oracles, integrity and infrastructure errors. This validates the measurement, does not approve compatibility of the programs.

`tools/package_compatibility_campaign.py` requires current evidence in Release and Debug and these tests. The private file contains sources, own controllers and evidence without third-party EXEs, Microsoft DLLs or memory snapshots. Extract is checked by hash; the collector and tests are executed from the package, including a functional case, a refusal by imports and unsupported profiles. Phase 15 awaits further request.
