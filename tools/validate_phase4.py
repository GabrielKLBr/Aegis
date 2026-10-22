"""Check phase-4 build/test evidence; optionally run only trusted project programs."""
import argparse
import datetime
import pathlib
import re
import xml.etree.ElementTree as ET
from validate_phase3 import ROOT, NS, require, read, write, sha, execute, inspect_image, project_boundaries


def matrices(build):
    output = []
    for config in ("Debug", "Release"):
        log = (build / f"phase4-full-{config.lower()}-script.log").read_text(encoding="utf-8", errors="replace")
        require("100% tests passed, 0 tests failed out of 23" in log and not re.search(r"\*\*\*(?:Skipped|Not Run|Failed)", log), "CTest incomplete: " + config)
        for name, filename, negatives in (("threads", f"phase4-boots-{config}", 12),
                                           ("memory", f"phase4-regression-phase3-boots-{config}", 18),
                                           ("cpu", f"phase4-regression-phase2-boots-{config}", 16),
                                           ("boot", f"phase4-regression-phase1-boots-{config}", 22)):
            data = read(build / (filename + ".json")); rows = data["boots"]
            require(data["backend"] == "WHP" and data["configuration"] == config and len(rows) == 40, "Incomplete boot matrix")
            for ram in (16 << 20, 64 << 20):
                require(sum(r["ramBytes"] == ram for r in rows) == 20, "Missing RAM case")
            for r in rows:
                require(r["success"] and not r["timedOut"] and r["stopStatus"] == 0 and r["cpl"] == 0 and r["wallMs"] < 3000, "Boot failed")
                if name == "threads":
                    t = r["threads"]
                    require(r["threadReportValid"] and t["checks"] == 32767 and t["coreChecks"] >= 983 and t["completed"] == 8 and
                            t["faults"] == 1 and t["apcs"] == 1 and t["ticks"] >= 4 and t["idleTicks"] >= 1 and t["preemptions"] >= 4 and
                            t["counters"][0] > 0 and t["counters"][1] > 0 and t["counters"][2:] == [16, 16] and
                            t["freeBefore"] == t["freeAfter"] + 21 and r["cpu"]["timerTicks"] == t["ticks"] and
                            r["cpu"]["timerUserTicks"] == t["ticks"] and
                            r["cpu"]["userEntries"] == 2 and r["cpu"]["userFaults"] == 1 and not r["memoryFault"], "Thread acceptance incomplete")
                elif name == "memory":
                    require(r["memory"]["checks"] == 4095 and r["memory"]["cycles"] == 128, "Memory regression")
                elif name == "cpu":
                    require(r["cpu"]["checks"] == 2047 and r["cpu"]["userFaults"] == 6, "CPU regression")
            negative = read(build / (filename + ".negative.json"))["boots"]
            require(len(negative) == negatives and all(not r["success"] for r in negative), "Negative matrix incomplete")
            if name == "threads":
                require(negative[-1]["timedOut"] and all(not r["timedOut"] for r in negative[:-1]), "Watchdog/report failure mismatch")
            output.append({"configuration": config, "workload": name, "positive_boots": len(rows), "negative_scenarios": len(negative),
                           "min_boot_ms": min(r["wallMs"] for r in rows), "max_boot_ms": max(r["wallMs"] for r in rows)})
    return output


