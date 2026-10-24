"""Real CLI session with persisted hashes; changed inputs must fail before VM execution."""
from pathlib import Path
import json
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import nt_compatibility as nt

exe, kernel, app, dll, evidence = map(Path, sys.argv[1:])
evidence.mkdir(parents=True, exist_ok=True)
import tempfile
with tempfile.TemporaryDirectory(prefix="run-", dir=evidence) as folder:
    folder = Path(folder)
    bundle = folder / "bundle"
    manifest = nt.create("win10", kernel, app, dll, bundle)
    nt.verify(bundle)
    script = folder / "input.txt"
    script.write_text("200 key 71\n400 click 50 65 1\n700 close\n", encoding="ascii")
    args = [str(exe), str(kernel), str(app), str(folder / "session"), "--script", str(script),
            "--nt-binding", str(bundle / "binding.gnt"), "--nt-manifest", str(bundle / "manifest.json")]
    result = subprocess.run(args, capture_output=True, text=True, timeout=40)
    assert result.returncode == 0, result.stdout + result.stderr
    boot = json.loads((folder / "session/boot.json").read_bytes())["boots"][0]
    assert boot["success"] and boot["ntCompatibility"]["targetNt"] == "10.0.19045"
    assert not boot["ntCompatibility"]["windowsBinaryCompatibility"]
    assert (folder / "session/nt-manifest.json").read_bytes() == (bundle / "manifest.json").read_bytes()
    records = [{"mode": "valid", "returncode": result.returncode, "manifestSha256": nt.sha((bundle / "manifest.json").read_bytes())}]
    for mode in ("manifest", "application", "profile"):
        bad = args.copy()
        target = folder / mode
        bad[3] = str(target)
        if mode == "manifest":
            changed = folder / "changed.json"
            changed.write_bytes((bundle / "manifest.json").read_bytes() + b" ")
            bad[-1] = str(changed)
        elif mode == "application":
            changed = folder / "changed.exe"
            data = bytearray(app.read_bytes());data[64] ^= 1;changed.write_bytes(data)
            bad[2] = str(changed)
        else:
            changed = folder / "changed.gnt"
            data = bytearray((bundle / "binding.gnt").read_bytes());data[24] = 99;changed.write_bytes(data)
            bad[-3] = str(changed)
        refused = subprocess.run(bad, capture_output=True, text=True, timeout=15)
        assert refused.returncode != 0 and not target.exists(), refused.stdout + refused.stderr
        records.append({"mode": mode, "returncode": refused.returncode, "outputCreated": target.exists()})
    output = evidence / ("result-" + folder.name + ".json")
    output.write_bytes(nt.canonical({"schema": 1, "status": "PASS", "cases": records}))
print("NT CLI: real guest session, persisted manifest, manifest/app/profile tamper refused before output/VM PASS")
