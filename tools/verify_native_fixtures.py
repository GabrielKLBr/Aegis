"""Verify frozen, owned PE parser inputs without loading or executing them."""
import hashlib
import json
import pathlib
import sys


def verify(folder):
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    expected = {"guardian-pe-console.exe", "guardian-pe-runtime.dll", "guardian-kernel.gk"}
    if {row["name"] for row in manifest["files"]} != expected or len(manifest["files"]) != len(expected):
        raise ValueError("Invalid native fixture inventory")
    for row in manifest["files"]:
        data = (folder / row["name"]).read_bytes()
        if data[:2] != b"MZ" or hashlib.sha256(data).hexdigest() != row["sha256"]:
            raise ValueError("Native fixture changed: " + row["name"])
    print("3 owned parser fixtures verified; PE code was not executed")


if __name__ == "__main__":
    verify(pathlib.Path(sys.argv[1]))
