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
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();archive=out.with_name(out.name+'.zip')
    if out.exists() or archive.exists():raise ValueError('Use a fresh package output')
    accepted={}
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes())
        if not (proof['phase']=='14.11' and proof['status']=='PASS' and proof['configuration']==config
                and proof['tests']==38 and proof['passed']==38 and proof['stableSourcesAndBinaries']
                and proof['earlierPhasesRevalidated'] is False and proof['hostManagedExecution'] is False
                and proof['inputs']==validation.inputs() and proof['binaries']==validation.binaries(build,config)):
            raise ValueError('Stale, incomplete or mismatched phase evidence: '+config)
        accepted[config]=proof
    out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,digest in accepted[config]['binaries'].items():
            copy(build/name,name)
            if validation.sha(out/name)!=digest:raise ValueError('Binary changed during packaging: '+name)
        for name in ('results.json','validation.json','parser.log'):copy(folder/name,'validation/'+config+'/'+name)
        for file in (folder/'inputs').glob('*.exe'):copy(file,'validation/'+config+'/inputs/'+file.name)
    for file in (validation.ROOT/'tools').glob('*.py'):copy(file,'tools/'+file.name)
    copy(validation.ROOT/'CMakeLists.txt','CMakeLists.txt')
    for file in (validation.ROOT/'guest/fixtures/frameworks').glob('*.cs'):
        copy(file,'guest/fixtures/frameworks/'+file.name)
    for file in (validation.ROOT/'config').rglob('*'):
        if file.is_file():copy(file,'config/'+file.relative_to(validation.ROOT/'config').as_posix())
    for file in (validation.ROOT/'test/frameworks').rglob('*'):
        if file.is_file():copy(file,'test/frameworks/'+file.relative_to(validation.ROOT/'test/frameworks').as_posix())
    for name in ('PHASE-14.11.md','PHASE-14.10G.md'):copy(validation.ROOT/'docs'/name,'docs/'+name)
    (out/'LEIA-ME.txt').write_text(
        'Aegis 14.11: CIL console limitado, contratos WinRT escalares e instalacao portatil no MiniNT.\n'
        'Leia docs/PHASE-14.11.md. Windows x64/WHP e ferramentas de desenvolvimento necessarias.\n'
        'Runtimes Microsoft/CRT, ASan e schemas de referencia nao incluidos: forneca referencias locais legais.\n'
        'Execute os clientes de instalacao/copia e os EXEs gerenciados SOMENTE no MiniNT.\n'
        'Para reproduzir: tools/validate_frameworks.py --build ESTA_PASTA --config Release ou Debug, '
        'com --reference, --schema-reference, --asan-runtime e --output novos.\n'
        'Nao implementa CLR completo, WinForms/WPF/WinUI, MSI ou instalacao de drivers.\n',encoding='utf-8')
    manifest={'schema':1,'phase':'14.11','configurations':['Release','Debug'],
              'tests':{config:proof['tests'] for config,proof in accepted.items()},
              'earlierPhasesRevalidated':False,'microsoftInputsIncluded':False,
              'files':{f.relative_to(out).as_posix():validation.sha(f) for f in sorted(out.rglob('*')) if f.is_file()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(out).as_posix())
    print(json.dumps({'phase':'14.11','package':str(out),'zip':str(archive),'sha256':validation.sha(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
