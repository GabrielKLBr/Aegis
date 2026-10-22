"""Audit the phase-8 loader milestone; never mark the entire phase complete."""
import argparse
import datetime
import pathlib
import re
import xml.etree.ElementTree as ET
from validate_phase3 import ROOT, NS, require, read, write, sha, execute, inspect_image
from validate_phase7 import boundaries, storage_boot, io_boot, process_boot, restart_pairs

NAMES = ("guardian-kernel", "guardian-kernel-cpu", "guardian-kernel-memory", "guardian-kernel-threads",
         "guardian-kernel-processes", "guardian-kernel-io", "guardian-kernel-storage", "guardian-kernel-loader")
CREATED = ["guest/PeLoader.h", "guest/PeLoader.cpp", "guest/PeKernel.h", "guest/PeKernel.cpp",
           "guest/fixtures/PeTls.h", "guest/fixtures/PeDll.cpp", "guest/fixtures/PeDll.def", "guest/fixtures/PeConsole.cpp",
           "tools/embed_pe_fixtures.py", "test/GuestPeLoaderTests.cpp", "test/GuestPeBootTests.cpp",
           "config/phase8-acceptance.json", "docs/PHASE-8.md", "tools/validate_phase8.py"]


def pe_boot(row):
    p, cpu, io, disk = row["processes"], row["cpu"], row["io"], row["storage"]
    require(row["processReportValid"] and row["cpuReportValid"] and p["checks"] == 32767 and p["coreChecks"] == 693 and
            p["completed"] == 2 and p["faulted"] == 1 and p["inheritance"] == 1 and p["remoteCopies"] == 2 and p["denied"] >= 6 and
            p["ticks"] >= 4 and p["switches"] >= 15 and p["freeBefore"] == p["freeAfter"] + 21 and
            all(p[k] == 0 for k in ("liveProcesses", "liveObjects", "liveHandles", "liveReferences")), "PE process cleanup/isolation incomplete")
    # The PE console adds one genuine CPL3 entry before the legacy workload.
    require(not row["memoryFault"] and not cpu["fatal"] and cpu["userEntries"] == 2 and cpu["userFaults"] == 1 and
            cpu["timerTicks"] == cpu["timerUserTicks"] == cpu["timerAcknowledged"] == p["ticks"] and len(cpu["faults"]) == 1 and
            all(cpu["faults"][0][k] == v for k, v in (("vector", 14), ("error", 5), ("cpl", 3), ("cr2", 0x100000))), "PE CPU acceptance incomplete")
    require(row["ioReportValid"] and io["checks"] == 4095 and io["coreChecks"] == 630 and io["loads"] == io["unloads"] == 3 and
            io["submitted"] == io["completed"] >= 6 and io["cancelled"] >= 1 and not io["failed"] and
            not io["liveDrivers"] and not io["liveRequests"] and not io["livePins"] and io["consoleBytes"] == io["hostConsole"] == 6 and
            io["channelBytes"] == io["hostChannel"] == 16 and io["blockBytes"] == io["hostBlock"] >= 1024 and
            io["hostRequests"] >= 5 and io["console"] == "IO OK\n" and io["ticks"] == cpu["timerTicks"], "PE I/O regression")
    require(row["storageReportValid"] and disk["checks"] == 4095 and disk["coreChecks"] == 784 and
            disk["sequenceAfter"] == disk["sequenceBefore"] + 2 and disk["recovered"] in (0, 1) and disk["userCalls"] >= 10 and
            disk["reads"] >= 1 and disk["writes"] >= 1 and disk["registryChanges"] == 2 and disk["denials"] >= 1 and
            not disk["liveHandles"] and not disk["liveWatches"] and disk["hostRecords"] >= 95, "PE persistence regression")
    require("Guardian kernel 0.8.0 PE MAPPING\n" in row["console"] and
            "PE loader mapping checks PASS\n" in row["console"] and
            "PE console TLS DLL execution PASS\n" in row["console"], "PE guest mapping/execution acceptance missing")


