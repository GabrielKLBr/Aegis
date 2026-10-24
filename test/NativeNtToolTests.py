from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_nt as native
import nt_compatibility as nt
import pe_volume
from NtCompatibilityInventoryTests import fixture

class Tools(unittest.TestCase):
    def setUp(self):self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def test_wrapper_exports(self):
        native.wrappers(self.root/'wrapper.cpp',self.root/'exports.def')
        source=(self.root/'wrapper.cpp').read_text();exports=(self.root/'exports.def').read_text()
        self.assertEqual(source.count('extern "C" Status Nt'),48)
        for name in native.SERVICES:self.assertIn('Zw'+name[2:]+'='+name,exports)
    def test_owned_map_is_pinned_to_bytes_and_profile(self):
        dll=self.root/'owned.dll';fixture(dll,exports=[('Probe',None)])
        for profile in nt.PROFILES:
            table,metadata=native.syscall_map(dll,profile,False)
            magic,version,size,target,count,original,reserved,digest=struct.unpack_from('<Q6IQ',table)
            self.assertEqual((magic,version,size,target,count,original,reserved),(0x31504D53544E47,1,552,nt.PROFILES[profile][0],0,0,0))
            self.assertEqual(digest,pe_volume.fnv(dll.read_bytes()));self.assertEqual(len(table),552)
            self.assertEqual(metadata['dllSha256'],nt.sha(dll.read_bytes()));self.assertIsNone(metadata['fileVersion'])
    def test_original_requires_version_resource(self):
        dll=self.root/'owned.dll';fixture(dll,exports=[('NtAllocateVirtualMemory',None)])
        with self.assertRaises(ValueError):native.syscall_map(dll,'win10',True)
    def test_table_signature_count_is_consistent(self):
        self.assertEqual(len(native.NAMES),len(native.OPS));self.assertEqual(len(native.SERVICES),48)
        self.assertEqual({v[0] for v in native.SERVICES.values()},set(range(1,49)))
if __name__=='__main__':unittest.main()
