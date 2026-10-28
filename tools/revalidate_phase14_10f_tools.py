"""Reuse unchanged matrix results only for this bounded, test-entrypoint-only correction.

No product source, binary, reference or other test input may change. The old
matrix and the fresh affected-test execution remain separate immutable evidence.
The merged JUnit file describes unique validated tests, not one new matrix run.
"""
from pathlib import Path
import argparse,copy,json,subprocess,sys
import xml.etree.ElementTree as ET
import nt_compatibility as nt
from validate_phase14_1 import inputs
from validate_phase14_10f import REQUIRED,TOTAL_TESTS

ALLOWED={'test/DesktopFlappyTests.py','tools/revalidate_phase14_10f_tools.py'}
def main():
    p=argparse.ArgumentParser()
    for key in ('baseline','build','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();base=json.loads((a.baseline/'validation.json').read_bytes());before=inputs()
    nt.require(base['phase']=='14.10F' and base['status']=='PASS' and base['stableSourcesAndBinaries'] and not base['failures'] and not base['skipped'],'Passing unchanged baseline required')
    expected=TOTAL_TESTS if base['suite']=='regression' else len(REQUIRED)
    nt.require(base['tests']==expected and len(set(base['testNames']))==expected,'Baseline matrix identities')
    changed={name for name in before.keys()|base['inputs'].keys() if before.get(name)!=base['inputs'].get(name)}
    nt.require(changed==ALLOWED,'Only the reviewed Flappy test correction and this verifier may change: '+','.join(sorted(changed)))
    for name,digest in base['binaries'].items():nt.require(nt.sha(nt.regular(a.build/name,32<<20))==digest,'Product binary changed: '+name)
    for key in ('flappyInputs','independentBinariesAndReceipts','independentSources','legalInputFiles'):
        for name,digest in base[key].items():nt.require(nt.sha(nt.regular(Path(name),32<<20))==digest,'External input changed')
    listing=json.loads(subprocess.check_output(['ctest','--test-dir',str(a.build),'-C',base['configuration'],'--show-only=json-v1'],text=True))
    command=next(t['command'] for t in listing['tests'] if t['name']=='desktop_flappy')
    nt.require(Path(command[1]).resolve()==(nt.ROOT/'test/DesktopFlappyTests.py').resolve(),'Affected entrypoint mismatch')
    nt.require({t['name'] for t in listing['tests']}>=set(base['testNames']),'CTest identities changed')
    a.output.mkdir(parents=True,exist_ok=False);fresh=a.output/'targeted.xml'
    run=subprocess.run(['ctest','--test-dir',str(a.build),'-C',base['configuration'],'-j','1','-R','^desktop_flappy$','--output-on-failure','--output-junit',str(fresh.resolve())],capture_output=True,text=True,encoding='utf-8',errors='replace')
    (a.output/'targeted.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    current=ET.parse(fresh).getroot().findall('.//testcase')
    passed=run.returncode==0 and len(current)==1 and current[0].attrib['name']=='desktop_flappy' and not any(current[0].find(k) is not None for k in ('failure','error','skipped'))
    stable=before==inputs()
    for name,digest in base['binaries'].items():stable=stable and nt.sha((a.build/name).read_bytes())==digest
    previous=ET.parse(a.baseline/'ctest.xml').getroot();merged=copy.deepcopy(previous)
    for case in list(merged):
        if case.tag=='testcase' and case.attrib['name']=='desktop_flappy':merged.remove(case)
    merged.append(copy.deepcopy(current[0]));merged.set('name','Aegis 14.10F: unchanged baseline plus affected test rerun')
    merged.set('time',str(sum(float(c.attrib.get('time','0')) for c in merged.findall('testcase'))))
    ET.ElementTree(merged).write(a.output/'ctest.xml',encoding='utf-8',xml_declaration=True)
    (a.output/'ctest.log').write_text('REVALIDATION: 1 current affected test; other unchanged results reused from '+str(a.baseline.resolve())+'\n\nBASELINE:\n'+(a.baseline/'ctest.log').read_text(encoding='utf-8')+'\n\nAFFECTED TEST RERUN:\n'+run.stdout+run.stderr,encoding='utf-8')
    proof=copy.deepcopy(base)
    proof.update(status='PASS' if passed and stable else 'FAIL',failures=0 if passed else 1,skipped=0,exitCode=run.returncode,stableSourcesAndBinaries=stable,inputs=before,
        validationStrategy='unchanged-product-and-inputs; prior complete matrix plus current affected-entrypoint rerun',
        baselineInputs=base['inputs'],baselineValidationSha256=nt.sha((a.baseline/'validation.json').read_bytes()),
        baselineJUnitSha256=nt.sha((a.baseline/'ctest.xml').read_bytes()),retestedTests=['desktop_flappy'],
        testExecutions=base['tests']+1,changedSources=sorted(changed),originalFailurePreserved=True,
        testCountMeaning='unique validated identities; reused baseline tests are not claimed as freshly rerun',
        baselineProof=str(a.baseline.resolve()))
    (a.output/'validation.json').write_bytes(nt.canonical(proof))
    print(json.dumps({k:proof[k] for k in ('status','configuration','tests','retestedTests','stableSourcesAndBinaries','validationStrategy')}))
    return 0 if passed and stable else 1
if __name__=='__main__':raise SystemExit(main())
