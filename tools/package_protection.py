"""Package only phase 16 owned binaries, source and scoped acceptance evidence."""
from pathlib import Path
import argparse,json,shutil,zipfile
import validate_frameworks as frozen
from validate_protection import binaries

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','release','debug','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();archive=out.with_suffix('.zip');assert not out.exists() and not archive.exists();root=Path(__file__).resolve().parents[1];proofs={}
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        q=json.loads((folder/'validation.json').read_bytes());assert q['phase']=='16' and q['status']=='PASS' and q['tests']==q['passed']>=50 and q['nativeTests']==25 and q['sourceStable'] and q['inputs']==frozen.inputs() and q['binaries']==binaries(build,config);proofs[config]=q
    out.mkdir(parents=True)
    def copy(source,name):
        destination=out/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,digest in proofs[config]['binaries'].items():copy(build/name,name);assert frozen.sha(out/name)==digest
        copy(root/'signatures.txt',config+'/signatures.txt')
        for name in ('validation.json','results.json','native.json'):copy(folder/name,'validation/'+config+'/'+name)
    for name in frozen.inputs():copy(root/name,name)
    for name in ('README.md','SECURITY.md'):copy(root/name,name)
    for file in (root/'docs').iterdir():
        if file.is_file() and file.suffix in ('.md','.json'):copy(file,'docs/'+file.name)
    (out/'LEIA-ME.txt').write_text('Aegis fase 16 - servico Windows, protecao apos alteracoes e cofre protegido.\n'
        'Leia docs/PHASE-16.md. Politicas, bancos, chaves, arquivos pessoais e referencias Microsoft nao estao incluidos.\n'
        'O servico deve ser instalado com autorizacao administrativa; a ferramenta nao burla a elevacao.\n'
        'Ativar pastas, pausar/desligar a protecao e restaurar/excluir do cofre exigem administrador.\n'
        'Nao ha minifilter nem bloqueio preventivo de execucao. A eficacia das regras nao foi certificada.\n',encoding='utf-8')
    manifest={'schema':1,'phase':'16','tests':{c:p['tests'] for c,p in proofs.items()},'nativeTestsPerConfiguration':25,'earlierPhasesRevalidated':False,'runtimeReferencesIncluded':False,'machineStateOrKeysIncluded':False,'files':{p.relative_to(out).as_posix():frozen.sha(p) for p in sorted(out.rglob('*')) if p.is_file()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(out).as_posix())
    print(json.dumps({'phase':'16','zip':str(archive),'sha256':frozen.sha(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
