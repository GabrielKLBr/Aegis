"""Audit the phase-8 virtual unwind milestone without declaring phase completion."""
import argparse
import datetime
import pathlib
import re
import xml.etree.ElementTree as ET
from validate_phase3 import ROOT, NS, require, read, write, sha, execute, inspect_image
from validate_phase7 import boundaries, storage_boot, io_boot, process_boot, restart_pairs

NAMES = ("guardian-kernel", "guardian-kernel-cpu", "guardian-kernel-memory", "guardian-kernel-threads",
         "guardian-kernel-processes", "guardian-kernel-io", "guardian-kernel-storage", "guardian-kernel-loader", "guardian-kernel-runtime", "guardian-kernel-lifetime")
CREATED = ['guest/Unwind.h','guest/Unwind.cpp','test/GuestUnwindTests.cpp','tools/validate_phase8_unwind.py','config/phase8-unwind-acceptance.json','docs/PHASE-8-UNWIND.md']


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
    require(row["ioReportValid"] and io["checks"] == 4095 and io["coreChecks"] == 640 and io["loads"] == io["unloads"] == 3 and
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


def runtime_boot(row):
    p, cpu, io, disk = row["processes"], row["cpu"], row["io"], row["storage"]
    require(row["processReportValid"] and row["cpuReportValid"] and p["checks"] == 32767 and p["coreChecks"] == 693 and
            p["completed"] == 2 and p["faulted"] == 1 and p["inheritance"] == 1 and p["remoteCopies"] == 2 and p["denied"] >= 6 and
            p["ticks"] >= 4 and p["switches"] >= 15 and p["freeBefore"] == p["freeAfter"] + 21 and
            all(p[k] == 0 for k in ("liveProcesses", "liveObjects", "liveHandles", "liveReferences")), "PE process cleanup/isolation incomplete")
    # The PE console adds one genuine CPL3 entry before the legacy workload.
    require(not row["memoryFault"] and not cpu["fatal"] and cpu["userEntries"] == 2 and cpu["userFaults"] == 1 and
            cpu["timerTicks"] == cpu["timerUserTicks"] == cpu["timerAcknowledged"] == p["ticks"] and len(cpu["faults"]) == 1 and
            all(cpu["faults"][0][k] == v for k, v in (("vector", 14), ("error", 5), ("cpl", 3), ("cr2", 0x100000))), "PE CPU acceptance incomplete")
    require(row["ioReportValid"] and io["checks"] == 4095 and io["coreChecks"] == 640 and io["loads"] == io["unloads"] == 3 and
            io["submitted"] == io["completed"] >= 6 and io["cancelled"] >= 1 and not io["failed"] and
            not io["liveDrivers"] and not io["liveRequests"] and not io["livePins"] and io["consoleBytes"] == io["hostConsole"] == 20 and
            io["channelBytes"] == io["hostChannel"] == 16 and io["blockBytes"] == io["hostBlock"] >= 1024 and
            io["hostRequests"] >= 5 and io["console"] == "PE RUNTIME OK\nIO OK\n" and io["ticks"] == cpu["timerTicks"], "PE I/O regression")
    require(row["storageReportValid"] and disk["checks"] == 4095 and disk["coreChecks"] == 784 and
            disk["sequenceAfter"] == disk["sequenceBefore"] + 3 and disk["recovered"] in (0, 1) and disk["userCalls"] >= 10 and
            disk["reads"] >= 1 and disk["writes"] >= 1 and disk["registryChanges"] == 3 and disk["denials"] >= 1 and
            not disk["liveHandles"] and not disk["liveWatches"] and disk["hostRecords"] >= 95, "PE persistence regression")
    require("Guardian kernel 0.8.0 PE MAPPING\n" in row["console"] and
            "PE loader mapping checks PASS\n" in row["console"] and
            "PE console TLS DLL execution PASS\n" in row["console"], "PE guest mapping/execution acceptance missing")

    require("PE runtime services heap locks PASS\n" in row["console"] and
            "PE private TEB TLS thread lifecycle contention PASS\n" in row["console"], "Runtime thread/API acceptance missing")


def lifetime_boot(row):
    runtime_boot(row)
    require("PE x64 virtual stack capture PASS\n" in row["console"], "Virtual stack capture proof missing")
    require("PE child processes explicit exits last-thread lifetime PASS\n" in row["console"] and
            "PE child creation rollback 10 fault budgets PASS\n" in row["console"], "PE child/termination/rollback acceptance missing")


def matrices(build):
    result, native = [], []
    workloads = (("lifetime", "phase8-unwind-boots", 0), ("runtime", "phase8-unwind-regression-runtime-boots", 0), ("loader", "phase8-unwind-regression-loader-boots", 0), ("storage", "phase8-unwind-regression-phase7-boots", 18),
                 ("io", "phase8-unwind-regression-phase6-boots", 22), ("processes", "phase8-unwind-regression-phase5-boots", 12),
                 ("threads", "phase8-unwind-regression-phase4-boots", 12), ("memory", "phase8-unwind-regression-phase3-boots", 18),
                 ("cpu", "phase8-unwind-regression-phase2-boots", 16), ("boot", "phase8-unwind-regression-phase1-boots", 22))
    for config in ("Debug", "Release"):
        log = (build / f"phase8-unwind-full-{config.lower()}-script.log").read_text(encoding="utf-8", errors="replace")
        require("100% tests passed, 0 tests failed out of 37" in log and
                not re.search(r"\*\*\*(?:Skipped|Not Run|Failed)", log), "CTest incomplete: " + config)
        details = (build / f"phase8-unwind-{config.lower()}-tests-details.log").read_text(encoding="utf-8", errors="replace")
        match = re.search(r"(\d+) freestanding PE loader checks PASS", details)
        faults = re.search(r"(\d+) backend fault points checked", details)
        chained = re.search(r"(\d+) chained unwind fault points checked", details)
        require(match and int(match[1]) >= 600 and faults and int(faults[1]) > 100, "PE negative/fault sweep missing")
        require(chained and int(chained[1]) > 100, "Chained unwind fault sweep missing")
        for text in ("185 runtime service/heap checks PASS", "784 freestanding storage checks PASS", "32 persistent host/overlay checks PASS",
                     "630 freestanding I/O checks PASS", "95 host I/O boundary checks PASS",
                     "693 freestanding process/security checks PASS", "983 freestanding scheduler checks PASS"):
            require(text in details, "Missing native regression: " + text)
        require("904 individual thread lifecycle checks PASS" in details, "Thread lifetime/rights/reuse tests missing")
        require("17 native thread opening rights checks PASS" in details and "10 per-process service exit checks PASS" in details, "New native ownership regressions missing")
        unwind = re.search(r"(\d+) freestanding virtual unwind checks PASS; (\d+) Windows differential frames PASS", details)
        injected = re.search(r"(\d+) differential virtual unwind read fault points checked", details)
        require(unwind and int(unwind[1]) >= 2000 and int(unwind[2]) == 59 and injected and int(injected[1]) >= 391, "Virtual unwind/Windows/fault proof missing")
        native.append({"virtual_unwind_checks": int(unwind[1]), "windows_differential_frames": int(unwind[2]), "unwind_read_fault_points": int(injected[1]), "native_thread_opening_rights_checks": 17, "per_process_service_exit_checks": 10, "individual_thread_lifecycle_checks": 904, "configuration": config, "pe_checks": int(match[1]), "backend_fault_points": int(faults[1]), "chained_unwind_fault_points": int(chained[1]), "runtime_service_heap_checks": 185})
        for kind, prefix, count in workloads:
            doc = read(build / f"{prefix}-{config}.json"); rows = doc["boots"]
            require(doc["backend"] == "WHP" and doc["configuration"] == config and len(rows) == 40, "Missing boot matrix: " + kind)
            for ram in (16 << 20, 64 << 20):
                require(sum(r["ramBytes"] == ram for r in rows) == 20, "Missing RAM case: " + kind)
            for row in rows:
                require(row["success"] and not row["timedOut"] and not row["stopStatus"] and row["cpl"] == 0 and row["wallMs"] < 3000, "Failed boot: " + kind)
                if kind == "lifetime":
                    lifetime_boot(row)
                elif kind == "runtime":
                    runtime_boot(row)
                elif kind == "loader":
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
            elif kind in ("runtime", "lifetime"):
                for i in range(0, len(rows), 2):
                    a, b = rows[i:i+2]
                    require(a["storage"]["sequenceBefore"] == 0 and a["storage"]["recovered"] == 0 and
                            b["storage"]["sequenceBefore"] == a["storage"]["sequenceAfter"] == 3 and
                            b["storage"]["recovered"] == 1 and a["ramBytes"] == b["ramBytes"], "Runtime persistence pair incomplete")
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
    policy = read(ROOT / "config/phase8-unwind-acceptance.json")
    require(policy["status"] == "in_progress" and policy["remaining_before_phase_completion"] and policy["next_phase"] == 9 and policy["required_ctest_count"] == 37,
            "Loader milestone must not claim phase completion")
    require(policy["worker_threads_per_runtime_boot"] == 31 and policy["serialized_counter_increments_per_runtime_boot"] == 360 and
            policy["individual_native_thread_checks"] == 904, "Thread runtime acceptance counts changed")
    require(policy["executed_children_per_lifetime_boot"] == 23 and policy["child_creation_fault_budgets_per_lifetime_boot"] == 10 and
            policy["native_thread_opening_rights_checks"] == 17 and policy["per_process_service_exit_checks"] == 10, "Process lifetime acceptance counts changed")
    require(policy["windows_differential_frames"] == 59 and policy["trace_threads_per_lifetime_boot"] == 4 and
            policy["successful_trace_requests_per_lifetime_boot"] == 20, "Virtual unwind policy changed")
    matrix, native = matrices(build)
    historical = read(snapshot / "historical-evidence.json")
    for item in historical:
        require(sha(pathlib.Path(item["Path"])) == item["Sha256"].lower(), "Historical evidence changed: " + item["Path"])
    old = read(build / "phase7-change-manifest.json")
    previous = read(build / "phase8-change-manifest.json")
    for item in previous["storage_artifacts"]:
        require(sha(ROOT / item["path"]) == item["sha256"], "Previous phase-8 disk artifact changed: " + item["path"])
    for item in old["storage_artifacts"]:
        require(sha(ROOT / item["path"]) == item["sha256"], "Prior phase disk artifact changed: " + item["path"])
    prior_runtime = read(build / "phase8-runtime-change-manifest.json")
    for item in prior_runtime["storage_artifacts"]:
        require(sha(ROOT / item["path"]) == item["sha256"], "Previous runtime disk artifact changed: " + item["path"])
    prior_concurrency = read(build / "phase8-concurrency-change-manifest.json")
    for item in prior_concurrency["storage_artifacts"]:
        require(sha(ROOT / item["path"]) == item["sha256"], "Previous concurrency disk artifact changed: " + item["path"])
    prior_lifetime = read(build / "phase8-lifetime-change-manifest.json")
    for item in prior_lifetime["storage_artifacts"]:
        require(sha(ROOT / item["path"]) == item["sha256"], "Previous lifetime disk artifact changed: " + item["path"])
    projects = boundaries(build)
    for target in ("guardian_kernel_loader", "guardian_kernel_runtime", "guardian_kernel_lifetime"):
        project = ET.parse(build / "guest" / (target + ".vcxproj")).getroot()
        for group in project.findall("m:ItemDefinitionGroup", NS):
            compiler = group.find("m:ClCompile", NS)
            if compiler is None:
                continue
            c = {p.tag.split("}")[-1]: (p.text or "").strip() for p in compiler}
            l = {p.tag.split("}")[-1]: (p.text or "").strip() for p in group.find("m:Link", NS)}
            require(c["LanguageStandard"] == "stdcpp20" and c["BasicRuntimeChecks"] == "Default" and not c["RuntimeLibrary"] and not c["ExceptionHandling"] and
                    c["RuntimeTypeInfo"] == "false" and c["OmitDefaultLibName"] == "true" and "/kernel" in c["AdditionalOptions"] and
                    l["IgnoreAllDefaultLibraries"] == "true" and not l["AdditionalDependencies"] and l["SubSystem"] == "Native", "Loader guest boundary changed")
            projects.append({"target": target, "configuration": group.get("Condition"), "compiler": c, "linker": l})
    require(len(projects) == 44, "Guest build boundary count changed")
    write(build / "phase8-unwind-build-boundaries.json", projects)
    images = [inspect_image(build / "guest" / c / (name + ".gk")) for c in ("Debug", "Release") for name in NAMES]
    write(build / "phase8-unwind-image-audit.json", images)
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
            for kind, image, repeat in (("storage", "guardian-kernel-storage", 2), ("loader", "guardian-kernel-loader", 1), ("runtime", "guardian-kernel-runtime", 2), ("lifetime", "guardian-kernel-lifetime", 2)):
                folder = build / ("phase8-unwind-cli-volumes-" + config) / datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d%H%M%S%f")
                folder.mkdir(parents=True)
                smoke.append(execute([tool, build / "guest" / config / (image + ".gk"), "--storage", "--ram-mib", "64", "--repeat", str(repeat),
                                      "--disk", folder / "base.gvd", "--overlay", folder / "session.gvo", "--create-disk",
                                      "--report", build / f"phase8-unwind-cli-{kind}-{config}.json"], config, kind + " CLI"))
        write(build / "phase8-unwind-reference-programs.json", refs); write(build / "phase8-unwind-smoke.json", smoke)
    refs, smoke = read(build / "phase8-unwind-reference-programs.json"), read(build / "phase8-unwind-smoke.json")
    require(len(refs) == 14 and len(smoke) == 32 and all(r["exit_code"] == 0 for r in refs + smoke), "Reference/smoke checks missing")
    artifacts = []
    for config in ("Debug", "Release"):
        storage = read(build / f"phase8-unwind-cli-storage-{config}.json")["boots"]
        require(len(storage) == 2 and all(row["success"] for row in storage), "Persistent CLI restart pair missing")
        for row in storage:
            storage_boot(row)
        restart_pairs(storage)
        loader = read(build / f"phase8-unwind-cli-loader-{config}.json")["boots"]
        require(len(loader) == 1 and loader[0]["success"], "PE CLI missing"); pe_boot(loader[0])
        runtime_rows = read(build / f"phase8-unwind-cli-runtime-{config}.json")["boots"]
        require(len(runtime_rows) == 2 and all(r["success"] for r in runtime_rows), "Runtime CLI restart pair missing")
        for row in runtime_rows:
            runtime_boot(row)
        for i, row in enumerate(runtime_rows):
            require(row["storage"]["sequenceBefore"] == i * 3, "Runtime CLI persistence lost")
        lifetime_rows = read(build / f"phase8-unwind-cli-lifetime-{config}.json")["boots"]
        require(len(lifetime_rows) == 2 and all(r["success"] for r in lifetime_rows), "Lifetime CLI restart pair missing")
        for i, row in enumerate(lifetime_rows):
            lifetime_boot(row)
            require(row["storage"]["sequenceBefore"] == i * 3, "Lifetime CLI persistence lost")
        exes = sorted((build / config).glob("*.exe")); kernels = sorted((build / "guest" / config).glob("*.gk"))
        fixtures = sorted((build / "guest/fixtures" / config).glob("*.exe")) + sorted((build / "guest/fixtures" / config).glob("*.dll"))
        require(len(exes) == 64 and len(kernels) == 12 and len(fixtures) == 6, "Incomplete build artifacts: " + config)
        artifacts.append({"configuration": config, "files": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in exes + kernels + fixtures]})
    clean = (build / "phase8-unwind-clean-debug-build.log").read_text(encoding="utf-8", errors="replace")
    require(clean.count("Kernel PE32+ x64 valid") == 10 and "error " not in clean.lower(), "Clean guest build audit missing")
    volumes = [p for folder in build.glob("phase8-unwind-*") if folder.is_dir() for p in folder.rglob("*") if p.is_file()]
    require(volumes and any(p.suffix == ".gvo" for p in volumes), "Disk evidence missing")
    for p in volumes:
        if p.name == "base.gvd":
            data = p.read_bytes(); require(len(data) == 65536 and not any(data), "Owned base disk changed")
    modified = read(snapshot / "originals.json")
    evidence = [p for p in build.glob("phase8-unwind-*") if p.is_file() and p.name not in ("phase8-unwind-change-manifest.json", "phase8-unwind-verification.log", "phase8-unwind-final-verification.log")]
    manifest = {"phase": 8, "status": "in_progress", "milestone_status": "validated", "next_phase_started": False,
                "verified_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "base_directory": str(ROOT), "backup_directory": str(snapshot),
                "remaining_before_phase_completion": policy["remaining_before_phase_completion"],
                "modified": [{"path": p, "before_sha256": sha(snapshot / p), "after_sha256": sha(ROOT / p)} for p in modified],
                "created": [{"path": p, "sha256": sha(ROOT / p)} for p in CREATED], "historical_evidence_files_unchanged": len(historical),
                "phase7_disk_artifacts_unchanged": len(old["storage_artifacts"]), "previous_phase8_disk_artifacts_unchanged": len(previous["storage_artifacts"]), "previous_runtime_disk_artifacts_unchanged": len(prior_runtime["storage_artifacts"]), "previous_lifetime_disk_artifacts_unchanged": len(prior_lifetime["storage_artifacts"]), "trace_threads_per_lifetime_boot": 4, "successful_trace_requests_per_lifetime_boot": 20, "previous_concurrency_disk_artifacts_unchanged": len(prior_concurrency["storage_artifacts"]), "ctest_per_configuration": 37, "skipped": 0, "executed_children_per_lifetime_boot": 23, "child_creation_fault_budgets_per_lifetime_boot": 10, "worker_threads_per_runtime_boot": 31, "serialized_counter_increments_per_runtime_boot": 360,
                "matrix": matrix, "native": native, "reference_programs": len(refs), "build_boundaries_checked": len(projects),
                "images": images, "build_artifacts": artifacts,
                "storage_artifacts": [{"path": str(p.relative_to(ROOT)), "bytes": p.stat().st_size, "sha256": sha(p)} for p in sorted(volumes)],
                "evidence": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in sorted(evidence)],
                "windows_boot_or_security_settings_changed": False}
    write(build / "phase8-unwind-change-manifest.json", manifest)
    print(f"PASS: phase 8 virtual unwind milestone, 37 tests/configuration, 800 matrix boots, {len(historical)} historical evidence files unchanged; phase 8 remains in progress")


if __name__ == "__main__":
    main()
