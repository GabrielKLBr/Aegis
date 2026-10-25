"""Accept the bounded security/service corpus with immutable source, binary and input evidence."""
from pathlib import Path
import argparse,json,subprocess
import xml.etree.ElementTree as ET
import api_set,crt_runtime,native_nt,nt_compatibility as nt
from validate_phase14_1 import inputs
from validate_phase14_5 import REQUIRED as PREVIOUS
REQUIRED=PREVIOUS|{'security_contract','security_core','security_windows_reference','security_runtime_tools','security_runtime_integration'}
TOTAL_TESTS=109

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--suite',choices=('acceptance','regression'),default='acceptance')
    p.add_argument('--reference',type=Path,required=True);p.add_argument('--original',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    _,native=native_nt.syscall_map(a.original,'win10',True);schema_raw=nt.regular(a.schema_reference,8<<20);version,contracts=api_set.parse(api_set.section(schema_raw));schema={'sha256':nt.sha(schema_raw),'fileVersion':native_nt.file_version(nt.InventoryPe(a.schema_reference,schema_raw)),'version':version,'contracts':len(contracts),'inputExecutedOnHost':False};crt=crt_runtime.references(a.reference)
    tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(a.build),'-C',a.config,'--show-only=json-v1'],text=True));commands={t['name']:t.get('command',[]) for t in tests['tests']};nt.require(len(commands)==TOTAL_TESTS and REQUIRED<=commands.keys() and all(commands.values()),'Required test matrix not configured/built')
    for name,arg,expected in [('native_nt_original_boot','--original',a.original),('win32_runtime_integration','--reference',a.schema_reference),('crt_runtime_integration','--reference',a.reference),('security_runtime_integration','--reference',a.reference),('security_runtime_integration','--schema-reference',a.schema_reference)]:
        command=commands[name];nt.require(Path(command[command.index(arg)+1]).resolve()==expected.resolve(),'CTest reference mismatch: '+name+' '+arg)
    required=REQUIRED if a.suite=='acceptance' else set(commands);before=inputs();paths=set()
    for directory in (a.build/a.config,*(a.build/'guest'/name/a.config for name in ('fixtures','external','native','win32','crt','security')),a.build/'guest'/a.config):paths.update(p for p in directory.rglob('*') if p.suffix.lower() in ('.exe','.dll','.gk'))
    binaries={p.relative_to(a.build).as_posix():nt.sha(p.read_bytes()) for p in sorted(paths)}
    run=subprocess.run(['ctest','--test-dir',str(a.build),'-C',a.config,'-j','1','-R','^('+'|'.join(sorted(required))+')$','--output-on-failure','--output-junit',str(a.output.resolve()/'ctest.xml')],capture_output=True,text=True,encoding='utf-8',errors='replace')
    (a.output/'ctest.log').write_text(run.stdout+run.stderr,encoding='utf-8');cases=ET.parse(a.output/'ctest.xml').getroot().findall('.//testcase');failures=sum(c.find('failure') is not None or c.find('error') is not None for c in cases);skipped=sum(c.find('skipped') is not None for c in cases)
    stable=before==inputs() and all(nt.sha((a.build/n).read_bytes())==d for n,d in binaries.items()) and nt.sha(nt.regular(a.original,8<<20))==native['dllSha256'] and nt.sha(nt.regular(a.schema_reference,8<<20))==schema['sha256'] and crt==crt_runtime.references(a.reference)
    passed=run.returncode==failures==skipped==0 and len(cases)==len(required) and {c.attrib['name'] for c in cases}==required and stable
    proof={'schema':1,'phase':'14.6','configuration':a.config,'suite':a.suite,'status':'PASS' if passed else 'FAIL','tests':len(cases),'testNames':sorted(c.attrib['name'] for c in cases),'failures':failures,'skipped':skipped,'exitCode':run.returncode,'stableSourcesAndBinaries':stable,'scope':'owned ADVAPI32/SECHOST/USERENV, isolated LocalService SCM and persistent bounded guest registry','nativeNtdllRegressionReference':native,'apiSetReference':schema,'crtReferences':crt,'windowsBinaryCompatibility':False,'phase14_7Started':False,'inputs':before,'binaries':binaries}
    (a.output/'validation.json').write_bytes(nt.canonical(proof));print(json.dumps({k:proof[k] for k in ('status','phase','configuration','suite','tests','failures','skipped','stableSourcesAndBinaries')}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
