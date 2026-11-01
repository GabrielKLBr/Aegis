"""Package the bounded 14.11 runtime and its accepted, frozen evidence."""
from pathlib import Path
import argparse
import json
import shutil
import zipfile
import validate_frameworks as validation

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','release','debug','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--forms',action='store_true',help='Package the targeted managed-forms continuation')
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();archive=out.with_name(out.name+'.zip')
    if out.exists() or archive.exists():raise ValueError('Use a fresh package output')
    accepted={}
    if a.forms:
        from validate_framework_forms import EXPECTED_TESTS
    else:EXPECTED_TESTS=56
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes())
        if not (proof['phase']=='14.11' and proof['status']=='PASS' and proof['configuration']==config
                and proof['tests']==EXPECTED_TESTS and proof['passed']==EXPECTED_TESTS and proof['stableSourcesAndBinaries']
                and proof['earlierPhasesRevalidated'] is False and proof['hostManagedExecution'] is False
                and proof['inputs']==validation.inputs() and proof['binaries']==validation.binaries(build,config)):
            raise ValueError('Stale, incomplete or mismatched phase evidence: '+config)
        if a.forms and proof.get('scopeKey')!='managed-forms':raise ValueError('Managed forms evidence required')
        accepted[config]=proof
    out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,digest in accepted[config]['binaries'].items():
            copy(build/name,name)
            if validation.sha(out/name)!=digest:raise ValueError('Binary changed during packaging: '+name)
        for name in (('results.json','validation.json','parser-forms.log') if a.forms else ('results.json','validation.json','parser.log','parser-managed.log')):copy(folder/name,'validation/'+config+'/'+name)
        for file in (folder/'inputs').glob('*.exe'):
            if not file.name.startswith('external-'):copy(file,'validation/'+config+'/inputs/'+file.name)
        if a.forms:
            for file in folder.glob('*/window.json'):copy(file,'validation/'+config+'/'+file.parent.name+'/window.json')
    for file in (validation.ROOT/'tools').glob('*.py'):copy(file,'tools/'+file.name)
    copy(validation.ROOT/'CMakeLists.txt','CMakeLists.txt')
    for file in (validation.ROOT/'guest/fixtures/frameworks').glob('*'):

        if file.suffix in ('.cs','.il'):copy(file,'guest/fixtures/frameworks/'+file.name)
    for file in (validation.ROOT/'config').rglob('*'):
        if file.is_file():copy(file,'config/'+file.relative_to(validation.ROOT/'config').as_posix())
    for file in (validation.ROOT/'test/frameworks').rglob('*'):
        if file.is_file():copy(file,'test/frameworks/'+file.relative_to(validation.ROOT/'test/frameworks').as_posix())
    for name in ('PHASE-14.11.md','PHASE-14.10G.md'):copy(validation.ROOT/'docs'/name,'docs/'+name)
    (out/'README.txt').write_text(
        'Aegis 14.11: Limited CIL, selected WinForms, WinRT scale and portable installation in MiniNT.\n'
        'Read docs/PHASE-14.11.md. Windows x64/WHP and necessarias.\n development tools'
        'Runtimes Microsoft/CRT, ASan and reference schemas not included: provide local references legais.\n'
        'Run instalacao/copia customers and EXEs managed ONLY in MiniNT.\n'
        'To reproduce: '+('tools/validate_framework_forms.py' if a.forms else 'tools/validate_frameworks.py')+' --build ESTA_PASTA --config Release or Debug, '
        'with --reference, --schema-reference, --asan-runtime and --output novos.\n'
        'For the graphic batch, supply --corpus PASTA_LOCAL; low external source fixed if ausente.\n'
        'The external example and its binary are not redistributed in this pacote.\n'
        'Does not implement complete CLR or WinForms, WPF/WinUI, MSI or drivers.\n installation',encoding='utf-8')
    manifest={'schema':1,'phase':'14.11','configurations':['Release','Debug'],
              'tests':{config:proof['tests'] for config,proof in accepted.items()},
              'earlierPhasesRevalidated':False,'microsoftInputsIncluded':False,'externalFormsSourceIncluded':False,'scopeKey':'managed-forms' if a.forms else 'frameworks-foundation',
              'files':{f.relative_to(out).as_posix():validation.sha(f) for f in sorted(out.rglob('*')) if f.is_file()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(out).as_posix())
    print(json.dumps({'phase':'14.11','package':str(out),'zip':str(archive),'sha256':validation.sha(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
