"""Verify phase-5 evidence; optional executions use only trusted project programs."""
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


def matrices(build):
    output = []
    for config in ("Debug", "Release"):
        log = (build / f"phase5-full-{config.lower()}-script.log").read_text(encoding="utf-8", errors="replace")
        require("100% tests passed, 0 tests failed out of 25" in log and not re.search(r"\*\*\*(?:Skipped|Not Run|Failed)", log),
                "CTest incomplete: " + config)
        details = (build / f"phase5-{config.lower()}-tests-details.log").read_text(encoding="utf-8", errors="replace")
        require("693 freestanding process/security checks PASS" in details and "983 freestanding scheduler checks PASS" in details,
                "Native core checks missing: " + config)
        for name, filename, negatives in (("processes", f"phase5-boots-{config}", 12),
                                         ("threads", f"phase5-regression-phase4-boots-{config}", 12),
                                         ("memory", f"phase5-regression-phase3-boots-{config}", 18),
                                         ("cpu", f"phase5-regression-phase2-boots-{config}", 16),
                                         ("boot", f"phase5-regression-phase1-boots-{config}", 22)):
            data = read(build / (filename + ".json")); rows = data["boots"]
            require(data["backend"] == "WHP" and data["configuration"] == config and len(rows) == 40, "Incomplete boot matrix")
            for ram in (16 << 20, 64 << 20):
                require(sum(r["ramBytes"] == ram for r in rows) == 20, "Missing RAM case")
            for r in rows:
                require(r["success"] and not r["timedOut"] and r["stopStatus"] == 0 and r["cpl"] == 0 and r["wallMs"] < 3000,
                        "Boot failed")
                if name == "processes":
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
            negative = read(build / (filename + ".negative.json"))["boots"]
            require(len(negative) == negatives and all(not r["success"] and r["error"] for r in negative), "Negative matrix incomplete")
            if name in ("processes", "threads"):
                require(negative[-1]["timedOut"] and all(not r["timedOut"] for r in negative[:-1]), "Watchdog/report mismatch")
            output.append({"configuration": config, "workload": name, "positive_boots": len(rows), "negative_scenarios": len(negative),
                           "min_boot_ms": min(r["wallMs"] for r in rows), "max_boot_ms": max(r["wallMs"] for r in rows)})
    return output


def boundaries(build):
    rows = project_boundaries(build)
    for target in ("guardian_kernel_threads", "guardian_kernel_processes"):
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
    require(len(rows) == 24, "Guest build boundary count changed")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    parser.add_argument("--build", default=ROOT / "build-codex", type=pathlib.Path)
    parser.add_argument("--run-programs", action="store_true")
    args = parser.parse_args(); build = args.build.resolve(); snapshot = args.snapshot.resolve()
    policy = read(ROOT / "config/phase5-acceptance.json")
    require(policy["required_ctest_count"] == 25 and policy["per_positive_boot"]["minimum_core_checks"] == 693 and
            policy["stop_after_completion"] and policy["next_phase"] == 6, "Acceptance contract mismatch")
    matrix = matrices(build)
    historical = read(snapshot / "historical-evidence.json")
    for item in historical:
        require(sha(pathlib.Path(item["Path"])) == item["Sha256"].lower(), "Historical evidence changed: " + item["Path"])
    projects = boundaries(build); write(build / "phase5-build-boundaries.json", projects)
    names = ("guardian-kernel", "guardian-kernel-cpu", "guardian-kernel-memory", "guardian-kernel-threads", "guardian-kernel-processes")
    images = [inspect_image(build / "guest" / c / (n + ".gk")) for c in ("Debug", "Release") for n in names]
    write(build / "phase5-image-audit.json", images)
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
            smoke.append(execute([tool, build / "guest" / config / "guardian-kernel-processes.gk", "--processes", "--ram-mib", "64", "--repeat", "2",
                                  "--report", build / f"phase5-cli-{config}.json"], config, "process CLI boots"))
        write(build / "phase5-reference-programs.json", refs); write(build / "phase5-smoke.json", smoke)
    refs = read(build / "phase5-reference-programs.json"); smoke = read(build / "phase5-smoke.json")
    require(len(refs) == 14 and len(smoke) == 16 and all(r["exit_code"] == 0 for r in refs + smoke), "Reference/smoke validation missing")
    artifacts = []
    for config in ("Debug", "Release"):
        cli = read(build / f"phase5-cli-{config}.json")["boots"]
        require(len(cli) == 2 and all(r["success"] for r in cli), "Process CLI failed")
        for row in cli:
            process_boot(row)
        exes = sorted((build / config).glob("*.exe")); kernels = sorted((build / "guest" / config).glob("*.gk"))
        require(len(exes) == 54 and len(kernels) == 7, "Incomplete build artifacts: " + config)
        artifacts.append({"configuration": config, "executables": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in exes],
                          "kernels": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in kernels]})
    clean = (build / "phase5-clean-debug-build.log").read_text(encoding="utf-8", errors="replace")
    require(clean.count("Kernel PE32+ x64 valid") == 5 and "error " not in clean.lower(), "Clean build audit incomplete")
    modified = ["guest/CpuKernel.cpp", "guest/ThreadKernel.cpp", "guest/Scheduler.h", "guest/Scheduler.cpp", "guest/CMakeLists.txt",
                "cmake/GuardianPlatform.cmake", "src/microvm/boot/KernelBoot.h", "src/microvm/boot/KernelBoot.cpp",
                "src/microvm/boot/BootMain.cpp", "README.md", "docs/ROADMAP-COMPLETO.md"]
    created = ["shared/guardian/Processes.h", "guest/ProcessManager.h", "guest/ProcessManager.cpp", "guest/ProcessChecks.cpp",
               "guest/ExecutionContext.h", "guest/ProcessKernel.h", "guest/ProcessKernel.cpp", "guest/ProcessUser.asm", "guest/StackProbe.asm",
               "test/GuestProcessCoreTests.cpp", "test/GuestProcessBootTests.cpp", "config/phase5-acceptance.json", "docs/PHASE-5.md", "tools/validate_phase5.py"]
    evidence = [p for p in build.glob("phase5-*") if p.is_file() and p.name not in
                ("phase5-change-manifest.json", "phase5-verification.log", "phase5-final-verification.log")]
    manifest = {"phase": 5, "status": "complete", "next_phase_started": False,
                "verified_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "base_directory": str(ROOT), "backup_directory": str(snapshot),
                "modified": [{"path": p, "before_sha256": sha(snapshot / p), "after_sha256": sha(ROOT / p)} for p in modified],
                "created": [{"path": p, "sha256": sha(ROOT / p)} for p in created], "historical_evidence_files_unchanged": len(historical),
                "ctest_tests_per_configuration": 25, "skipped": 0, "matrix": matrix, "reference_programs": len(refs),
                "build_boundaries_checked": len(projects), "images": images, "build_artifacts": artifacts,
                "evidence": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in sorted(evidence)],
                "windows_boot_or_security_settings_changed": False}
    write(build / "phase5-change-manifest.json", manifest)
    print(f"PASS: phase 5, 25 tests/configuration, 400 matrix boots, {len(historical)} historical evidence files unchanged, 14 reference programs")


if __name__ == "__main__":
    main()
