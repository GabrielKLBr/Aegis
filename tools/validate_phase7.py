"""Verify phase-7 evidence; optional executions use only trusted project programs."""
import argparse
import datetime
import pathlib
import re
import xml.etree.ElementTree as ET
from validate_phase3 import ROOT, NS, require, read, write, sha, execute, inspect_image, project_boundaries


def process_boot(row):
    p, cpu = row["processes"], row["cpu"]
    require(row["processMode"] and row["processReportValid"] and row["cpuReportValid"] and
            p["checks"] == 32767 and p["coreChecks"] >= 693 and p["completed"] == 2 and p["faulted"] == 1 and
            p["denied"] >= 6 and p["inheritance"] == 1 and p["remoteCopies"] == 2 and p["ticks"] >= 4 and
            p["switches"] >= 15 and p["freeBefore"] == p["freeAfter"] + 21 and
            all(p[k] == 0 for k in ("liveProcesses", "liveObjects", "liveHandles", "liveReferences")),
            "Process acceptance incomplete")
    require(not row["memoryFault"] and not cpu["fatal"] and cpu["userEntries"] == 1 and cpu["userFaults"] == 1 and
            cpu["timerTicks"] == cpu["timerUserTicks"] == cpu["timerAcknowledged"] == p["ticks"] and
            len(cpu["faults"]) == 1 and all(cpu["faults"][0][k] == v for k, v in
            (("vector", 14), ("error", 5), ("cpl", 3), ("cr2", 0x100000))), "Process CPU isolation incomplete")


def io_boot(row):
    process_boot(row)
    x = row["io"]
    require(row["ioMode"] and row["ioReportValid"] and x["checks"] == 4095 and x["coreChecks"] == 630 and
            x["loads"] == x["unloads"] == 3 and x["submitted"] == x["completed"] and x["submitted"] >= 6 and
            x["cancelled"] >= 1 and not x["failed"] and not x["liveDrivers"] and not x["liveRequests"] and not x["livePins"],
            "I/O lifecycle/queue acceptance incomplete")
    require(x["consoleBytes"] == x["hostConsole"] == 6 and x["channelBytes"] == x["hostChannel"] == 16 and
            x["blockBytes"] == x["hostBlock"] >= 1024 and x["hostRequests"] >= 5 and x["console"] == "IO OK\n" and
            x["ticks"] == row["cpu"]["timerTicks"] >= 4, "I/O driver/host accounting incomplete")


def storage_boot(row):
    io_boot(row)
    x = row["storage"]
    require(row["storageMode"] and row["storageReportValid"] and x["checks"] == 4095 and x["coreChecks"] == 784 and
            x["sequenceAfter"] == x["sequenceBefore"] + 2 and x["recovered"] in (0, 1) and
            x["userCalls"] >= 10 and x["reads"] >= 1 and x["writes"] >= 1 and x["registryChanges"] == 2 and
            x["denials"] >= 1 and not x["liveHandles"] and not x["liveWatches"] and x["hostRecords"] >= 95,
            "Persistent volume acceptance incomplete")


def restart_pairs(rows):
    require(len(rows) % 2 == 0, "Incomplete restart pair")
    for i in range(0, len(rows), 2):
        a, b = rows[i], rows[i+1]
        require(a["ramBytes"] == b["ramBytes"] and a["storage"]["recovered"] == 0 and a["storage"]["sequenceBefore"] == 0 and
                b["storage"]["recovered"] == 1 and b["storage"]["sequenceBefore"] == a["storage"]["sequenceAfter"], "Restart lost file/registry state")


