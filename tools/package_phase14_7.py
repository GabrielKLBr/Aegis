"""Package the owned desktop runtime and stable phase 14.7 evidence only."""
from pathlib import Path
import argparse,json,shutil,zipfile
import nt_compatibility as nt
from validate_phase14_7 import REQUIRED,TOTAL_TESTS
APPS=('demo','threads','settings','nativeprobe')
DLLS=('ntdll.dll','kernelbase.dll','kernel32.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--debug',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    for config,suite,folder in (('Release','acceptance',a.release),('Debug','acceptance',a.debug),('Release','regression',a.regression)):
        proof=json.loads((folder/'validation.json').read_bytes());covers=suite=='acceptance' and config=='Release' and proof['suite']=='regression';expected=TOTAL_TESTS if proof['suite']=='regression' else len(REQUIRED)
        nt.require(proof['phase']=='14.7' and proof['status']=='PASS' and (proof['suite']==suite or covers) and proof['configuration']==config and proof['tests']==expected and not proof['failures'] and not proof['skipped'] and proof['stableSourcesAndBinaries'],'Stable accepted evidence required')
        if suite=='acceptance':nt.require(REQUIRED<=set(proof['testNames']) if covers else REQUIRED==set(proof['testNames']),'Acceptance identities changed')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        for name,digest in proof['flappyInputs'].items():nt.require(nt.sha(Path(name).read_bytes())==digest,'External input changed')
    out=a.output;archive=out.with_name(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'New package path required');out.mkdir(parents=True)
    def copy(source,name):
        dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-desktop','guardian-whp-memory','desktop_core_tests','desktop_viewer_tests','desktop_windows_demo','desktop_windows_threads'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-desktop.gk','guest/'+config+'/guardian-kernel-desktop.gk')
        copy(a.build/'guest/crt'/config/'guardian-runtime-api.dll','guest/crt/'+config+'/guardian-runtime-api.dll')
        for name in (*DLLS,*(('guardian-desktop-'+name+'.exe') for name in APPS)):copy(a.build/'guest/desktop'/config/name,'guest/desktop/'+config+'/'+name)
        for name in ('validation.json','ctest.log','ctest.xml'):copy(evidence/name,'validation/'+config+'/'+name)
    for name in ('validation.json','ctest.log','ctest.xml'):copy(a.regression/name,'validation/regression-Release/'+name)
    copy(a.build/'desktop-generated/coverage.json','validation/desktop-export-coverage.json');copy(nt.ROOT/'config/crt-references.json','config/crt-references.json')
    for name in ('desktop_runtime.py','crt_runtime.py','win32_runtime.py','native_nt.py','api_set.py','nt_compatibility.py','inspect_gui_compatibility.py','pe_volume.py','smoke_phase14_7.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    for name in ('DesktopIntegrationTests.py','DesktopFlappyTests.py'):copy(nt.ROOT/'test'/name,'test/'+name)
    copy(nt.ROOT/'docs/PHASE-14.7.md','PHASE-14.7.md')
    (out/'LEIA-ME.txt').write_text('''Aegis 14.7 - desktop do MiniNT x64

Requer Windows x64, WHP e NX ativos e o Visual C++ runtime no host.
Contem o kernel e as DLLs proprias, aplicativos de teste e evidencias.
As janelas do programa permanecem no MiniNT, mostradas em um visualizador.
O parametro --interactive habilita entrada somente nessa janela do host.
A integracao de uma janela nativa por janela convidada pertence a fase 12F.

Nao inclui DLLs Microsoft de entrada, Flappy, ISOs, discos pessoais ou GPVs.
Forneca suas referencias locais exatas (config/crt-references.json), o schema
Win10 aceito e o Flappy original acompanhado de settings.txt e icon.ico.

Em uma pasta de saida nova, no PowerShell:
python -B tools/desktop_runtime.py bundle --build . --config Release --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --output app
.\\Release\\guardian-desktop.exe guest/Release/guardian-kernel-desktop.gk app/runtime.gpv app/manifest.json app/binding.gnt sessao --interactive

O bundle padrao abre o aplicativo proprio de demonstracao. Para o Flappy,
adicione --application CAMINHO-FLAPPY --asset CAMINHO-SETTINGS --asset CAMINHO-ICON
na criacao do bundle. O executavel externo nunca roda diretamente no host.

Para verificar o pacote nas duas configuracoes:
python -B tools/smoke_phase14_7.py --package . --reference PASTA-CRT --schema-reference SCHEMA-LEGAL --application CAMINHO-FLAPPY --output EVIDENCIA-NOVA

security-state.bin guarda 64 KiB do disco convidado e pode ser fornecido a uma
nova sessao com --state ARQUIVO. Nao altere nem publique dados da sua sessao.
Leia PHASE-14.7.md para cotas, suporte e limitacoes.
''',encoding='utf-8')
    files={path.relative_to(out).as_posix():nt.sha(path.read_bytes()) for path in sorted(out.rglob('*')) if path.is_file()};(out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.7','files':files,'acceptance':{'Release':len(REQUIRED),'Debug':len(REQUIRED)},'regressionTests':TOTAL_TESTS,'microsoftInputBinariesIncluded':False,'externalApplicationIncluded':False,'completeWindowsGui':False,'phase12FStarted':False,'phase14_8Started':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
if __name__=='__main__':main()
