"""Record bounded GUI compatibility tests; never declare the full GUI lot complete."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET

import inspect_gui_compatibility as gui

ROOT=Path(__file__).resolve().parents[1]


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def inputs():
    selected=[ROOT/'CMakeLists.txt']
    for directory in ('cmake','guest','shared','src','test','tools'):
        selected.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and p.suffix.lower() in
                        {'.h','.hpp','.cpp','.c','.asm','.inc','.py','.cmake','.txt','.json','.def'} and '__pycache__' not in p.parts)
    return {str(p.relative_to(ROOT)).replace('\\','/'):digest(p) for p in sorted(selected)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--config',choices=('Debug','Release'),required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--application',type=Path)
    parser.add_argument('--regex',default='^pe_gui_')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    before=inputs()
    binaries=[args.build/args.config/(name+'.exe') for name in ('pe_gui_native_oracle','pe_gui_boot_tests','guardian-gui-pe')]
    binaries += [args.build/'guest'/args.config/'guardian-kernel-pe-gui.gk',args.build/'guest/fixtures'/args.config/'user32.dll',args.build/'guest/fixtures'/args.config/'guardian-gui-application.exe']
    original_binaries={str(p.relative_to(args.build)).replace('\\','/'):digest(p) for p in binaries}
    command=['ctest','--test-dir',str(args.build),'-C',args.config,'-R',args.regex,'--output-on-failure','--output-junit',str(args.output.resolve()/'ctest.xml')]
    result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace')
    (args.output/'ctest.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    xml=ET.parse(args.output/'ctest.xml').getroot()
    cases=xml.findall('.//testcase')
    names={case.attrib['name'] for case in cases}
    skipped=sum(case.find('skipped') is not None for case in cases)
    failed=sum(case.find('failure') is not None for case in cases)
    after=inputs()
    if args.application:
        inventory=gui.inspect(args.application,binaries[-2])
        (args.output/'application-inventory.json').write_text(json.dumps(inventory,indent=2),encoding='utf-8')
    stable=before==after and all(digest(args.build/name)==sha for name,sha in original_binaries.items())
    status='PASS' if result.returncode==0 and failed==skipped==0 and {'pe_gui_native','pe_gui_inventory','pe_gui_boot'}<=names and stable else 'FAIL'
    proof={'schema':1,'status':status,'configuration':args.config,'phaseComplete':False,'phase12FStarted':False,
           'scope':'owned external PE and bounded USER32 subset; no Flappy/full Win32 acceptance',
           'ctestExitCode':result.returncode,'tests':len(cases),'failures':failed,'skipped':skipped,
           'sourcesUnchanged':before==after,'inputs':before,'binaries':original_binaries,'inputsAndBinariesStable':stable}
    (args.output/'validation.json').write_text(json.dumps(proof,indent=2),encoding='utf-8')
    print(json.dumps({k:proof[k] for k in ('status','configuration','phaseComplete','tests','failures','skipped','inputsAndBinariesStable')}))
    return 0 if status=='PASS' else 1


if __name__=='__main__':
    raise SystemExit(main())
