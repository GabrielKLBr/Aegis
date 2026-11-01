"""Package 14.13A owned builds and targeted evidence, excluding local DLL catalogs."""
from pathlib import Path
import argparse,json,shutil,zipfile
import validate_runtime_integration as validation,validate_frameworks as frozen

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','release','debug','services','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();archive=out.with_name(out.name+'.zip')
    if out.exists() or archive.exists():raise ValueError('Fresh package output required')
    proofs={}
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes())
        if not(proof['phase']=='14.13A' and proof['status']=='PASS' and proof['configuration']==config and proof['tests']==proof['passed']==validation.EXPECTED and proof['stableSourcesAndBinaries'] and not proof['earlierPhasesRevalidated'] and not proof['hostInputExecution'] and proof['inputs']==frozen.inputs() and proof['binaries']==validation.binaries(build,config)):raise ValueError('Stale or incomplete evidence: '+config)
        proofs[config]=proof
    services=json.loads((a.services/'validation.json').read_bytes())
    if not(services['phase']=='14.13A' and services['status']=='PASS' and services['tests']==services['passed']==11 and services['sourceStable'] and services['serviceSha256']==frozen.sha(build/'Release/guardian-engine-service.exe')):raise ValueError('Installed service evidence rejected')
    out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,digest in proofs[config]['binaries'].items():
            copy(build/name,name)
            if frozen.sha(out/name)!=digest:raise ValueError('Binary changed during packaging')
        for name in ('validation.json','results.json','parser.log','protocol.log'):copy(folder/name,'validation/'+config+'/'+name)
    for name in ('validation.json','results.json'):copy(a.services/name,'validation/services/'+name)
    for name in frozen.inputs():copy(validation.ROOT/name,name)
    for name in ('README.md','SECURITY.md','signatures.txt'):copy(validation.ROOT/name,name)
    for file in (validation.ROOT/'docs').glob('*.md'):copy(file,'docs/'+file.name)
    (out/'README.txt').write_text('Aegis 14.13A - Automatic packages and integration of servicos.\n'
        'Read docs/PHASE-14.13A.md. Prepare the local catalog with tools/provision_runtime.py.\n'
        'This package does not include Microsoft DLLs, external program files, local catalogs or snapshots of memoria.\n'
        'Do not execute guest EXes on the host. The guest directory programs are data for MiniNT.\n'
        'Validation restricted to this delivery; this does not certify ampla.\n compatibility',encoding='utf-8')
    manifest={'schema':1,'phase':'14.13A','scope':'automatic-packages-current-runtime-integration',
              'tests':{c:r['tests'] for c,r in proofs.items()},'installedServiceTests':services['tests'],
              'earlierPhasesRevalidated':False,'microsoftInputsIncluded':False,
              'externalSourceOrBinaryIncluded':False,'memorySnapshotsIncluded':False,
              'files':{f.relative_to(out).as_posix():frozen.sha(f) for f in sorted(out.rglob('*')) if f.is_file()}}
    (out/'manifest.json').write_bytes(json.dumps(manifest,indent=2).encode()+b'\n')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(out).as_posix())
    print(json.dumps({'phase':'14.13A','zip':str(archive),'sha256':frozen.sha(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
