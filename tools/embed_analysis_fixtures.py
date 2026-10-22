"""Embed only the two owned behavioral analysis applications."""
from pathlib import Path
import sys
if len(sys.argv)!=4:
    raise ValueError('Expected header, environment application and document reader')
output, app, docs=map(Path,sys.argv[1:])
text='#pragma once\n#include "PeCompleteFixtures.h"\nnamespace guardian::pe::fixtures {\n'
for name,path in [('AnalysisApp',app),('AnalysisChild',docs)]:
    data=path.read_bytes()
    if not 64<=len(data)<=0x20000 or data[:2]!=b'MZ':raise ValueError('Invalid owned environment PE')
    text+='inline constexpr uint8_t '+name+'[]={\n'
    text+=''.join(','.join(map(str,data[i:i+32]))+',\n' for i in range(0,len(data),32))+'};\n'
text+='inline constexpr Source AnalysisSource={"guardian-analysis.exe",AnalysisApp,sizeof(AnalysisApp)};\n'
text+='inline constexpr Source AnalysisChildSource={"guardian-analysis-child.exe",AnalysisChild,sizeof(AnalysisChild)};\n}\n'
output.parent.mkdir(parents=True,exist_ok=True);output.write_text(text,encoding='utf-8')
