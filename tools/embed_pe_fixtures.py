"""Embed only the two compiler-built, owned PE fixtures for guest loader checks."""
import pathlib
import sys

out, app, dll = map(pathlib.Path, sys.argv[1:])
parts = ['// Generated from owned compiler outputs; do not edit.\n#pragma once\n#include "PeLoader.h"\nnamespace guardian::pe::fixtures {\n']
for name, path in (("App", app), ("Dll", dll)):
    data = path.read_bytes()
    if not 64 <= len(data) <= 0x20000 or data[:2] != b"MZ":
        raise SystemExit("Invalid owned fixture: " + str(path))
    parts.append(f"inline constexpr uint8_t {name}[]={{\n")
    parts.extend(",".join(str(v) for v in data[i:i+32]) + ",\n" for i in range(0, len(data), 32))
    parts.append("};\n")
parts.append('inline constexpr Source Sources[]={{"guardian-pe-console.exe",App,sizeof(App)},{"guardian-pe-runtime.dll",Dll,sizeof(Dll)}};\n}\n')
out.parent.mkdir(parents=True, exist_ok=True)
text = "".join(parts)
if not out.exists() or out.read_text(encoding="utf-8") != text:
    out.write_text(text, encoding="utf-8")
