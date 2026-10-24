"""Malformed guest-volume inputs must fail before execution or output creation."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import pe_volume as pv
from NtCompatibilityInventoryTests import fixture

class VolumeTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.pe=self.root/'owned.exe';fixture(self.pe)
        self.kernel=self.root/'owned.gk';self.kernel.write_bytes(b'GuardianExternalPe/1/volume')
        self.spec={'profile':'win10','root':'c:\\app\\owned.exe','files':[{'path':'c:\\app\\owned.exe','source':str(self.pe),'kind':'pe'}]}
        self.bundle=self.root/'bundle';pv.build(self.spec,self.kernel,self.bundle)
        self.data=bytearray((self.bundle/'runtime.gpv').read_bytes())
    def tearDown(self):self.temp.cleanup()
    def reject(self,data):
        struct.pack_into('<Q',data,40,pv.fnv(data,True))
        with self.assertRaises(ValueError):pv.validate_volume(data)
    def test_roundtrip(self):
        self.assertEqual(pv.verify(self.bundle)['profile'],'win10')
        self.assertEqual(pv.validate_volume(self.data)[0],1)
    def test_table_counts_and_ranges(self):
        for at,value in ((24,17),(28,1),(32,17),(36,9),(176,4096),(180,0),(184,9),(188,1)):
            with self.subTest(at=at):
                data=self.data.copy();struct.pack_into('<I',data,at,value);self.reject(data)
    def test_hash_and_profile(self):
        data=self.data.copy();data[-1]^=1
        with self.assertRaises(ValueError):pv.validate_volume(data)
        data=self.data.copy();struct.pack_into('<I',data,20,99);self.reject(data)
    def test_namespace(self):
        for name in ('D:\\host.dll','c:\\app\\..\\host.dll','c:\\app\\bad.\\x.dll','c:\\app\\x.dll:stream','c:\\app\\x.dll\\'):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):pv.path(name)
    def test_rejects_before_output_creation(self):
        self.spec['files'][0]['path']='D:\\host.dll'
        with self.assertRaises(ValueError):pv.build(self.spec,self.kernel,self.root/'bad-output')
        self.assertFalse((self.root/'bad-output').exists())
    def test_manifest_binding(self):
        (self.bundle/'binding.gnt').write_bytes(bytes(176))
        with self.assertRaises(ValueError):pv.verify(self.bundle)

if __name__=='__main__':unittest.main()
