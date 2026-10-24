"""Accept the bounded native NT matrix with pinned inputs and no skipped tests."""
from pathlib import Path
import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
import native_nt
import nt_compatibility as nt
from validate_phase14_1 import inputs
from validate_phase14_2 import REQUIRED as PREVIOUS

REQUIRED=PREVIOUS|{'native_nt_tools','native_nt_windows_reference','native_nt_unwind',
                   'native_nt_core','native_nt_owned_boot','native_nt_original_boot'}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),required=True)
    p.add_argument('--original',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    _,reference=native_nt.syscall_map(a.original,'win10',True)
    inventory=json.loads(subprocess.check_output(['ctest','--test-dir',str(a.build),'-C',a.config,'--show-only=json-v1'],text=True))
    commands={test['name']:test.get('command',[]) for test in inventory['tests']}
    nt.require(REQUIRED<=commands.keys() and all(commands[name] for name in REQUIRED),'Required native/reference tests were not configured/built')
    original=commands['native_nt_original_boot'];index=original.index('--original')
    nt.require(Path(original[index+1]).resolve()==a.original.resolve(),'CTest original DLL differs from selected reference')
    before=inputs();paths=set()
    for directory in (a.build/a.config,a.build/'guest'/a.config,a.build/'guest/fixtures'/a.config,
                      a.build/'guest/external'/a.config,a.build/'guest/native'/a.config):
        paths.update(p for p in directory.rglob('*') if p.suffix.lower() in ('.exe','.dll','.gk'))
    binaries={p.relative_to(a.build).as_posix():nt.sha(p.read_bytes()) for p in sorted(paths)}
    command=['ctest','--test-dir',str(a.build),'-C',a.config,'-j','1','-R',
             '^('+'|'.join(sorted(REQUIRED))+')$','--output-on-failure','--output-junit',str(a.output.resolve()/'ctest.xml')]
    run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace')
    (a.output/'ctest.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    cases=ET.parse(a.output/'ctest.xml').getroot().findall('.//testcase')
    failures=sum(c.find('failure') is not None or c.find('error') is not None for c in cases)
    skipped=sum(c.find('skipped') is not None for c in cases)
    stable=before==inputs() and all(nt.sha((a.build/name).read_bytes())==h for name,h in binaries.items())
    stable=stable and nt.sha(nt.regular(a.original,8<<20))==reference['dllSha256']
    accepted=run.returncode==failures==skipped==0 and len(cases)==len(REQUIRED) and {c.attrib['name'] for c in cases}==REQUIRED and stable
    proof={'schema':1,'phase':'14.3','status':'PASS' if accepted else 'FAIL','configuration':a.config,
           'tests':len(cases),'failures':failures,'skipped':skipped,'exitCode':run.returncode,
           'testNames':sorted(c.attrib['name'] for c in cases),
           'stableSourcesAndBinaries':stable,'scope':'bounded native NT services; owned runtime on three profiles; selected original Win10 ntdll subset',
           'originalReference':reference,'windowsBinaryCompatibility':False,'phase14_4Started':False,
           'exactNativeReferences':{profile:'pending' for profile in nt.PROFILES},'inputs':before,'binaries':binaries}
    (a.output/'validation.json').write_bytes(nt.canonical(proof))
    print(json.dumps({k:proof[k] for k in ('status','phase','configuration','tests','failures','skipped','stableSourcesAndBinaries')}))
    return 0 if accepted else 1


if __name__=='__main__':raise SystemExit(main())
