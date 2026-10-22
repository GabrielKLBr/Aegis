"""Build and audit the native Linux subset; never launch Wine or PE input files."""
import datetime
import hashlib
import json
import os
import pathlib
import platform
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    paths = [ROOT / name for name in ("CMakeLists.txt", "Build-Linux.sh", "signatures.txt")]
    extensions = {".inc", ".h", ".cpp", ".c", ".asm", ".def", ".py", ".cmake", ".json", ".gk", ".exe", ".dll"}
    for folder in ("guest", "shared", "src", "test", "tools", "cmake", "config"):
        paths.extend(p for p in (ROOT / folder).rglob("*") if p.is_file() and
                     (p.suffix in extensions or p.name == "CMakeLists.txt") and "__pycache__" not in p.parts)
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(set(paths))}


def elf(path):
    data = path.read_bytes()
    require(len(data) >= 64 and data[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", data, 18)[0] == 62,
            "Not a native x86-64 ELF executable: " + str(path))
    return {"path": path.relative_to(ROOT).as_posix(), "format": "ELF64 x86-64", "sha256": sha(path)}


def tests(path, count):
    suite = ET.parse(path).getroot()
    cases = suite.findall(".//testcase")
    require(len(cases) == count, "Incomplete native test inventory")
    require(all(not case.findall("failure") and not case.findall("error") and not case.findall("skipped")
                and case.get("status", "run") == "run" for case in cases), "Native test failed/skipped/not run")
    names = {case.get("name") for case in cases}
    require(len(names) == count and {"linux_scanner", "guest_pe_loader", "guest_unwind_handlers", "guest_process_core"} <= names,
            "Missing native behavior coverage")
    return [{"name": case.get("name"), "output": case.findtext("system-out", "")} for case in cases]


def main():
    require(platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64"), "Linux x86-64 is required")
    policy = json.loads((ROOT / "config/linux-native-acceptance.json").read_text())
    require(not policy["wine_allowed"] and policy["vm_execution"] and policy["phase_status"] == "in_progress", "Invalid scope")
    before = source_hashes()
    out = ROOT / "build-linux" / ("validation-" + datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S-%f"))
    out.mkdir(parents=True, exist_ok=False)
    report = {"validation": "running", "platform": platform.platform(), "scope": policy["scope"],
              "phase": 11, "phase_status": "in_progress", "next_phase_started": False,
              "wine_used": False, "windows_differential_tests": "not_run", "vm_execution": "not_exercised_by_this_core_validation",
              "kvm_device_exists": pathlib.Path("/dev/kvm").exists(), "sources": before, "configurations": []}
    try:
        for config in policy["configurations"]:
            print("Building/verifying native Linux " + config, flush=True)
            build = out / config
            env = dict(os.environ, GUARDIAN_LINUX_BUILD_DIR=str(build), GUARDIAN_TEST_KVM="OFF")
            with (out / (config + ".log")).open("w", encoding="utf-8") as log:
                result = subprocess.run(["bash", ROOT / "Build-Linux.sh", config], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
            require(result.returncode == 0, "Native build failed; see " + str(out / (config + ".log")))
            row = {"configuration": config, "tests": tests(build / "tests.xml", policy["required_ctest_count"]),
                   "skipped": 0, "artifacts": [elf(build / "guardianav"), elf(build / "guardian-kvm"), elf(build / "guardian-vm"), elf(build / "guardian-gui")]}
            for path in sorted(build.glob("*_tests")):
                row["artifacts"].append(elf(path))
            require(len(row["artifacts"]) == 24, "Missing native executables")
            caps = subprocess.run([build / "guardianav", "--capabilities"], capture_output=True, text=True, check=True)
            row["capabilities"] = json.loads(caps.stdout)
            require(row["capabilities"]["native"] and not row["capabilities"]["wine"] and
                    row["capabilities"]["vmBackend"] == "KVM", "Unexpected backend")
            dynamic = subprocess.run(["ldd", build / "guardianav"], capture_output=True, text=True, check=True)
            row["dynamic_dependencies"] = dynamic.stdout
            require("wine" not in dynamic.stdout.lower() and "not found" not in dynamic.stdout.lower(), "Invalid native dependencies")
            report["configurations"].append(row)
        require(before == source_hashes(), "Sources changed during native verification")
        report["validation"] = "passed"
    except BaseException as error:
        report["validation"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (out / "validation.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Native subset approved. Windows oracles and VM execution remain outside this validation. Evidence: " + str(out))


if __name__ == "__main__":
    main()
