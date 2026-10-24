"""Validate the 14.1 scope and preserve test results, source and binary SHA-256."""
from pathlib import Path
import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
import nt_compatibility as nt


def inputs():
    from aegis_git import source_files
    return {str(p.relative_to(nt.ROOT)).replace("\\", "/"): nt.sha(p.read_bytes()) for p in source_files(nt.ROOT)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--config", choices=("Release", "Debug"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--regex", default="^(nt_compatibility_.*|pe_gui_.*)$")
    parser.add_argument("--application", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    before = inputs()
    binary_paths = [args.build / args.config / (n + ".exe") for n in (
        "nt_compatibility_tests", "nt_compatibility_boot_tests", "guardian-gui-pe", "pe_gui_boot_tests", "pe_gui_native_oracle")]
    binary_paths += [args.build / "guest" / args.config / "guardian-kernel-pe-gui.gk",
                     args.build / "guest/fixtures" / args.config / "guardian-gui-application.exe",
                     args.build / "guest/fixtures" / args.config / "user32.dll"]
    pinned_paths = set(binary_paths)
    for directory in (args.build / args.config, args.build / 'guest' / args.config, args.build / 'guest/fixtures' / args.config):
        pinned_paths.update(p for p in directory.iterdir() if p.suffix.lower() in ('.exe', '.dll', '.gk'))
    binaries = {str(p.relative_to(args.build)).replace("\\", "/"): nt.sha(p.read_bytes()) for p in sorted(pinned_paths)}
    command = ["ctest", "--test-dir", str(args.build), "-C", args.config, "-R", args.regex,
               "--output-on-failure", "--output-junit", str(args.output.resolve() / "ctest.xml")]
    run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    (args.output / "ctest.log").write_text(run.stdout + run.stderr, encoding="utf-8")
    cases = ET.parse(args.output / "ctest.xml").getroot().findall(".//testcase")
    names = {case.attrib["name"] for case in cases}
    failures = sum(case.find("failure") is not None for case in cases)
    skipped = sum(case.find("skipped") is not None for case in cases)
    if args.application:
        result = nt.inventory([args.application, binary_paths[-1]])
        (args.output / "external-inventory.json").write_bytes(nt.canonical(result))
    after = inputs()
    stable = before == after and all(nt.sha((args.build / name).read_bytes()) == digest for name, digest in binaries.items())
    required = {"nt_compatibility_core", "nt_compatibility_inventory", "nt_compatibility_boot", "nt_compatibility_cli",
                "pe_gui_native", "pe_gui_inventory", "pe_gui_boot"}
    status = "PASS" if run.returncode == failures == skipped == 0 and required <= names and stable else "FAIL"
    proof = {"schema": 1, "phase": "14.1", "status": status, "configuration": args.config, "tests": len(cases),
             "failures": failures, "skipped": skipped, "exitCode": run.returncode, "stableSourcesAndBinaries": stable,
             "scope": "owned contract profiles/binding/snapshots and static PE inventory; no Windows native DLL ABI acceptance",
             "phase14_2Started": False, "windowsBinaryCompatibility": False,
             "exactNativeReferences": {p: "pending" for p in nt.PROFILES}, "inputs": before, "binaries": binaries}
    (args.output / "validation.json").write_bytes(nt.canonical(proof))
    print(json.dumps({k: proof[k] for k in ("status", "phase", "configuration", "tests", "failures", "skipped", "stableSourcesAndBinaries")}))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
