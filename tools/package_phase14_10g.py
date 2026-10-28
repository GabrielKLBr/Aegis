"""Package only owned PCM runtime binaries and accepted Windows evidence."""
from pathlib import Path
import argparse,json,shutil,zipfile
from validate_phase14_10g import ROOT,sha,inputs,AUDIO
from component_runtime import DLLS as COMPONENT_DLLS
from internet_runtime import DLLS as INTERNET_DLLS
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','release','debug','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();proofs={};current=inputs()
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes());assert proof['phase']=='14.10G' and proof['status']=='PASS' and proof['configuration']==config and proof['stableSourcesAndBinaries'] and not proof['failures'] and not proof['skipped'] and proof['inputs']==current
        assert proof['suite']=='phase' and proof['tests']==len(AUDIO) and set(proof['testNames'])==AUDIO and proof['earlierPhasesRevalidated'] is False
        for name,digest in proof['binaries'].items():assert sha(a.build/name)==digest,name
        proofs[config]={k:proof[k] for k in ('tests','suite','stableSourcesAndBinaries')}
    out=a.output;archive=out.with_name(out.name+'.zip');assert not out.exists() and not archive.exists();out.mkdir(parents=True)
    def copy(source,name):
        destination=out/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-desktop','guardian-audio-host','guardian-whp-memory','audio_broker_tests'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-audio.gk','guest/'+config+'/guardian-kernel-audio.gk')
        for path in (a.build/'guest/audio'/config).iterdir():
            if path.suffix in ('.dll','.exe'):copy(path,'guest/audio/'+config+'/'+path.name)
        for name in ('ntdll.dll','kernel32.dll','kernelbase.dll','user32.dll','gdi32.dll','gdi32full.dll','win32u.dll','dwmapi.dll'):copy(a.build/'guest/desktop'/config/name,'guest/desktop/'+config+'/'+name)
        copy(a.build/'guest/crt'/config/'guardian-runtime-api.dll','guest/crt/'+config+'/guardian-runtime-api.dll')
        copy(a.build/'guest/security'/config/'advapi32.dll','guest/security/'+config+'/advapi32.dll')
        for group,dlls in (('components',COMPONENT_DLLS),('internet',INTERNET_DLLS)):
            for name in dlls:copy(a.build/'guest'/group/config/(name+'.dll'),'guest/'+group+'/'+config+'/'+name+'.dll')
        for path in folder.iterdir():
            if path.is_file():copy(path,'validation/'+config+'/'+path.name)
    for path in (ROOT/'tools').glob('*.py'):copy(path,'tools/'+path.name)
    for path in (ROOT/'config').rglob('*'):
        if path.is_file():copy(path,'config/'+path.relative_to(ROOT/'config').as_posix())
    for name in ('AudioIntegrationTests.py','AudioRuntimeToolsTests.py'):copy(ROOT/'test'/name,'test/'+name)
    for name in ('PHASE-14.10G.md','PHASE-14.10F.md','HOST-API-PROGRESS.md'):copy(ROOT/'docs'/name,'docs/'+name)
    (out/'LEIA-ME.txt').write_text('Aegis 14.10G: PCM/WAV WinMM e WASAPI compartilhado.\nLeia docs/PHASE-14.10G.md.\nSem captura, video ou Media Foundation. Referencias Microsoft e corpus externo nao incluidos.\nValidacao limitada a fase 14.10G e ao pacote, sem regressao completa das fases anteriores.\n',encoding='utf-8')
    manifest={'schema':1,'phase':'14.10G','configurations':['Release','Debug'],'validation':proofs,'microsoftInputsIncluded':False,'independentCorpusIncluded':False,'sourceDigests':current,'files':{p.relative_to(out).as_posix():sha(p) for p in sorted(out.rglob('*')) if p.is_file()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out).as_posix())
    print(json.dumps({'phase':'14.10G','package':str(out),'zip':str(archive),'files':len(manifest['files'])+1,'sha256':sha(archive)}))
if __name__=='__main__':main()
