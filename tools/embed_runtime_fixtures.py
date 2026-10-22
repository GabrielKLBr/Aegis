"""Embed compiler-produced, owned runtime PE inputs without patching them."""
import pathlib
import sys
out, app, dll = map(pathlib.Path, sys.argv[1:])
parts = ['// Generated from owned PE files.\n#pragma once\n#include "PeLoader.h"\nnamespace guardian::pe::fixtures {\n']
for name, path in (("RuntimeApp", app), ("RuntimeDll", dll)):
    data = path.read_bytes()
    if not 64 <= len(data) <= 0x20000 or data[:2] != b"MZ":
        raise SystemExit("Invalid runtime fixture: " + str(path))
    parts.append(f"inline constexpr uint8_t {name}[]={{\n")
    parts.extend(",".join(str(v) for v in data[i:i+32]) + ",\n" for i in range(0, len(data), 32))
    parts.append("};\n")
parts.append('inline constexpr Source RuntimeSources[]={{"guardian-runtime-console.exe",RuntimeApp,sizeof(RuntimeApp)},{"guardian-runtime-api.dll",RuntimeDll,sizeof(RuntimeDll)}};\n}\n')
out.parent.mkdir(parents=True, exist_ok=True)
text = "".join(parts)
if not out.exists() or out.read_text(encoding="utf-8") != text:
    out.write_text(text, encoding="utf-8")
