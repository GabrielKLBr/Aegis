#!/usr/bin/env python3
"""Recreate parser fixtures from an owned guest build; never execute PE images."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys


def prepare(guest, output):
    names = ("guardian-pe-console.exe", "guardian-pe-runtime.dll", "guardian-kernel.gk")
    rows = []
    for name in names:
        source = guest / name
        data = source.read_bytes()
        if data[:2] != b"MZ":
            raise ValueError("Imagem de teste inválida: " + name)
        rows.append({"name": name, "source": "fresh-owned-guest-build/" + name,
                     "sha256": hashlib.sha256(data).hexdigest()})
    output.mkdir(parents=True, exist_ok=False)
    for row in rows:
        target = output / row["name"]
        shutil.copyfile(guest / row["name"], target)
        target.chmod(0o644)
    manifest = {"purpose": "Fresh owned parser inputs, not frozen MSVC reference outputs; never executed by parser tests.",
                "files": rows}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return {"status": "PASS", "directory": str(output.resolve()), "files": len(rows),
            "nativeWindowsReference": False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--from-guest", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    print(json.dumps(prepare(a.from_guest.resolve(), a.output.resolve()), indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as e:
        print("Erro:", e, file=sys.stderr)
        sys.exit(1)
