"""Embed the project's own conformance application; never import system DLLs."""
from pathlib import Path
import sys

def main():
    if len(sys.argv) != 3:
        raise ValueError('Expected output header and owned PE application')
    output, application = map(Path, sys.argv[1:])
    data = application.read_bytes()
    if not 64 <= len(data) <= 0x20000 or data[:2] != b'MZ':
        raise ValueError('Invalid owned PE conformance fixture')
    text = '#pragma once\n#include "PeCompleteFixtures.h"\nnamespace guardian::pe::fixtures {\n'
    text += 'inline constexpr uint8_t ConformanceApp[]={\n'
    text += ''.join(','.join(map(str,data[i:i+32]))+',\n' for i in range(0,len(data),32))
    text += '};\ninline constexpr Source ConformanceSource={"guardian-conformance.exe",ConformanceApp,sizeof(ConformanceApp)};\n}\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding='utf-8')

if __name__ == '__main__':
    main()
