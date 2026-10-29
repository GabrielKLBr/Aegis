"""Package the initial x86 core only after current Release/Debug acceptance."""
from pathlib import Path
import argparse,json,shutil,zipfile
import validate_compat32 as validation
import validate_frameworks as snapshots
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','release','debug','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();archive=out.with_name(out.name+'.zip');accepted={}
    if out.exists() or archive.exists():raise ValueError('Fresh package output required')
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes())
        if not (proof['phase']=='14.12' and proof['scope']=='initial-x86-core' and proof['phaseComplete'] is False and proof['status']=='PASS' and proof['configuration']==config and proof['tests']==proof['passed']==validation.EXPECTED and proof['stableSourcesAndBinaries'] and proof['earlierPhasesRevalidated'] is False and proof['hostInputExecution'] is False and proof['inputs']==snapshots.inputs() and proof['binaries']==validation.binaries(build,config)):
            raise ValueError('Stale or incomplete x86 evidence: '+config)
        accepted[config]=proof
    out.mkdir(parents=True)
    def copy(file,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,digest in accepted[config]['binaries'].items():
            copy(build/name,name)
            if snapshots.sha(out/name)!=digest:raise ValueError('Binary changed: '+name)
        for name in ('results.json','validation.json','parser.log'):copy(folder/name,'validation/'+config+'/'+name)
        for file in folder.glob('*/run/*.json'):
            if file.name in ('boot.json','analysis.json','native-user32.json'):copy(file,'validation/'+config+'/'+file.parent.parent.name+'/'+file.name)
    for file in (validation.ROOT/'tools').glob('*.py'):copy(file,'tools/'+file.name)
    copy(validation.ROOT/'CMakeLists.txt','CMakeLists.txt')
    for file in (validation.ROOT/'config').rglob('*'):
        if file.is_file():copy(file,'config/'+file.relative_to(validation.ROOT/'config').as_posix())
    for name in ('NativeUserIntegrationTests.py','Compat32LoaderTests.cpp'):copy(validation.ROOT/'test'/name,'test/'+name)
    for name in ('Compat32Acceptance.cpp','Compat32ConsoleEntry.cpp','Compat32Kernel.cpp','Compat32Crt.cpp','Compat32Gate.asm','Compat32User.inl'):copy(validation.ROOT/'guest/fixtures'/name,'guest/fixtures/'+name)
    copy(validation.ROOT/'docs/PHASE-14.12.md','docs/PHASE-14.12.md')
    (out/'LEIA-ME.txt').write_text(
        'Aegis 14.12A: nucleo PE32/x86 para NT 10.0.19045. A fase 14.12 ampla permanece aberta.\n'
        'Requisitos: Windows x64 com WHP/NX ativos, Python, MSVC x86/x64, SDK e runtime ASan local.\n'
        'Os EXEs em guest/compat32 sao entradas SOMENTE para o MiniNT, nunca para execucao no host.\n'
        'Leia docs/PHASE-14.12.md. APIs x86 limitadas, sem WOW64 completo.\n'
        'Reproducao: tools/validate_compat32.py --build ESTA_PASTA --config Release ou Debug '
        '--output PASTA_NOVA --corpus CACHE_EXTERNO --asan-runtime PASTA_ASAN_LOCAL.\n'
        'O corpus externo e compilado localmente e roda apenas no MiniNT. Fonte/binario externos e DLLs Microsoft nao sao redistribuidos.\n',encoding='utf-8')
    manifest={'schema':1,'phase':'14.12','subphase':'14.12A','scope':'initial-x86-core','phaseComplete':False,'targetNt':'10.0.19045','architectures':{'host':'x64','kernel':'x64','application':'x86'},'configurations':['Release','Debug'],'tests':{c:p['tests'] for c,p in accepted.items()},'earlierPhasesRevalidated':False,'microsoftInputsIncluded':False,'externalSourceOrBinaryIncluded':False,'files':{file.relative_to(out).as_posix():snapshots.sha(file) for file in sorted(out.rglob('*')) if file.is_file()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(out).as_posix())
    print(json.dumps({'phase':'14.12','subphase':'14.12A','package':str(out),'zip':str(archive),'sha256':snapshots.sha(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
