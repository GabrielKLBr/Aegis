"""Package the owned phase 15 binaries and current, scoped acceptance evidence."""
from pathlib import Path
import argparse,json,shutil,zipfile
import validate_quarantine as validation,validate_frameworks as frozen

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','release','debug','services','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();archive=out.with_suffix('.zip');assert not out.exists() and not archive.exists();proofs={}
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes());assert proof['phase']=='15' and proof['status']=='PASS' and proof['tests']==proof['passed']>=57 and proof['stableSourcesAndBinaries'] and proof['inputs']==frozen.inputs() and proof['binaries']==validation.binaries(a.build.resolve(),config);proofs[config]=proof
    services=json.loads((a.services/'validation.json').read_bytes());assert services['status']=='PASS' and services['tests']==services['passed']==13 and services['serviceSha256']==frozen.sha(a.build/'Release/guardian-engine-service.exe') and services['inputs']==frozen.inputs() and services['binaries']==validation.binaries(a.build.resolve(),'Release')
    out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,digest in proofs[config]['binaries'].items():copy(a.build/name,name);assert frozen.sha(out/name)==digest
        for name in ('validation.json','results.json'):copy(folder/name,'validation/'+config+'/'+name)
        # Service distribution dependencies, not revalidated as earlier phase suites.
        for name in ('guardian-scan-worker.exe','guardian-whp-memory.exe','guardian-service-sample.exe','guardian-runtime.exe','guardian-desktop.exe','guardian-native-user-host.exe','guardian-audio-host.exe','guardian-kernel-audio.gk'):
            copy(a.build/config/name,config+'/'+name)
        copy(validation.ROOT/'signatures.txt',config+'/signatures.txt')
    for name in ('validation.json','results.json'):copy(a.services/name,'validation/services/'+name)
    for name in frozen.inputs():copy(validation.ROOT/name,name)
    for name in ('README.md','SECURITY.md'):copy(validation.ROOT/name,name)
    for file in (validation.ROOT/'docs').iterdir():
        if file.is_file() and file.suffix in ('.md','.json'):copy(file,'docs/'+file.name)
    (out/'LEIA-ME.txt').write_text('Aegis fase 15 - quarentena transacional por usuario.\nLeia docs/PHASE-15.md.\n'
        'Chaves, bancos de quarentena e arquivos do usuario nao estao incluidos.\n'
        'O driver de teste tem injecao de falhas; o guardian-quarantine.exe de producao nao.\n'
        'A restauracao e explicita e nunca sobrescreve. Exclusao nao garante apagamento fisico em SSD/backups.\n'
        'Referencias Microsoft e catalogos locais de sandbox nao incluidos.\n',encoding='utf-8')
    manifest={'schema':1,'phase':'15','scope':'transactional-user-quarantine','tests':{c:r['tests'] for c,r in proofs.items()},'serviceTests':13,'quarantineKeysOrDataIncluded':False,'earlierPhasesRevalidated':False,'files':{f.relative_to(out).as_posix():frozen.sha(f) for f in sorted(out.rglob('*')) if f.is_file()}};(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for f in sorted(out.rglob('*')):
            if f.is_file():z.write(f,f.relative_to(out).as_posix())
    print(json.dumps({'phase':'15','zip':str(archive),'sha256':frozen.sha(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
