"""Package accepted 14.3 owned binaries and evidence, excluding Microsoft inputs."""
from pathlib import Path
import argparse
import json
import shutil
import zipfile
import native_nt
import nt_compatibility as nt
import pe_volume
from validate_phase14_3 import REQUIRED


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--release',type=Path,required=True)
    p.add_argument('--debug',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();proofs={}
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((evidence/'validation.json').read_bytes())
        nt.require(proof['phase']=='14.3' and proof['status']=='PASS' and proof['configuration']==config
                   and proof['tests']==len(REQUIRED) and proof['failures']==proof['skipped']==0
                   and proof['stableSourcesAndBinaries'],'Accepted 14.3 evidence required')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        proofs[config]=proof
    out=a.output;archive=out.parent/(out.name+'.zip')
    nt.require(not out.exists() and not archive.exists(),'Use a new package path');out.mkdir(parents=True)
    def copy(source,target):
        destination=out/target;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-pe','guardian-whp-memory','native_nt_core_tests','native_nt_windows_reference_tests','native_nt_unwind_tests'):
            copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-native-nt.gk','guest/'+config+'/guardian-kernel-native-nt.gk')
        for name in ('ntdll.dll','guardian-native-owned.exe','guardian-native-original.exe'):
            copy(a.build/'guest/native'/config/name,'guest/native/'+config+'/'+name)
        copy(a.build/'guest/external'/config/'guardian-runtime-api.dll','guest/external/'+config+'/guardian-runtime-api.dll')
        for name in ('validation.json','ctest.xml','ctest.log'):copy(evidence/name,'validation/'+config+'/'+name)
        for profile in nt.PROFILES:
            bundle=out/config/'profiles'/profile;native_nt.bundle(out,config,bundle,profile);pe_volume.verify(bundle)
    for name in ('native_nt.py','pe_volume.py','nt_compatibility.py','inspect_gui_compatibility.py','smoke_phase14_3.py'):
        copy(nt.ROOT/'tools'/name,'tools/'+name)
    copy(nt.ROOT/'docs/PHASE-14.3.md','PHASE-14.3.md')
    (out/'LEIA-ME.txt').write_text('''Aegis fase 14.3 - servicos nativos MiniNT x64

Build de desenvolvimento Windows x64. Requer WHP, NX e runtime C++ VS 2022.
Compatibilidade limitada a matriz de referencia descrita em PHASE-14.3.md.
A NTDLL neste pacote foi escrita pelo projeto. Nao inclui DLLs da Microsoft.

No PowerShell, nesta pasta, use uma pasta de sessao que ainda nao existe:
python tools/pe_volume.py verify Release/profiles/win10
.\\Release\\guardian-pe.exe guest/Release/guardian-kernel-native-nt.gk Release/profiles/win10/runtime.gpv Release/profiles/win10/manifest.json Release/profiles/win10/binding.gnt sessao-nova

Troque win10 por win7 ou win11 em todos os argumentos para outro perfil proprio.
Troque Release por Debug para a compilacao de depuracao.
boot.json e analysis.json registram execucao, chamadas e resultados.
O ensaio com NTDLL original requer uma copia legal local adicional, conforme o documento.
Arquivos/registro criados pela matriz ficam em RAM no convidado e nao persistem.
Esta entrega nao inicia a 14.4 nem declara que o Flappy ja funciona.
''',encoding='utf-8')
    files={p.relative_to(out).as_posix():nt.sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.3','files':files,
        'acceptance':{c:p['tests'] for c,p in proofs.items()},'windowsBinaryCompatibility':False,'phase14_4Started':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii')
    print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}));return 0


if __name__=='__main__':raise SystemExit(main())
