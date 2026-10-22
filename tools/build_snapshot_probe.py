"""Rebuild the owned x64 snapshot probe fixture with native LLVM (no execution)."""
import argparse,hashlib,json,pathlib,re,struct,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--llvm',type=pathlib.Path,required=True);args=p.parse_args()
source=ROOT/'test/fixtures/phase9/SnapshotProbe.asm';target=source.with_suffix('.h')
with tempfile.TemporaryDirectory(prefix='guardian-snapshot-probe-') as temp:
 temp=pathlib.Path(temp);obj=temp/'probe.obj';pe=temp/'probe.exe';mapping=temp/'probe.map';raw=temp/'probe.bin'
 for command in ([args.llvm/'bin/llvm-ml','--m64','/c','/Fo'+str(obj),source],
                 [args.llvm/'bin/lld-link','/nodefaultlib','/entry:GuardianSnapshotProbe','/subsystem:native','/base:0x200000','/fixed','/out:'+str(pe),'/map:'+str(mapping),obj]):
  subprocess.run(list(map(str,command)),check=True,capture_output=True)
 pe_bytes=pe.read_bytes();header=struct.unpack_from('<I',pe_bytes,0x3C)[0];count=struct.unpack_from('<H',pe_bytes,header+6)[0];sections=header+24+struct.unpack_from('<H',pe_bytes,header+20)[0]
 data=None
 for i in range(count):
  at=sections+i*40
  if pe_bytes[at:at+8].rstrip(b'\0')==b'.text':
   size,offset=struct.unpack_from('<II',pe_bytes,at+16);data=pe_bytes[offset:offset+size]
 assert data is not None
 match=re.search(r'0001:([0-9a-fA-F]+)\s+GuardianSnapshotIrq',mapping.read_text());assert match and len(data)<=4096
 offset=int(match[1],16)
 target.write_text('#pragma once\n#include <cstdint>\nnamespace guardian::test {\ninline constexpr uint32_t SnapshotIrqOffset='+str(offset)+';\ninline constexpr uint8_t SnapshotProbe[]={\n'+''.join('    '+','.join('0x%02X'%b for b in data[at:at+16])+',\n' for at in range(0,len(data),16))+'};\n}\n')
 source.with_suffix('.json').write_text(json.dumps({'assembly_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'header_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'code_sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'handler_offset':offset,'origin':'Owned test assembly; code executed only in KVM/WHP guest'},indent=2)+'\n')
 print('Owned snapshot probe rebuilt:',len(data),'bytes')
