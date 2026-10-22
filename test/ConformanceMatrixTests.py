"""Parser/differential negatives use synthetic unit fixtures, never OS evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('conformance_matrix',ROOT/'tools/conformance_matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)

def fixture():
    rows=[dict(case,actual=case['expected']) for case in matrix.catalog()]
    runs=[]
    for profile,nt in matrix.TARGETS.items():
        for ram,restored in [(16<<20,False)]*2+[(64<<20,False)]*2+[(16<<20,True)]:
            runs.append({'profile':profile,'nt':nt,'ramBytes':ram,'restored':restored,'observations':copy.deepcopy(rows)})
    return {'schema':1,'status':'PASS','architecture':'x64','backend':'KVM','programHash':matrix.program_id(),
            'caseCount':len(rows),'completeRuns':15,'runs':runs}

class MatrixTests(unittest.TestCase):
    def test_missing_reference_stays_pending(self):
        proof=matrix.compare(fixture(),[])
        self.assertFalse(proof['referenceCompared']);self.assertFalse(proof['phaseComplete'])
        self.assertEqual(len(proof['missingNativeVersions']),3)

    def test_rejects_missing_duplicate_wrong_version_and_revision(self):
        for mutate in (
            lambda p:p['runs'].pop(),
            lambda p:p['runs'][0]['observations'].pop(),
            lambda p:p['runs'][0]['observations'][1].update(id=0),
            lambda p:p['runs'][0].update(nt='10.0.26100'),
            lambda p:p.update(programHash='0'*64),
            lambda p:p['runs'][0]['observations'][0].update(actual=True),
            lambda p:p['runs'][0].update(restored=True),
        ):
            proof=fixture();mutate(proof)
            with self.assertRaises(ValueError):matrix.compare(proof,[])

    def test_actual_differential_and_exact_native_version(self):
        guest=fixture();rows=copy.deepcopy(guest['runs'][0]['observations'])
        # Explicit synthetic unit fixture: no reference capture is invoked.
        reference={'schema':1,'kind':'native-reference','status':'PASS','native':True,'wine':False,
                   'host':'Windows-unit-fixture','programHash':matrix.program_id(),'executableSha256':'a'*64,
                   'metadata':{'schema':1,'pageBytes':4096,'nt':'6.1.7601','architecture':'x64','backend':'native-windows','programHash':matrix.program_id()},
                   'observations':rows}
        proof=matrix.compare(guest,[reference]);self.assertEqual(proof['comparedGuestRuns'],5)
        reference['observations'][0]['actual']=0
        proof=matrix.compare(guest,[reference]);self.assertEqual(proof['status'],'FAIL');self.assertEqual(len(proof['differences']),5)
        reference['metadata']['nt']='6.1.7600'
        self.assertFalse(matrix.compare(guest,[reference])['referenceCompared'])
        reference['wine']=True
        with self.assertRaises(ValueError):matrix.compare(guest,[reference])

    def test_linux_cannot_capture_windows_evidence(self):
        with mock.patch.object(matrix.sys,'platform','linux'):
            with self.assertRaisesRegex(ValueError,'native Windows'):
                matrix.capture_reference(Path('not-executed.exe'),Path('not-created'))

    def test_raw_reference_requires_completion_and_program(self):
        metadata={'schema':1,'backend':'native-windows','architecture':'x64','nt':'6.1.7601','pageBytes':4096,'programHash':matrix.program_id()}
        output='12C_META '+json.dumps(metadata)+'\n'
        output+=''.join(f'12C {case["id"]} {case["expected"]}\n' for case in matrix.catalog())
        self.assertEqual(len(matrix.parse_reference(output+'12C_DONE 0\n')[1]),len(matrix.catalog()))
        for damaged in (output,output+'12C_DONE 1\n',output+'12C 0 1\n12C_DONE 0\n',output.replace(matrix.program_id(),'0'*64)+'12C_DONE 0\n'):
            with self.assertRaises(ValueError):matrix.parse_reference(damaged)

if __name__=='__main__':unittest.main()
