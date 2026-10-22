"""Build and verify handler-metadata preparation on native Windows x64.

Uses a fresh build directory so historical phase-8 logs and disks survive.
This validates a preparatory substep, not exception dispatch or phase completion.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import uuid
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    extensions = {".cpp", ".c", ".h", ".hpp", ".asm", ".def", ".py", ".cmake", ".json", ".ps1", ".bat", ".rc", ".in"}
    paths = [ROOT / name for name in ("CMakeLists.txt", "Build-Kernel.ps1", "README.md")]
    for directory in ("guest", "shared", "src", "test", "tools", "cmake", "config", "docs"):
        paths.extend(p for p in (ROOT / directory).rglob("*") if p.is_file() and
                     (p.suffix in extensions or p.suffix == ".md" or p.name == "CMakeLists.txt") and "__pycache__" not in p.parts)
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(set(paths))}


def parse_tests(path, policy):
    suite = ET.parse(path).getroot()
    cases = suite.findall(".//testcase")
    require(len(cases) == policy["required_ctest_count"], "Incomplete CTest inventory")
    require(all(not case.findall("failure") and not case.findall("error") and not case.findall("skipped")
                and case.get("status", "run") == "run" for case in cases), "Failed, skipped or unrun CTest")
    by_name = {case.get("name"): case for case in cases}
    require(len(by_name) == len(cases), "Duplicate test names")
    for name in ("guest_unwind_handlers", "guest_unwind_core", "guest_pe_lifetime"):
        require(name in by_name, "Missing regression: " + name)
    handler_log = by_name["guest_unwind_handlers"].findtext("system-out", "")
    counts = re.search(r"(\d+) handler metadata checks PASS; (\d+) valid scenarios; (\d+) injected read fault points PASS", handler_log)
    native = re.search(r"(\d+) Windows handler differential frames PASS", handler_log)
    require(counts and int(counts[1]) >= policy["minimum_handler_checks"] and
            int(counts[2]) >= policy["valid_handler_scenarios"] and
            int(counts[3]) >= policy["handler_read_fault_points"], "Missing handler/fault proofs")
    require(native and int(native[1]) == policy["windows_handler_differential_frames"] and
            "NOT RUN" not in handler_log, "Native Windows handler comparison did not execute")
    old_log = by_name["guest_unwind_core"].findtext("system-out", "")
    old = re.search(r"(\d+) freestanding virtual unwind checks PASS; (\d+) Windows differential frames PASS", old_log)
    faults = re.search(r"(\d+) differential virtual unwind read fault points checked", old_log)
    require(old and int(old[1]) >= 2166 and int(old[2]) == 59 and faults and int(faults[1]) >= 391,
            "Original Windows virtual-unwind regression incomplete")
    return {"tests": len(cases), "handler_checks": int(counts[1]), "handler_scenarios": int(counts[2]),
            "handler_read_fault_points": int(counts[3]), "windows_handler_frames": int(native[1]),
            "legacy_unwind_checks": int(old[1]), "windows_legacy_frames": int(old[2]), "skipped": 0}


def execute(command, log):
    print("Running: " + subprocess.list2cmdline([str(part) for part in command]), flush=True)
    # Redirect to disk to retain output even if the compiler or test is interrupted.
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(part) for part in command], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(encoding="utf-8", errors="replace")[-8000:], flush=True)
    require(result.returncode == 0, "Command failed; see " + str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, help="New, empty Windows build directory")
    args = parser.parse_args()
    if os.name != "nt" or platform.machine().upper() not in ("AMD64", "X86_64"):
        parser.error("Native Windows x64 is required. Portable checks are not Windows/WHP validation.")
    require(shutil.which("cmake") and shutil.which("ctest"), "CMake 4+ and CTest must be on PATH")
    policy = json.loads((ROOT / "config/phase8-handlers-acceptance.json").read_text(encoding="utf-8"))
    require(policy["status"] == "preparation" and policy["phase"] == 8 and
            policy["remaining_before_phase_completion"] and not policy["next_phase_started"], "Invalid phase status")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
    build = (args.build or ROOT / ("build-phase8-handlers-windows-" + stamp + "-" + uuid.uuid4().hex[:8])).resolve()
    require(not build.exists() or not any(build.iterdir()), "Refusing to overwrite an existing build: " + str(build))
    build.mkdir(parents=True, exist_ok=True)
    before = sources()
    result = {"phase": 8, "phase_status": "in_progress", "substep": "handler_metadata_only",
              "next_phase_started": False, "validation": "running", "platform": platform.platform(),
              "build": str(build), "sources": before, "configurations": []}
    try:
        execute(["cmake", "-S", ROOT, "-B", build, "-G", "Visual Studio 17 2022", "-A", "x64",
                 "-DBUILD_TESTING=ON", "-DGUARDIAN_BUILD_GUEST_CONTRACT=ON"], build / "phase8-handlers-configure.log")
        for config in ("Debug", "Release"):
            execute(["cmake", "--build", build, "--config", config, "--parallel", "2"],
                    build / ("phase8-handlers-build-" + config + ".log"))
            execute(["cmake", "--build", build, "--config", config, "--target", "guardian_guest", "--parallel", "2"],
                    build / ("phase8-handlers-audit-" + config + ".log"))
            junit = build / ("phase8-handlers-ctest-" + config + ".xml")
            execute(["ctest", "--test-dir", build, "-C", config, "-V", "--output-on-failure", "--output-junit", junit],
                    build / ("phase8-handlers-ctest-" + config + ".log"))
            row = parse_tests(junit, policy)
            row["configuration"] = config
            result["configurations"].append(row)
        require(before == sources(), "Sources changed during validation; results cannot describe the current source")
        images = list((build / "guest").rglob("*.gk"))
        require(len(images) == 24, "Missing Debug/Release guest images")
        result["images"] = {p.relative_to(build).as_posix(): digest(p) for p in sorted(images)}
        result["validation"] = "passed"
    except BaseException as error:
        result["validation"] = "failed"
        result["error"] = str(error)
        raise
    finally:
        result["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (build / "phase8-handlers-windows-validation.json").write_text(
            json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Handler-metadata preparation validated; phase 8 remains in progress. Evidence: " + str(build))


if __name__ == "__main__":
    main()
