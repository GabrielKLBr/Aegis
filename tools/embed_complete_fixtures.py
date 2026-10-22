"""Embed only the project's compiler-produced phase 8 acceptance applications."""
import pathlib
import sys
if len(sys.argv)!=6:raise ValueError('Expected output and four owned input images')
out=pathlib.Path(sys.argv[1])
names=['CompleteApp','CompleteChild','DynamicDll','FailingDll']
parts=['#pragma once\n#include "PeRuntimeFixtures.h"\nnamespace guardian::pe::fixtures {\n']
for name,path in zip(names,map(pathlib.Path,sys.argv[2:])):
    data=path.read_bytes()
    if not 64<=len(data)<=0x20000 or data[:2]!=b'MZ':raise ValueError('Invalid owned PE fixture')
    parts.append('inline constexpr uint8_t '+name+'[]={\n')
    parts.extend(','.join(map(str,data[i:i+32]))+',\n' for i in range(0,len(data),32));parts.append('};\n')
parts.append('inline constexpr Source CompleteSources[]={{"guardian-complete-console.exe",CompleteApp,sizeof(CompleteApp)},{"guardian-runtime-api.dll",RuntimeDll,sizeof(RuntimeDll)},{"guardian-dynamic.dll",DynamicDll,sizeof(DynamicDll)},{"guardian-failing.dll",FailingDll,sizeof(FailingDll)},{"guardian-lifetime-child.exe",CompleteChild,sizeof(CompleteChild)}};\n}\n')
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(''.join(parts))
