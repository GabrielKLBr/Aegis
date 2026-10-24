"""Static GUI prerequisite checks use real compiler outputs and bounded malformed PEs."""
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import inspect_gui_compatibility as gui

app, dll = map(Path, sys.argv[1:])
result = gui.inspect(app, dll)
assert result["candidateForGuestLoader"] and not result["executed"] and not result["functionalCompatibilityProven"]
assert result["importCount"] >= 10 and set(result["imports"]) == {"user32.dll"}
with tempfile.TemporaryDirectory() as temp:
    changed = Path(temp) / "missing.exe"
    data = app.read_bytes()
    assert b"GetMessageA\0" in data
    changed.write_bytes(data.replace(b"GetMessageA\0", b"GetMessageZ\0"))
    missing = gui.inspect(changed, dll)
    assert not missing["candidateForGuestLoader"] and "GetMessageZ" in missing["missingImports"]["user32.dll"]
    for data in (b"", b"MZ", b"MZ" + bytes(58) + b"\xff\xff\xff\x7f"):
        changed.write_bytes(data)
        try:
            gui.Pe(changed)
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed header accepted")
print("Static GUI inventory: actual exports, missing-symbol refusal, bounded headers; no execution claim PASS")
