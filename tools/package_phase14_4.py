"""Package the accepted owned Win32 runtime without Microsoft binary inputs."""
from pathlib import Path
import argparse,json,shutil,zipfile
import xml.etree.ElementTree as ET
import win32_runtime as w
import nt_compatibility as nt
import pe_volume
from validate_phase14_4 import REQUIRED

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--debug',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    proofs={}
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((evidence/'validation.json').read_bytes());nt.require(proof['phase']=='14.4' and proof['status']=='PASS' and proof['configuration']==config and proof['tests']==len(REQUIRED) and not proof['skipped'] and not proof['failures'] and proof['stableSourcesAndBinaries'],'Accepted evidence required')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        nt.require(nt.sha(nt.regular(a.schema_reference,8<<20))==proof['apiSetReference']['sha256'],'Schema reference changed');proofs[config]=proof
    cases=ET.parse(a.regression).getroot().findall('.//testcase');nt.require(len(cases)==100 and all(c.find('failure') is None and c.find('error') is None and c.find('skipped') is None for c in cases),'Complete 100-test regression required')
    out=a.output;archive=out.with_name(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'Use a new package path');out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-pe','guardian-whp-memory','win32_apiset_tests','win32_native_core_tests','win32_windows_reference_tests'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-win32.gk','guest/'+config+'/guardian-kernel-win32.gk')
        copy(a.build/'guest/native'/config/'ntdll.dll','guest/native/'+config+'/ntdll.dll')
        copy(a.build/'guest/external'/config/'guardian-runtime-api.dll','guest/external/'+config+'/guardian-runtime-api.dll')
        for name in ('guardian-win32-child.exe','guardian-win32-plugin.dll'):copy(a.build/'guest/win32'/config/name,'guest/win32/'+config+'/'+name)
        for profile in nt.PROFILES:
            for name in ('kernel32.dll','kernelbase.dll','guardian-win32.exe'):copy(a.build/'guest/win32'/config/profile/name,'guest/win32/'+config+'/'+profile+'/'+name)
            bundle=out/config/'profiles'/profile;w.bundle(out,config,bundle,profile,a.schema_reference);pe_volume.verify(bundle)
        for name in ('validation.json','ctest.xml','ctest.log'):copy(evidence/name,'validation/'+config+'/'+name)
    copy(a.regression,'validation/regression-Release.xml')
    for name in ('win32_runtime.py','api_set.py','native_nt.py','pe_volume.py','nt_compatibility.py','inspect_gui_compatibility.py','smoke_phase14_4.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    copy(nt.ROOT/'docs/PHASE-14.4.md','PHASE-14.4.md')
    (out/'LEIA-ME.txt').write_text('''Aegis fase 14.4 - KERNEL32/KERNELBASE proprias no MiniNT x64

Build de desenvolvimento Windows x64. Requer WHP, NX e runtime C++ VS 2022.
Compatibilidade limitada a matriz descrita em PHASE-14.4.md; CRT/GUI gerais pendentes.
As DLLs executaveis deste pacote sao do projeto; nao inclui binarios Microsoft.
O schema Win10 usa dados re-encodificados da referencia legal; Win7/11 usam fixtures.

No PowerShell, nesta pasta, use uma pasta de sessao que ainda nao existe:
python tools/pe_volume.py verify Release/profiles/win10
.\\Release\\guardian-pe.exe guest/Release/guardian-kernel-win32.gk Release/profiles/win10/runtime.gpv Release/profiles/win10/manifest.json Release/profiles/win10/binding.gnt sessao-nova

Troque win10 por win7 ou win11 em todos os argumentos para outro perfil proprio.
Troque Release por Debug para depuracao. boot.json e analysis.json registram resultados.
Arquivos mutaveis e ambiente pertencem ao convidado; o volume de arquivos e efemero.
Leia os limites de arquivos, I/O, Unicode, processos, thread pool e API sets no documento.
Esta entrega nao inicia a 14.5.
''',encoding='utf-8')
    files={p.relative_to(out).as_posix():nt.sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.4','files':files,'acceptance':{c:p['tests'] for c,p in proofs.items()},'regressionTests':len(cases),'windowsBinaryCompatibility':False,'phase14_5Started':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}));return 0
if __name__=='__main__':raise SystemExit(main())
