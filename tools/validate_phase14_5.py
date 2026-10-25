"""Accept the bounded original CRT matrix with pinned references and immutable evidence."""
from pathlib import Path
import argparse,json,subprocess
import xml.etree.ElementTree as ET
import crt_runtime
import api_set
import native_nt
import nt_compatibility as nt
from validate_phase14_1 import inputs
from validate_phase14_4 import REQUIRED as PREVIOUS
REQUIRED=PREVIOUS|{'crt_contract','crt_windows_reference','crt_runtime_tools','crt_runtime_integration'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True)
    p.add_argument('--reference',type=Path,required=True);p.add_argument('--original',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    _,native=native_nt.syscall_map(a.original,'win10',True);schema_raw=nt.regular(a.schema_reference,8<<20);schema_version,contracts=api_set.parse(api_set.section(schema_raw))
    reference={'sha256':nt.sha(schema_raw),'fileVersion':native_nt.file_version(nt.InventoryPe(a.schema_reference,schema_raw)),'version':schema_version,'contracts':len(contracts),'inputExecutedOnHost':False}
    tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(a.build),'-C',a.config,'--show-only=json-v1'],text=True))
    commands={t['name']:t.get('command',[]) for t in tests['tests']};nt.require(REQUIRED<=commands.keys() and all(commands[n] for n in REQUIRED),'Required tests not configured/built')
    command=commands['native_nt_original_boot'];nt.require(Path(command[command.index('--original')+1]).resolve()==a.original.resolve(),'CTest NTDLL reference mismatch')
    command=commands['win32_runtime_integration'];nt.require(Path(command[command.index('--reference')+1]).resolve()==a.schema_reference.resolve(),'CTest schema reference mismatch')
    command=commands['crt_runtime_integration'];nt.require(Path(command[command.index('--reference')+1]).resolve()==a.reference.resolve(),'CTest CRT reference mismatch')
    nt.require(Path(command[command.index('--schema-reference')+1]).resolve()==a.schema_reference.resolve(),'CRT schema reference mismatch')
    crt=crt_runtime.references(a.reference)
    before=inputs();paths=set()
    for directory in (a.build/a.config,*(a.build/'guest'/name/a.config for name in ('fixtures','external','native','win32','crt')),a.build/'guest'/a.config):
        paths.update(p for p in directory.rglob('*') if p.suffix.lower() in ('.exe','.dll','.gk'))
    binaries={p.relative_to(a.build).as_posix():nt.sha(p.read_bytes()) for p in sorted(paths)}
    run=subprocess.run(['ctest','--test-dir',str(a.build),'-C',a.config,'-j','1','-R','^('+'|'.join(sorted(REQUIRED))+')$','--output-on-failure','--output-junit',str(a.output.resolve()/'ctest.xml')],capture_output=True,text=True,encoding='utf-8',errors='replace')
    (a.output/'ctest.log').write_text(run.stdout+run.stderr,encoding='utf-8');cases=ET.parse(a.output/'ctest.xml').getroot().findall('.//testcase')
    failures=sum(c.find('failure') is not None or c.find('error') is not None for c in cases);skipped=sum(c.find('skipped') is not None for c in cases)
    stable=before==inputs() and all(nt.sha((a.build/n).read_bytes())==d for n,d in binaries.items()) and nt.sha(nt.regular(a.original,8<<20))==native['dllSha256'] and nt.sha(nt.regular(a.schema_reference,8<<20))==reference['sha256']
    stable=stable and crt==crt_runtime.references(a.reference)
    passed=run.returncode==failures==skipped==0 and len(cases)==len(REQUIRED) and {c.attrib['name'] for c in cases}==REQUIRED and stable
    proof={'schema':1,'phase':'14.5','configuration':a.config,'status':'PASS' if passed else 'FAIL','tests':len(cases),'testNames':sorted(c.attrib['name'] for c in cases),'failures':failures,'skipped':skipped,'exitCode':run.returncode,'stableSourcesAndBinaries':stable,
           'scope':'MSVC 19.44 /MD FH3/FH4 and selected original UCRT, VC14, MSVCP and MSVCRT guest corpus; bounded compatibility','nativeNtdllRegressionReference':native,'apiSetReference':reference,'windowsBinaryCompatibility':False,'phase14_6Started':False,'crtReferences':crt,'inputs':before,'binaries':binaries}
    (a.output/'validation.json').write_bytes(nt.canonical(proof));print(json.dumps({k:proof[k] for k in ('status','phase','configuration','tests','failures','skipped','stableSourcesAndBinaries')}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
