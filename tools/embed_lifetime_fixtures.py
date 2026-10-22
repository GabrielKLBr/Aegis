"""Embed owned lifecycle PE inputs without binary patching."""
import pathlib
import sys
out, app, child = map(pathlib.Path, sys.argv[1:])
parts = ['// Generated from owned PE files.\n#pragma once\n#include "PeRuntimeFixtures.h"\nnamespace guardian::pe::fixtures {\n']
for name,path in (("LifetimeApp",app),("LifetimeChild",child)):
    data=path.read_bytes()
    if not 64<=len(data)<=0x20000 or data[:2]!=b"MZ":raise SystemExit("Invalid lifetime fixture: "+str(path))
    parts.append(f"inline constexpr uint8_t {name}[]={{\n")
    parts.extend(",".join(str(v) for v in data[i:i+32])+",\n" for i in range(0,len(data),32));parts.append("};\n")
parts.append('inline constexpr Source LifetimeSources[]={{"guardian-lifetime-console.exe",LifetimeApp,sizeof(LifetimeApp)},{"guardian-runtime-api.dll",RuntimeDll,sizeof(RuntimeDll)},{"guardian-lifetime-child.exe",LifetimeChild,sizeof(LifetimeChild)}};\n}\n')
out.parent.mkdir(parents=True,exist_ok=True);text="".join(parts)
if not out.exists() or out.read_text(encoding="utf-8")!=text:out.write_text(text,encoding="utf-8")
