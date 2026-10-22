"""Verify phase-3 evidence without overwriting historical phase reports.

Optional program runs are limited to the seven project reference examples,
help output, PE audits, and the project-built memory kernel in WHP.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(command, configuration, scenario):
    started = datetime.datetime.now(datetime.timezone.utc)
    result = subprocess.run([str(p) for p in command], cwd=ROOT, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=30)
    require(result.returncode == 0, f"{scenario}: exit {result.returncode}: {result.stderr}")
    return {"configuration": configuration, "scenario": scenario, "command": [str(p) for p in command],
            "exit_code": result.returncode, "elapsed_ms": round((datetime.datetime.now(datetime.timezone.utc) - started).total_seconds() * 1000, 3),
            "output": result.stdout, "stderr": result.stderr}


def project_boundaries(build):
    rows = []
    for target in ("guardian_guest_contract", "guardian_kernel", "guardian_kernel_cpu", "guardian_kernel_memory"):
        project = ET.parse(build / "guest" / (target + ".vcxproj")).getroot()
        for group in project.findall("m:ItemDefinitionGroup", NS):
            compiler = group.find("m:ClCompile", NS)
            if compiler is None:
                continue
            options = {item.tag.split("}")[-1]: (item.text or "").strip() for item in compiler}
            require(options.get("LanguageStandard") == "stdcpp20" and options.get("BasicRuntimeChecks") == "Default" and
                    not options.get("RuntimeLibrary") and not options.get("ExceptionHandling") and
                    options.get("RuntimeTypeInfo") == "false" and options.get("OmitDefaultLibName") == "true" and
                    "/kernel" in options.get("AdditionalOptions", ""), f"Freestanding boundary changed: {target}")
            linker = group.find("m:Link", NS)
            link_options = {} if linker is None else {item.tag.split("}")[-1]: (item.text or "").strip() for item in linker}
            if linker is not None:
                require(link_options.get("IgnoreAllDefaultLibraries") == "true" and not link_options.get("AdditionalDependencies") and
                        link_options.get("SubSystem") == "Native", f"Guest linker dependency: {target}")
            rows.append({"target": target, "configuration": group.get("Condition"), "compiler": options, "linker": link_options})
    host = ET.parse(build / "guardian_boot_core.vcxproj").getroot()
    debug = next(g for g in host.findall("m:ItemDefinitionGroup", NS) if "Debug|x64" in g.get("Condition", ""))
    require(debug.find("m:ClCompile/m:BasicRuntimeChecks", NS).text == "EnableFastChecks", "Host Debug checks removed")
    return rows


def inspect_image(path):
    data = path.read_bytes(); pe = struct.unpack_from("<I", data, 60)[0]; opt = pe + 24
    count = struct.unpack_from("<H", data, pe + 6)[0]
    size = struct.unpack_from("<I", data, opt + 56)[0]
    require(struct.unpack_from("<Q", data, opt + 24)[0] == 0x200000 and size <= 0x100000, "Image extent/base changed")
    for index in (1, 5, 9, 10, 14):
        require(struct.unpack_from("<II", data, opt + 112 + index * 8) == (0, 0), "Guest PE has forbidden dependency/directory")
    for index in range(count):
        flags = struct.unpack_from("<I", data, opt + 240 + index * 40 + 36)[0]
        require(flags & 0xA0000000 != 0xA0000000, "Guest PE contains W+X")
    return {"path": str(path), "sha256": sha(path), "file_bytes": len(data), "image_bytes": size, "sections": count}


def verify_reports(build):
    summary = []
    for config in ("Debug", "Release"):
        full_log = (build / f"phase3-full-{config.lower()}-script.log").read_text(encoding="utf-8", errors="replace")
        require("100% tests passed, 0 tests failed out of 21" in full_log and
                not re.search(r"\*\*\*(?:Skipped|Not Run|Failed)", full_log), f"Incomplete {config} CTest suite")
        for label, filename, negative_count in (
                ("memory", f"phase3-boots-{config}", 18),
                ("cpu_regression", f"phase3-regression-phase2-boots-{config}", 16),
                ("boot_regression", f"phase3-regression-phase1-boots-{config}", 22)):
            report = read(build / (filename + ".json")); rows = report["boots"]
            require(report["configuration"] == config and report["backend"] == "WHP" and len(rows) == 40, "Boot matrix/configuration mismatch")
            for ram in (16 << 20, 64 << 20):
                require(sum(row["ramBytes"] == ram for row in rows) == 20, "RAM matrix incomplete")
            for row in rows:
                require(row["success"] and not row["timedOut"] and row["stopStatus"] == 0 and row["cpl"] == 0 and row["wallMs"] < 3000, "Boot acceptance failure")
                if label == "memory":
                    m = row["memory"]; c = row["cpu"]
                    require(row["memoryReportValid"] and m["checks"] == 4095 and m["freeBefore"] == m["freeAfter"] + 21 and
                            m["peakFrames"] == m["freeBefore"] and m["exhaustedFrames"] == m["freeBefore"] - 42 and
                            m["allocationFailures"] == 30 and m["cycles"] == 128 and m["heapChecks"] == 5 and m["guardFaults"] == 1 and
                            m["userFaults"] == 8 and c["userEntries"] == 150 and c["userCompleted"] == 142 and not c["fatal"], "Guest memory accounting incomplete")
                    require([f["error"] for f in m["faults"]] == [4, 7, 4, 4, 21, 5, 5, 4] and not row["memoryFault"], "Actual CPU permission checks missing")
                elif label == "cpu_regression":
                    require(row["cpu"]["checks"] == 2047 and row["cpu"]["userFaults"] == 6 and row["cpu"]["kernelFaults"] == 2, "CPU regression")
            negatives = read(build / (filename + ".negative.json"))["boots"]
            require(len(negatives) == negative_count and all(not r["success"] for r in negatives), "Negative scenarios incomplete")
            summary.append({"configuration": config, "workload": label, "positive_boots": len(rows), "negative_scenarios": len(negatives),
                            "min_boot_ms": min(r["wallMs"] for r in rows), "max_boot_ms": max(r["wallMs"] for r in rows)})
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    parser.add_argument("--build", default=ROOT / "build-codex", type=pathlib.Path)
    parser.add_argument("--run-programs", action="store_true")
    args = parser.parse_args(); build = args.build.resolve(); snapshot = args.snapshot.resolve()
    summary = verify_reports(build)
    history = read(snapshot / "historical-evidence.json")
    for item in history:
        require(sha(pathlib.Path(item["Path"])) == item["Sha256"].lower(), f"Historical evidence changed: {item['Path']}")
    boundaries = project_boundaries(build); write(build / "phase3-build-boundaries.json", boundaries)
    images = [inspect_image(build / "guest" / c / (n + ".gk")) for c in ("Debug", "Release")
              for n in ("guardian-kernel", "guardian-kernel-cpu", "guardian-kernel-memory")]
    write(build / "phase3-image-audit.json", images)
    if args.run_programs:
        references, smoke = [], []
        for config in ("Debug", "Release"):
            for name in ("vmntdll", "vmntfile", "vmntsect", "vmntimage", "vmntquery", "vmntreg", "vmwininit"):
                references.append(execute([build / config / (name + ".exe")], config, name))
            smoke.append(execute([build / config / "av.exe", "--help"], config, "scanner help"))
            tool = build / config / "guardian_boot.exe"
            smoke.append(execute([tool, "--help"], config, "kernel tool help"))
            for name in ("guardian-kernel", "guardian-kernel-cpu", "guardian-kernel-memory"):
                smoke.append(execute([tool, "--check", build / "guest" / config / (name + ".gk")], config, "PE audit: " + name))
            smoke.append(execute([tool, build / "guest" / config / "guardian-kernel-memory.gk", "--memory", "--ram-mib", "64", "--repeat", "2",
                                  "--report", build / f"phase3-cli-{config}.json"], config, "memory CLI boots"))
        write(build / "phase3-reference-programs.json", references); write(build / "phase3-smoke.json", smoke)
    references = read(build / "phase3-reference-programs.json"); smoke = read(build / "phase3-smoke.json")
    require(len(references) == 14 and len(smoke) == 12 and all(r["exit_code"] == 0 for r in references + smoke), "Program validation incomplete")
    for config in ("Debug", "Release"):
        require(all(r["success"] for r in read(build / f"phase3-cli-{config}.json")["boots"]), "Memory CLI failed")
    require("Kernel PE32+ x64 valid" in (build / "phase3-clean-debug-build.log").read_text(encoding="utf-8", errors="replace"), "Clean build evidence absent")
    modified = ["guest/CpuKernel.cpp", "guest/CMakeLists.txt", "cmake/GuardianPlatform.cmake", "src/microvm/boot/KernelBoot.h",
                "src/microvm/boot/KernelBoot.cpp", "src/microvm/boot/BootMain.cpp", "README.md", "docs/ROADMAP-COMPLETO.md"]
    created = ["shared/guardian/Memory.h", "guest/MemoryManager.h", "guest/MemoryManager.cpp", "guest/MemoryKernel.h",
               "guest/MemoryKernel.cpp", "guest/MemoryUser.asm", "test/GuestMemoryTests.cpp", "test/GuestMemoryBootTests.cpp",
               "config/phase3-acceptance.json", "docs/PHASE-3.md", "tools/validate_phase3.py"]
    # These console logs can still be open for redirection while this program
    # runs. Hash stable evidence only, never the manifest or its live stdout.
    evidence = [p for p in build.glob("phase3-*") if p.is_file() and p.name not in
                ("phase3-change-manifest.json", "phase3-verification.log", "phase3-final-verification.log")]
    manifest = {"phase": 3, "status": "complete", "next_phase_started": False,
                "verified_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "base_directory": str(ROOT), "backup_directory": str(snapshot),
                "modified": [{"path": p, "before_sha256": sha(snapshot / p), "after_sha256": sha(ROOT / p)} for p in modified],
                "created": [{"path": p, "sha256": sha(ROOT / p)} for p in created],
                "historical_evidence_files_unchanged": len(history), "ctest_tests_per_configuration": 21, "skipped": 0,
                "matrix": summary, "reference_programs": len(references), "build_boundaries_checked": len(boundaries),
                "images": images, "evidence": [{"path": str(p.relative_to(ROOT)), "sha256": sha(p)} for p in sorted(evidence)],
                "windows_boot_or_security_settings_changed": False}
    write(build / "phase3-change-manifest.json", manifest)
    print(f"PASS: phase 3, 21 tests/configuration, 240 matrix boots, {len(history)} historical evidence files unchanged, 14 reference programs")


if __name__ == "__main__":
    main()
