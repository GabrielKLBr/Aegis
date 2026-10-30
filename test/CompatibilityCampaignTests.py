"""Regression tests for claims, denominators and the frozen phase 14.14 protocol."""
from pathlib import Path
import copy,json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import compatibility_campaign as c

class CampaignTests(unittest.TestCase):
    def setUp(self):self.plan=json.loads((ROOT/'config/nt-compatibility/phase14.14-corpus.json').read_bytes())
    def rows(self):return [{'program':a['id'],'profile':p,'inputSha256':a['sha256'],'outcome':'not-evaluated','workflowVerified':False} for p in self.plan['policy']['profiles'] for a in self.plan['programs']]
    def report(self,**kw):return {'type':'sandbox','inputExecutedOnHost':False,'complete':False,'loaded':False,'ran':False,'error':'Unsupported input',**kw}
    def test_frozen_policy_thresholds(self):
        c.validate_plan(self.plan);self.assertEqual(self.plan['policy']['requiredOverallFunctionalPercentPerProfile'],75);self.assertEqual(self.plan['policy']['requiredFunctionalPercentPerCategory'],50)
    def test_exact_program_and_category_counts(self):
        self.assertEqual(len(self.plan['programs']),32);self.assertEqual(set(c.Counter(a['category'] for a in self.plan['programs']).values()),{4})
    def test_duplicate_version_does_not_count(self):
        self.plan['programs'][1]['independenceKey']=self.plan['programs'][0]['independenceKey']
        with self.assertRaises(ValueError):c.validate_plan(self.plan)
    def test_same_bytes_cannot_be_two_programs(self):
        self.plan['programs'][1]['sha256']=self.plan['programs'][0]['sha256']
        with self.assertRaises(ValueError):c.validate_plan(self.plan)
    def test_owned_fixture_excluded(self):
        self.plan['programs'][0]['ownedFixture']=True
        with self.assertRaises(ValueError):c.validate_plan(self.plan)
    def test_anycpu_not_counted_as_native_x86(self):
        for a in self.plan['programs']:
            if a['architecture']=='x86':a['architecture']='anycpu'
        with self.assertRaises(ValueError):c.validate_plan(self.plan)
    def test_changed_policy_rejected(self):
        self.plan['policy']['requiredOverallFunctionalPercentPerProfile']=0
        with self.assertRaises(ValueError):c.validate_plan(self.plan)
    def test_missing_scenario_rejected(self):
        self.plan['programs'][0]['scenario']['successCriteria']=[]
        with self.assertRaises(ValueError):c.validate_plan(self.plan)
    def test_missing_cell_rejected(self):
        with self.assertRaises(ValueError):c.summarize(self.plan,self.rows()[:-1])
    def test_duplicate_cell_rejected(self):
        rows=self.rows();rows[-1]=rows[0]
        with self.assertRaises(ValueError):c.summarize(self.plan,rows)
    def test_changed_input_row_rejected(self):
        rows=self.rows();rows[0]['inputSha256']='0'*64
        with self.assertRaises(ValueError):c.summarize(self.plan,rows)
    def test_unsupported_profile_has_no_observed_rate(self):
        summary=c.summarize(self.plan,self.rows());self.assertIsNone(summary['profiles']['win7']['observedFunctionalPercent']);self.assertFalse(summary['broadCompatibilityAccepted'])
    def test_unverified_workflow_cannot_be_functional(self):
        rows=self.rows();rows[0]['outcome']='functional'
        with self.assertRaises(ValueError):c.summarize(self.plan,rows)
    def test_partial_does_not_inflate_functional_rate(self):
        rows=self.rows()
        for row in rows:row.update(outcome='partial')
        summary=c.summarize(self.plan,rows);self.assertEqual(summary['profiles']['win10']['observedFunctionalPercent'],0);self.assertFalse(summary['broadCompatibilityAccepted'])
    def test_selected_denominator_kept(self):
        rows=self.rows();row=next(r for r in rows if r['profile']=='win10');row.update(outcome='functional',workflowVerified=True)
        summary=c.summarize(self.plan,rows);self.assertEqual(summary['profiles']['win10']['functionalPercentOfSelected'],3.125);self.assertFalse(summary['profiles']['win10']['acceptanceMet'])
    def test_category_gate_survives_high_overall_rate(self):
        rows=self.rows();excluded={a['id'] for a in self.plan['programs'] if a['category']=='multimedia'}
        for row in rows:row.update(outcome='incompatible' if row['program'] in excluded else 'functional',workflowVerified=row['program'] not in excluded)
        summary=c.summarize(self.plan,rows);self.assertEqual(summary['profiles']['win10']['observedFunctionalPercent'],87.5);self.assertFalse(summary['broadCompatibilityAccepted'])
    def test_complete_without_workflow_is_partial(self):
        app=next(a for a in self.plan['programs'] if a['id']=='notepad');self.assertEqual(c.classify(app,self.report(complete=True,loaded=True,ran=True,exitCode=0))[0],'partial')
    def test_hello_oracle_requires_exact_line_and_exit(self):
        app=self.plan['programs'][0]
        self.assertEqual(c.classify(app,self.report(complete=True,loaded=True,ran=True,exitCode=0,console='Hello World\n'))[0],'functional')
        self.assertEqual(c.classify(app,self.report(complete=True,loaded=True,ran=True,exitCode=0,console='Not Hello World output'))[0],'partial')
    def test_declared_host_execution_rejected(self):
        with self.assertRaises(ValueError):c.classify(self.plan['programs'][0],self.report(inputExecutedOnHost=True))
    def test_missing_imports_are_a_compatibility_failure(self):
        self.assertEqual(c.classify(self.plan['programs'][0],self.report(unresolved=['missing.dll!Function']))[0],'incompatible')
    def test_infrastructure_failure_cannot_lower_compatibility(self):
        with self.assertRaises(ValueError):c.classify(self.plan['programs'][0],self.report(error='bad allocation'))
    def test_infrastructure_failure_after_load_still_invalidates_campaign(self):
        with self.assertRaises(ValueError):c.classify(self.plan['programs'][0],self.report(loaded=True,ran=True,error='bad allocation'))
    def test_size_limit_recorded_separately(self):
        app=copy.deepcopy(self.plan['programs'][0]);app['bytes']=9<<20;self.assertEqual(c.classify(app,self.report())[1],'input-size-limit')
    def test_frozen_corpus_tamper_stops_before_dispatch(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);cache=root/'cache';cache.mkdir();c.write(cache/'freeze.json',{'planSha256':c.identity(self.plan)})
            first=cache/self.plan['programs'][0]['id'];first.mkdir();(first/'input.exe').write_bytes(b'corrupt')
            plan=root/'plan.json';c.write(plan,self.plan)
            with self.assertRaisesRegex(ValueError,'Frozen input hash mismatch'):c.run(plan,cache,root/'no-build','Release',root/'output',root,root)

if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(CampaignTests);result=unittest.TextTestRunner(verbosity=2).run(suite)
    receipt={'phase':'14.14','tests':result.testsRun,'passed':result.testsRun-len(result.failures)-len(result.errors),'status':'PASS' if result.wasSuccessful() else 'FAIL'}
    if len(sys.argv)==2:c.write(Path(sys.argv[1]),receipt)
    print(json.dumps(receipt));raise SystemExit(0 if result.wasSuccessful() else 1)
