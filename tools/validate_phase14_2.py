"""Accept the bounded 14.2 loader with stable sources/binaries and no skipped tests."""
from pathlib import Path
import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
import nt_compatibility as nt
from validate_phase14_1 import inputs

REQUIRED={'external_pe_volume','external_pe_native','external_pe_integration',
          'nt_compatibility_core','nt_compatibility_inventory','nt_compatibility_boot','nt_compatibility_cli',
          'pe_gui_native','pe_gui_inventory','pe_gui_boot','guest_pe_loader','guest_runtime_core',
          'guest_exception_dispatch','guest_unwind_core','guest_unwind_handlers'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--config',choices=('Release','Debug'),required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    before=inputs();paths=set()
    for directory in (args.build/args.config,args.build/'guest'/args.config,args.build/'guest/fixtures'/args.config,args.build/'guest/external'/args.config):
        paths.update(p for p in directory.rglob('*') if p.suffix.lower() in ('.exe','.dll','.gk'))
    binaries={str(p.relative_to(args.build)).replace('\\','/'):nt.sha(p.read_bytes()) for p in sorted(paths)}
    command=['ctest','--test-dir',str(args.build),'-C',args.config,'-R','^('+'|'.join(sorted(REQUIRED))+')$',
             '--output-on-failure','--output-junit',str(args.output.resolve()/'ctest.xml')]
    run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace')
    (args.output/'ctest.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    cases=ET.parse(args.output/'ctest.xml').getroot().findall('.//testcase')
    failures=sum(c.find('failure') is not None for c in cases);skipped=sum(c.find('skipped') is not None for c in cases)
    stable=before==inputs() and all(nt.sha((args.build/name).read_bytes())==h for name,h in binaries.items())
    accepted=run.returncode==failures==skipped==0 and {c.attrib['name'] for c in cases}==REQUIRED and stable
    proof={'schema':1,'phase':'14.2','status':'PASS' if accepted else 'FAIL','configuration':args.config,
           'tests':len(cases),'failures':failures,'skipped':skipped,'exitCode':run.returncode,'stableSourcesAndBinaries':stable,
           'scope':'bounded external PE32+ guest volume and owned x64 DLL runtime; native Windows service coverage remains pending',
           'windowsBinaryCompatibility':False,'phase14_3Started':False,'inputs':before,'binaries':binaries}
    (args.output/'validation.json').write_bytes(nt.canonical(proof))
    print(json.dumps({k:proof[k] for k in ('status','phase','configuration','tests','failures','skipped','stableSourcesAndBinaries')}))
    return 0 if accepted else 1

if __name__=='__main__':raise SystemExit(main())
