"""Targeted catalogue and publication-artifact checks; no MiniNT regression suites."""
from pathlib import Path
import json,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import windows_api_progress as site
import progress

class CatalogueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.rows=site.inventory(site.ROOT);cls.entries={(r['dll'],r['name']):r for r in cls.rows}
    def test_unique(self):self.assertEqual(len(self.rows),len(self.entries))
    def test_core_exports(self):
        for key in [('ntdll.dll','NtReadFile'),('kernel32.dll','Sleep'),('advapi32.dll','RegOpenKeyExW')]:self.assertIn(key,self.entries)
    def test_declarations_are_not_verified(self):self.assertEqual(self.entries['kernel32.dll','Sleep']['status'],'declared')
    def test_explicit_contracts_win(self):
        for row in progress.inventory(site.ROOT):self.assertEqual(self.entries[row['dll'].lower(),row['name']]['status'],row['status'])
    def test_planning(self):self.assertEqual(self.entries['msi.dll','MsiInstallProductW']['status'],'pending')
    def test_no_private_probe(self):self.assertFalse(any(r['dll'].startswith('guardian') for r in self.rows))
    def test_deterministic_complete_site(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder);progress.generate(site.ROOT,output);site.generate(site.ROOT,output)
            files={p.name:p.read_bytes() for p in output.iterdir()}
            self.assertTrue({'index.html','progress.html','progress.json','windows-apis.html','windows-apis.json','.nojekyll'}<=files.keys())
            self.assertIn(b'href="progress.html"',files['index.html']);self.assertIn(b'href="windows-apis.html"',files['index.html'])
            self.assertEqual(len(json.loads(files['windows-apis.json'])['entries']),len(self.rows))
            progress.generate(site.ROOT,output);site.generate(site.ROOT,output)
            self.assertEqual(files,{p.name:p.read_bytes() for p in output.iterdir()})

if __name__=='__main__':unittest.main()