def matrices(build):
    output = []
    for config in ("Debug", "Release"):
        log = (build / f"phase7-full-{config.lower()}-script.log").read_text(encoding="utf-8", errors="replace")
        require("100% tests passed, 0 tests failed out of 31" in log and not re.search(r"\*\*\*(?:Skipped|Not Run|Failed)", log),
                "CTest incomplete: " + config)
        details = (build / f"phase7-{config.lower()}-tests-details.log").read_text(encoding="utf-8", errors="replace")
        require("693 freestanding process/security checks PASS" in details and "983 freestanding scheduler checks PASS" in details,
                "Native core checks missing: " + config)
        require("630 freestanding I/O checks PASS" in details and "95 host I/O boundary checks PASS" in details, "I/O unit evidence missing")
        require("784 freestanding storage checks PASS" in details and "32 persistent host/overlay checks PASS" in details, "Storage unit evidence missing")
        for name, filename, negatives in (("storage", f"phase7-boots-{config}", 18),
                                         ("io", f"phase7-regression-phase6-boots-{config}", 22),
                                         ("processes", f"phase7-regression-phase5-boots-{config}", 12),
                                         ("threads", f"phase7-regression-phase4-boots-{config}", 12),
                                         ("memory", f"phase7-regression-phase3-boots-{config}", 18),
                                         ("cpu", f"phase7-regression-phase2-boots-{config}", 16),
                                         ("boot", f"phase7-regression-phase1-boots-{config}", 22)):
            data = read(build / (filename + ".json")); rows = data["boots"]
            require(data["backend"] == "WHP" and data["configuration"] == config and len(rows) == 40, "Incomplete boot matrix")
            for ram in (16 << 20, 64 << 20):
                require(sum(r["ramBytes"] == ram for r in rows) == 20, "Missing RAM case")
            for r in rows:
                require(r["success"] and not r["timedOut"] and r["stopStatus"] == 0 and r["cpl"] == 0 and r["wallMs"] < 3000,
                        "Boot failed")
                if name == "storage":
                    storage_boot(r)
                elif name == "io":
                    io_boot(r)
                elif name == "processes":
                    process_boot(r)
                elif name == "threads":
                    t, c = r["threads"], r["cpu"]
                    require(r["threadReportValid"] and t["checks"] == 32767 and t["coreChecks"] >= 983 and t["completed"] == 8 and
                            t["faults"] == 1 and t["apcs"] == 1 and t["ticks"] >= 4 and t["idleTicks"] >= 1 and t["preemptions"] >= 4 and
                            t["counters"][0] > 0 and t["counters"][1] > 0 and t["counters"][2:] == [16, 16] and
                            t["freeBefore"] == t["freeAfter"] + 21 and c["timerTicks"] == c["timerUserTicks"] == t["ticks"] and
                            c["userEntries"] == 2 and c["userFaults"] == 1 and not r["memoryFault"], "Thread regression")
                elif name == "memory":
                    m, c = r["memory"], r["cpu"]
                    require(r["memoryReportValid"] and m["checks"] == 4095 and m["freeBefore"] == m["freeAfter"] + 21 and
                            m["cycles"] == 128 and m["allocationFailures"] == 30 and m["heapChecks"] == 5 and m["guardFaults"] == 1 and
                            m["userFaults"] == 8 and c["userEntries"] == 150 and c["userCompleted"] == 142 and not c["fatal"] and
                            [f["error"] for f in m["faults"]] == [4, 7, 4, 4, 21, 5, 5, 4] and not r["memoryFault"], "Memory regression")
                elif name == "cpu":
                    require(r["cpu"]["checks"] == 2047 and r["cpu"]["userFaults"] == 6 and r["cpu"]["kernelFaults"] == 2, "CPU regression")
            if name == "storage":
                restart_pairs(rows)
            negative = read(build / (filename + ".negative.json"))["boots"]
            require(len(negative) == negatives and all(not r["success"] and r["error"] for r in negative), "Negative matrix incomplete")
            if name in ("storage", "io", "processes", "threads"):
                require(negative[-1]["timedOut"] and all(not r["timedOut"] for r in negative[:-1]), "Watchdog/report mismatch")
            output.append({"configuration": config, "workload": name, "positive_boots": len(rows), "negative_scenarios": len(negative),
                           "min_boot_ms": min(r["wallMs"] for r in rows), "max_boot_ms": max(r["wallMs"] for r in rows)})
    return output