def matrices(build):
    result, native = [], []
    workloads = (("loader", "phase8-loader-boots", 0), ("storage", "phase8-regression-phase7-boots", 18),
                 ("io", "phase8-regression-phase6-boots", 22), ("processes", "phase8-regression-phase5-boots", 12),
                 ("threads", "phase8-regression-phase4-boots", 12), ("memory", "phase8-regression-phase3-boots", 18),
                 ("cpu", "phase8-regression-phase2-boots", 16), ("boot", "phase8-regression-phase1-boots", 22))
    for config in ("Debug", "Release"):
        log = (build / f"phase8-full-{config.lower()}-script.log").read_text(encoding="utf-8", errors="replace")
        require("100% tests passed, 0 tests failed out of 33" in log and
                not re.search(r"\*\*\*(?:Skipped|Not Run|Failed)", log), "CTest incomplete: " + config)
        details = (build / f"phase8-{config.lower()}-tests-details.log").read_text(encoding="utf-8", errors="replace")
        match = re.search(r"(\d+) freestanding PE loader checks PASS", details)
        faults = re.search(r"(\d+) backend fault points checked", details)
        require(match and int(match[1]) >= 600 and faults and int(faults[1]) > 100, "PE negative/fault sweep missing")
        for text in ("784 freestanding storage checks PASS", "32 persistent host/overlay checks PASS",
                     "630 freestanding I/O checks PASS", "95 host I/O boundary checks PASS",
                     "693 freestanding process/security checks PASS", "983 freestanding scheduler checks PASS"):
            require(text in details, "Missing native regression: " + text)
        native.append({"configuration": config, "pe_checks": int(match[1]), "backend_fault_points": int(faults[1])})
        for kind, prefix, count in workloads:
            doc = read(build / f"{prefix}-{config}.json"); rows = doc["boots"]
            require(doc["backend"] == "WHP" and doc["configuration"] == config and len(rows) == 40, "Missing boot matrix: " + kind)
            for ram in (16 << 20, 64 << 20):
                require(sum(r["ramBytes"] == ram for r in rows) == 20, "Missing RAM case: " + kind)
            for row in rows:
                require(row["success"] and not row["timedOut"] and not row["stopStatus"] and row["cpl"] == 0 and row["wallMs"] < 3000, "Failed boot: " + kind)
                if kind == "loader":
                    pe_boot(row)
                elif kind == "storage":
                    storage_boot(row)
                elif kind == "io":
                    io_boot(row)
                elif kind == "processes":
                    process_boot(row)
                elif kind == "threads":
                    x = row["threads"]
                    require(row["threadReportValid"] and x["checks"] == 32767 and x["coreChecks"] == 983 and x["completed"] == 8 and x["faults"] == 1 and
                            x["apcs"] == 1 and x["idleTicks"] >= 1 and x["preemptions"] >= 4 and x["freeBefore"] == x["freeAfter"] + 21, "Scheduler regression")
                elif kind == "memory":
                    x = row["memory"]
                    require(row["memoryReportValid"] and x["checks"] == 4095 and x["cycles"] == 128 and x["allocationFailures"] == 30 and x["heapChecks"] == 5 and
                            x["freeBefore"] == x["freeAfter"] + 21 and [f["error"] for f in x["faults"]] == [4, 7, 4, 4, 21, 5, 5, 4], "Memory regression")
                elif kind == "cpu":
                    require(row["cpu"]["checks"] == 2047 and row["cpu"]["userFaults"] == 6 and row["cpu"]["kernelFaults"] == 2, "CPU regression")
            if kind == "storage":
                restart_pairs(rows)
            if count:
                negatives = read(build / f"{prefix}-{config}.negative.json")["boots"]
                require(len(negatives) == count and all(not r["success"] and r["error"] for r in negatives), "Missing negative matrix: " + kind)
                if kind in ("storage", "io", "processes", "threads"):
                    require(negatives[-1]["timedOut"] and all(not r["timedOut"] for r in negatives[:-1]), "Watchdog mismatch")
            result.append({"configuration": config, "workload": kind, "boots": 40, "external_failure_cases": count,
                           "min_ms": min(r["wallMs"] for r in rows), "max_ms": max(r["wallMs"] for r in rows)})
    return result, native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    parser.add_argument("--build", default=ROOT / "build-codex", type=pathlib.Path)
    parser.add_argument("--run-programs", action="store_true")
    args = parser.parse_args(); build, snapshot = args.build.resolve(), args.snapshot.resolve()
    policy = read(ROOT / "config/phase8-acceptance.json")
    require(policy["status"] == "in_progress" and policy["remaining_before_phase_completion"] and policy["next_phase"] == 9 and policy["required_ctest_count"] == 33,
            "Loader milestone must not claim phase completion")
    matrix, native = matrices(build)
    historical = read(snapshot / "historical-evidence.json")
    for item in historical:
        require(sha(pathlib.Path(item["Path"])) == item["Sha256"].lower(), "Historical evidence changed: " + item["Path"])
    old = read(build / "phase7-change-manifest.json")
    for item in old["storage_artifacts"]:
        require(sha(ROOT / item["path"]) == item["sha256"], "Prior phase disk artifact changed: " + item["path"])
    projects = boundaries(build)
    project = ET.parse(build / "guest/guardian_kernel_loader.vcxproj").getroot()
    for group in project.findall("m:ItemDefinitionGroup", NS):
        compiler = group.find("m:ClCompile", NS)
        if compiler is None:
            continue
        c = {p.tag.split("}")[-1]: (p.text or "").strip() for p in compiler}
        l = {p.tag.split("}")[-1]: (p.text or "").strip() for p in group.find("m:Link", NS)}
        require(c["LanguageStandard"] == "stdcpp20" and c["BasicRuntimeChecks"] == "Default" and not c["RuntimeLibrary"] and not c["ExceptionHandling"] and
                c["RuntimeTypeInfo"] == "false" and c["OmitDefaultLibName"] == "true" and "/kernel" in c["AdditionalOptions"] and
                l["IgnoreAllDefaultLibraries"] == "true" and not l["AdditionalDependencies"] and l["SubSystem"] == "Native", "Loader guest boundary changed")
        projects.append({"target": "guardian_kernel_loader", "configuration": group.get("Condition"), "compiler": c, "linker": l})
    require(len(projects) == 36, "Guest build boundary count changed")
    write(build / "phase8-build-boundaries.json", projects)
    images = [inspect_image(build / "guest" / c / (name + ".gk")) for c in ("Debug", "Release") for name in NAMES]
    write(build / "phase8-image-audit.json", images)
    if args.run_programs:
        refs, smoke = [], []
        for config in ("Debug", "Release"):
            for name in ("vmntdll", "vmntfile", "vmntsect", "vmntimage", "vmntquery", "vmntreg", "vmwininit"):
                refs.append(execute([build / config / (name + ".exe")], config, name))
            tool = build / config / "guardian_boot.exe"
            for name in ("av.exe", "guardian_boot.exe"):
                smoke.append(execute([build / config / name, "--help"], config, "help: " + name))
            for name in NAMES:
                smoke.append(execute([tool, "--check", build / "guest" / config / (name + ".gk")], config, "PE: " + name))
            for kind, image, repeat in (("storage", "guardian-kernel-storage", 2), ("loader", "guardian-kernel-loader", 1)):
                folder = build / ("phase8-cli-volumes-" + config) / datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d%H%M%S%f")
                folder.mkdir(parents=True)
                smoke.append(execute([tool, build / "guest" / config / (image + ".gk"), "--storage", "--ram-mib", "64", "--repeat", str(repeat),
                                      "--disk", folder / "base.gvd", "--overlay", folder / "session.gvo", "--create-disk",
                                      "--report", build / f"phase8-cli-{kind}-{config}.json"], config, kind + " CLI"))
        write(build / "phase8-reference-programs.json", refs); write(build / "phase8-smoke.json", smoke)
    refs, smoke = read(build / "phase8-reference-programs.json"), read(build / "phase8-smoke.json")
    require(len(refs) == 14 and len(smoke) == 24 and all(r["exit_code"] == 0 for r in refs + smoke), "Reference/smoke checks missing")
    artifacts = []
    for config in ("Debug", "Release"):
        storage = read(build / f"phase8-cli-storage-{config}.json")["boots"]
        require(len(storage) == 2 and all(row["success"] for row in storage), "Persistent CLI restart pair missing")
        for row in storage:
            storage_boot(row)
        restart_pairs(storage)
        loader = read(build / f"phase8-cli-loader-{config}.json")["boots"]
        require(len(loader) == 1 and loader[0]["success"], "PE CLI missing"); pe_boot(loader[0])
        exes = sorted((build / config).glob("*.exe")); kernels = sorted((build / "guest" / config).glob("*.gk"))
        fixtures = sorted((build / "guest/fixtures" / config).glob("*.exe")) + sorted((build / "guest/fixtures" / config).glob("*.dll"))
        require(len(exes) == 62 and len(kernels) == 10 and len(fixtures) == 2, "Incomplete build artifacts: " + config)
        artifacts.append({"configuration": config, "files": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in exes + kernels + fixtures]})
    clean = (build / "phase8-clean-debug-build.log").read_text(encoding="utf-8", errors="replace")
    require(clean.count("Kernel PE32+ x64 valid") == 8 and "error " not in clean.lower(), "Clean guest build audit missing")
    volumes = [p for folder in build.glob("phase8-*") if folder.is_dir() for p in folder.rglob("*") if p.is_file()]
    require(volumes and any(p.suffix == ".gvo" for p in volumes), "Disk evidence missing")
    for p in volumes:
        if p.name == "base.gvd":
            data = p.read_bytes(); require(len(data) == 65536 and not any(data), "Owned base disk changed")
    modified = read(snapshot / "originals.json")
    evidence = [p for p in build.glob("phase8-*") if p.is_file() and p.name not in ("phase8-change-manifest.json", "phase8-verification.log", "phase8-final-verification.log")]
    manifest = {"phase": 8, "status": "in_progress", "milestone_status": "validated", "next_phase_started": False,
                "verified_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "base_directory": str(ROOT), "backup_directory": str(snapshot),
                "remaining_before_phase_completion": policy["remaining_before_phase_completion"],
                "modified": [{"path": p, "before_sha256": sha(snapshot / p), "after_sha256": sha(ROOT / p)} for p in modified],
                "created": [{"path": p, "sha256": sha(ROOT / p)} for p in CREATED], "historical_evidence_files_unchanged": len(historical),
                "phase7_disk_artifacts_unchanged": len(old["storage_artifacts"]), "ctest_per_configuration": 33, "skipped": 0,
                "matrix": matrix, "native": native, "reference_programs": len(refs), "build_boundaries_checked": len(projects),
                "images": images, "build_artifacts": artifacts,
                "storage_artifacts": [{"path": str(p.relative_to(ROOT)), "bytes": p.stat().st_size, "sha256": sha(p)} for p in sorted(volumes)],
                "evidence": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in sorted(evidence)],
                "windows_boot_or_security_settings_changed": False}
    write(build / "phase8-change-manifest.json", manifest)
    print(f"PASS: phase 8 loader milestone, 33 tests/configuration, 640 matrix boots, {len(historical)} historical evidence files unchanged; phase 8 remains in progress")


if __name__ == "__main__":
    main()