def boundaries(build):
    rows = project_boundaries(build)
    root = ET.parse(build / "guest" / "guardian_kernel_threads.vcxproj").getroot()
    for group in root.findall("m:ItemDefinitionGroup", NS):
        compiler = group.find("m:ClCompile", NS)
        if compiler is None:
            continue
        c = {p.tag.split("}")[-1]: (p.text or "").strip() for p in compiler}
        l = {p.tag.split("}")[-1]: (p.text or "").strip() for p in group.find("m:Link", NS)}
        require(c["LanguageStandard"] == "stdcpp20" and c["BasicRuntimeChecks"] == "Default" and not c["RuntimeLibrary"] and
                not c["ExceptionHandling"] and c["RuntimeTypeInfo"] == "false" and c["OmitDefaultLibName"] == "true" and
                "/kernel" in c["AdditionalOptions"] and l["IgnoreAllDefaultLibraries"] == "true" and not l["AdditionalDependencies"] and
                l["SubSystem"] == "Native", "Thread image freestanding boundary failed")
        rows.append({"target": "guardian_kernel_threads", "configuration": group.get("Condition"), "compiler": c, "linker": l})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    parser.add_argument("--build", default=ROOT / "build-codex", type=pathlib.Path)
    parser.add_argument("--run-programs", action="store_true")
    args = parser.parse_args(); build = args.build.resolve(); snapshot = args.snapshot.resolve()
    matrix = matrices(build)
    historical = read(snapshot / "historical-evidence.json")
    for item in historical:
        require(sha(pathlib.Path(item["Path"])) == item["Sha256"].lower(), "Historical evidence changed: " + item["Path"])
    projects = boundaries(build); write(build / "phase4-build-boundaries.json", projects)
    names = ("guardian-kernel", "guardian-kernel-cpu", "guardian-kernel-memory", "guardian-kernel-threads")
    images = [inspect_image(build / "guest" / c / (n + ".gk")) for c in ("Debug", "Release") for n in names]
    write(build / "phase4-image-audit.json", images)
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
            smoke.append(execute([tool, build / "guest" / config / "guardian-kernel-threads.gk", "--threads", "--ram-mib", "64", "--repeat", "2",
                                  "--report", build / f"phase4-cli-{config}.json"], config, "thread CLI boots"))
        write(build / "phase4-reference-programs.json", refs); write(build / "phase4-smoke.json", smoke)
    refs = read(build / "phase4-reference-programs.json"); smoke = read(build / "phase4-smoke.json")
    require(len(refs) == 14 and len(smoke) == 14 and all(r["exit_code"] == 0 for r in refs + smoke), "Reference/smoke validation missing")
    for config in ("Debug", "Release"):
        require(all(r["success"] for r in read(build / f"phase4-cli-{config}.json")["boots"]), "Thread CLI failed")
    clean = (build / "phase4-clean-debug-build.log").read_text(encoding="utf-8", errors="replace")
    require(clean.count("Kernel PE32+ x64 valid") == 4 and "error " not in clean.lower(), "Clean build audit incomplete")
    modified = ["guest/CpuKernel.cpp", "guest/CpuKernel.h", "guest/Cpu.asm", "guest/CMakeLists.txt", "cmake/GuardianPlatform.cmake",
                "src/microvm/boot/KernelBoot.h", "src/microvm/boot/KernelBoot.cpp", "src/microvm/boot/BootMain.cpp", "README.md", "docs/ROADMAP-COMPLETO.md"]
    created = ["shared/guardian/Threads.h", "guest/Scheduler.h", "guest/Scheduler.cpp", "guest/SchedulerChecks.cpp", "guest/ThreadKernel.h",
               "guest/ThreadKernel.cpp", "guest/ThreadUser.asm", "test/GuestSchedulerCoreTests.cpp", "test/GuestThreadBootTests.cpp",
               "config/phase4-acceptance.json", "docs/PHASE-4.md", "tools/validate_phase4.py"]
    evidence = [p for p in build.glob("phase4-*") if p.is_file() and p.name not in
                ("phase4-change-manifest.json", "phase4-verification.log", "phase4-final-verification.log")]
    manifest = {"phase": 4, "status": "complete", "next_phase_started": False,
                "verified_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "base_directory": str(ROOT), "backup_directory": str(snapshot),
                "modified": [{"path": p, "before_sha256": sha(snapshot / p), "after_sha256": sha(ROOT / p)} for p in modified],
                "created": [{"path": p, "sha256": sha(ROOT / p)} for p in created], "historical_evidence_files_unchanged": len(historical),
                "ctest_tests_per_configuration": 23, "skipped": 0, "matrix": matrix, "reference_programs": len(refs),
                "build_boundaries_checked": len(projects), "images": images,
                "evidence": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in sorted(evidence)],
                "windows_boot_or_security_settings_changed": False}
    write(build / "phase4-change-manifest.json", manifest)
    print(f"PASS: phase 4, 23 tests/configuration, 320 matrix boots, {len(historical)} historical evidence files unchanged, 14 reference programs")


if __name__ == "__main__":
    main()
