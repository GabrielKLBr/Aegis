"""Embed only the owned USER32 implementation, never an application PE."""
from pathlib import Path
import sys
out, dll = map(Path, sys.argv[1:])
data = dll.read_bytes()
if not 64 <= len(data) <= 0x20000 or data[:2] != b"MZ":
    raise SystemExit("Owned USER32 image exceeds its contract")
text = '#pragma once\n#include "PeLoader.h"\nnamespace guardian::pe::fixtures {\ninline constexpr uint8_t GuiDll[]={\n'
text += ''.join(','.join(map(str,data[i:i+32]))+',\n' for i in range(0,len(data),32))
text += '};\ninline constexpr Source GuiSource{"user32.dll",GuiDll,sizeof(GuiDll)};\n}\n'
out.parent.mkdir(parents=True,exist_ok=True)
if not out.exists() or out.read_text(encoding='utf-8') != text:
    out.write_text(text,encoding='utf-8')