def boundaries(build):
    rows = project_boundaries(build)
    for target in ("guardian_kernel_threads", "guardian_kernel_processes", "guardian_kernel_io", "guardian_kernel_storage"):
        root = ET.parse(build / "guest" / (target + ".vcxproj")).getroot()
        for group in root.findall("m:ItemDefinitionGroup", NS):
            compiler = group.find("m:ClCompile", NS)
            if compiler is None:
                continue
            c = {p.tag.split("}")[-1]: (p.text or "").strip() for p in compiler}
            l = {p.tag.split("}")[-1]: (p.text or "").strip() for p in group.find("m:Link", NS)}
            require(c["LanguageStandard"] == "stdcpp20" and c["BasicRuntimeChecks"] == "Default" and not c["RuntimeLibrary"] and
                    not c["ExceptionHandling"] and c["RuntimeTypeInfo"] == "false" and c["OmitDefaultLibName"] == "true" and
                    "/kernel" in c["AdditionalOptions"] and l["IgnoreAllDefaultLibraries"] == "true" and not l["AdditionalDependencies"] and
                    l["SubSystem"] == "Native", "Freestanding boundary failed: " + target)
            rows.append({"target": target, "configuration": group.get("Condition"), "compiler": c, "linker": l})
    require(len(rows) == 32, "Guest build boundary count changed")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    parser.add_argument("--build", default=ROOT / "build-codex", type=pathlib.Path)
    parser.add_argument("--run-programs", action="store_true")
    args = parser.parse_args(); build = args.build.resolve(); snapshot = args.snapshot.resolve()
    policy = read(ROOT / "config/phase7-acceptance.json")
    require(policy["required_ctest_count"] == 31 and policy["per_positive_boot"]["core_storage_checks"] == 784 and
            policy["stop_after_completion"] and policy["next_phase"] == 8, "Acceptance contract mismatch")
    matrix = matrices(build)
    historical = read(snapshot / "historical-evidence.json")
    for item in historical:
        require(sha(pathlib.Path(item["Path"])) == item["Sha256"].lower(), "Historical evidence changed: " + item["Path"])
    projects = boundaries(build); write(build / "phase7-build-boundaries.json", projects)
    names = ("guardian-kernel", "guardian-kernel-cpu", "guardian-kernel-memory", "guardian-kernel-threads", "guardian-kernel-processes", "guardian-kernel-io", "guardian-kernel-storage")
    images = [inspect_image(build / "guest" / c / (n + ".gk")) for c in ("Debug", "Release") for n in names]
    write(build / "phase7-image-audit.json", images)
    if args.run_programs:
        refs, smoke = [], []
        for config in ("Debug", "Release"):
            for name in ("vmntdll", "vmntfile", "vmntsect", "vmntimage", "vmntquery", "vmntreg", "vmwininit"):
                refs.append(execute([build / config / (name + ".exe")], config, name))
            tool = build / config / "guardian_boot.exe"
            for executable in ("av.exe", "guardian_boot.exe"):
                smoke.append(execute([build / config / executable, "--help"], config, "help: " + executable))
            for name in names:
                smoke.append(execute([tool, "--check", build / "guest" / config / (name + ".gk")], config, "PE: " + name))
            volume = build / ("phase7-cli-volumes-" + config) / datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d%H%M%S%f")
            volume.mkdir(parents=True)
            smoke.append(execute([tool, build / "guest" / config / "guardian-kernel-storage.gk", "--storage", "--ram-mib", "64", "--repeat", "2",
                                  "--disk", volume / "base.gvd", "--overlay", volume / "session.gvo", "--create-disk",
                                  "--report", build / f"phase7-cli-{config}.json"], config, "persistent CLI restart pair"))
        write(build / "phase7-reference-programs.json", refs); write(build / "phase7-smoke.json", smoke)
    refs = read(build / "phase7-reference-programs.json"); smoke = read(build / "phase7-smoke.json")
    require(len(refs) == 14 and len(smoke) == 20 and all(r["exit_code"] == 0 for r in refs + smoke), "Reference/smoke validation missing")
    artifacts = []
    for config in ("Debug", "Release"):
        cli = read(build / f"phase7-cli-{config}.json")["boots"]
        require(len(cli) == 2 and all(r["success"] for r in cli), "Storage CLI failed")
        for row in cli:
            storage_boot(row)
        restart_pairs(cli)
        exes = sorted((build / config).glob("*.exe")); kernels = sorted((build / "guest" / config).glob("*.gk"))
        require(len(exes) == 60 and len(kernels) == 9, "Incomplete build artifacts: " + config)
        artifacts.append({"configuration": config, "executables": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in exes],
                          "kernels": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in kernels]})
    clean = (build / "phase7-clean-debug-build.log").read_text(encoding="utf-8", errors="replace")
    require(clean.count("Kernel PE32+ x64 valid") == 7 and "error " not in clean.lower(), "Clean build audit incomplete")
    volumes = [p for folder in build.glob("phase7-*") if folder.is_dir() for p in folder.rglob("*") if p.is_file()]
    require(volumes and any(p.suffix == ".gvo" for p in volumes), "Persistent disk artifacts missing")
    for p in volumes:
        if p.name == "base.gvd":
            data = p.read_bytes()
            require(len(data) == 65536 and not any(data), "Owned blank base disk was modified: " + str(p))
    modified = ["guest/CpuKernel.cpp", "guest/ProcessKernel.cpp", "guest/ProcessUser.asm", "guest/IoKernel.h", "guest/IoKernel.cpp", "shared/guardian/Io.h",
                "guest/CMakeLists.txt", "cmake/GuardianPlatform.cmake", "src/microvm/boot/IoBackend.h", "src/microvm/boot/IoBackend.cpp",
                "src/microvm/boot/KernelBoot.h", "src/microvm/boot/KernelBoot.cpp", "src/microvm/boot/BootMain.cpp", "README.md", "docs/ROADMAP-COMPLETO.md"]
    created = ["shared/guardian/Storage.h", "guest/Volume.h", "guest/Volume.cpp", "guest/StorageChecks.cpp", "guest/StorageKernel.h", "guest/StorageKernel.cpp",
               "src/microvm/boot/PersistentBlockStore.h", "src/microvm/boot/PersistentBlockStore.cpp", "test/GuestStorageCoreTests.cpp", "test/GuestStorageHostTests.cpp", "test/GuestStorageBootTests.cpp",
               "config/phase7-acceptance.json", "docs/PHASE-7.md", "tools/validate_phase7.py"]
    evidence = [p for p in build.glob("phase7-*") if p.is_file() and p.name not in
                ("phase7-change-manifest.json", "phase7-verification.log", "phase7-final-verification.log")]
    manifest = {"phase": 7, "status": "complete", "next_phase_started": False,
                "verified_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "base_directory": str(ROOT), "backup_directory": str(snapshot),
                "modified": [{"path": p, "before_sha256": sha(snapshot / p), "after_sha256": sha(ROOT / p)} for p in modified],
                "created": [{"path": p, "sha256": sha(ROOT / p)} for p in created], "historical_evidence_files_unchanged": len(historical),
                "ctest_tests_per_configuration": 31, "skipped": 0, "matrix": matrix, "reference_programs": len(refs),
                "build_boundaries_checked": len(projects), "images": images, "build_artifacts": artifacts,
                "storage_artifacts": [{"path": str(p.relative_to(ROOT)), "bytes": p.stat().st_size, "sha256": sha(p)} for p in sorted(volumes)],
                "evidence": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in sorted(evidence)],
                "windows_boot_or_security_settings_changed": False}
    write(build / "phase7-change-manifest.json", manifest)
    print(f"PASS: phase 7, 31 tests/configuration, 560 matrix boots, {len(historical)} historical evidence files unchanged, 14 reference programs")


if __name__ == "__main__":
    main()
