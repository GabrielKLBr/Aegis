from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import api_set as a
import win32_runtime as w

class Tools(unittest.TestCase):
    def test_profile_exports(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder);old=w.exports(p/'old.def',p/'old-k.def','win7');new=w.exports(p/'new.def',p/'new-k.def','win10')
            self.assertGreater(len(new),100);self.assertNotIn('GetSystemTimePreciseAsFileTime',old);self.assertIn('GetSystemTimePreciseAsFileTime',new)
            self.assertIn('VirtualAlloc=KbVirtualAlloc',(p/'new.def').read_text());self.assertIn('VirtualAlloc=kernelbase.VirtualAlloc',(p/'new-k.def').read_text())
    def test_schema_roundtrip(self):
        for version in (2,6):
            mapping={**a.OWNED,'api-ms-win-core-aegis-importer-l1-1-0':{'':'kernelbase.dll','plugin.dll':'kernel32.dll'}}
            encoded=a.encode(version,mapping);self.assertEqual(a.parse(encoded),(version,mapping));self.assertEqual(a.parse(a.section(a.pe(encoded))),(version,mapping))
    def test_schema_refusals(self):
        good=a.encode(6,a.OWNED)
        for offset in (4,12,16,20):
            bad=bytearray(good);struct.pack_into('<I',bad,offset,0xffffffff)
            with self.assertRaises(ValueError):a.parse(bytes(bad))
        with self.assertRaises(ValueError):a.encode(6,{'api-ms-win-core-escape-l1-1-0':{'':'../kernelbase.dll'}})

def main():
    core,reference=map(Path,sys.argv[1:]);suite=unittest.defaultTestLoader.loadTestsFromTestCase(Tools);result=unittest.TextTestRunner().run(suite)
    if not result.wasSuccessful():return 1
    with tempfile.TemporaryDirectory() as folder:
        p=Path(folder);paths=[]
        for version in (2,6):
            mapping={**a.OWNED,'api-ms-win-core-aegis-importer-l1-1-0':{'':'kernelbase.dll','plugin.dll':'kernel32.dll'}}
            path=p/('schema'+str(version)+'.dll');path.write_bytes(a.pe(a.encode(version,mapping)));paths.append(path)
        subprocess.run([str(core),*map(str,paths),str(reference)],check=True)
    version,records=a.parse(a.section(reference.read_bytes()));print(json.dumps({'schemaVersion':version,'nativeContracts':len(records),'status':'PASS'}));return 0
if __name__=='__main__':raise SystemExit(main())
