"""Embed only the two owned synthetic environment applications."""
from pathlib import Path
import sys
if len(sys.argv)!=4:
    raise ValueError('Expected header, environment application and document reader')
output, app, docs=map(Path,sys.argv[1:])
text='#pragma once\n#include "PeCompleteFixtures.h"\nnamespace guardian::pe::fixtures {\n'
for name,path in [('EnvironmentApp',app),('DocsApp',docs)]:
    data=path.read_bytes()
    if not 64<=len(data)<=0x20000 or data[:2]!=b'MZ':raise ValueError('Invalid owned environment PE')
    text+='inline constexpr uint8_t '+name+'[]={\n'
    text+=''.join(','.join(map(str,data[i:i+32]))+',\n' for i in range(0,len(data),32))+'};\n'
text+='inline constexpr Source EnvironmentSource={"guardian-environment.exe",EnvironmentApp,sizeof(EnvironmentApp)};\n'
text+='inline constexpr Source DocsSource={"guardian-docs.exe",DocsApp,sizeof(DocsApp)};\n}\n'
output.parent.mkdir(parents=True,exist_ok=True);output.write_text(text,encoding='utf-8')
