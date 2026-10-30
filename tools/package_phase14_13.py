"""Package the 14.13 binaries and current, targeted Release/Debug evidence."""
from pathlib import Path
import argparse,json,shutil,zipfile
import validate_unified as validation,validate_frameworks as snapshots
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','release','debug','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();archive=out.with_name(out.name+'.zip');accepted={}
    if out.exists() or archive.exists():raise ValueError('Fresh package output required')
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes())
        if not(proof['phase']=='14.13' and proof['status']=='PASS' and proof['configuration']==config and proof['tests']==proof['passed']==validation.EXPECTED and proof['stableSourcesAndBinaries'] and proof['earlierPhasesRevalidated'] is False and proof['hostInputExecution'] is False and proof['inputs']==snapshots.inputs() and proof['binaries']==validation.binaries(build,config)):raise ValueError('Stale/incomplete phase evidence: '+config)
        accepted[config]=proof
    out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,digest in accepted[config]['binaries'].items():
            copy(build/name,name)
            if snapshots.sha(out/name)!=digest:raise ValueError('Binary changed: '+name)
        for name in ('validation.json','results.json','parser.log'):copy(folder/name,'validation/'+config+'/'+name)
        for name in ('checkpoint-gui32','restore-gui32','checkpoint-gui64','restore-gui64'):
            for file in (folder/name).glob('*.json'):
                if file.name in ('boot.json','runtime.json','analysis.json','native-user32.json'):copy(file,'validation/'+config+'/'+name+'/'+file.name)
    copy(validation.ROOT/'CMakeLists.txt','CMakeLists.txt')
    for file in (validation.ROOT/'tools').glob('*.py'):copy(file,'tools/'+file.name)
    for file in (validation.ROOT/'config').rglob('*'):
        if file.is_file():copy(file,'config/'+file.relative_to(validation.ROOT/'config').as_posix())
    for name in ('NativeUserIntegrationTests.py','NativeCheckpointTests.cpp'):copy(validation.ROOT/'test'/name,'test/'+name)
    for name in ('Compat32ConsoleEntry.cpp','UnifiedProbe.cpp'):copy(validation.ROOT/'guest/fixtures'/name,'guest/fixtures/'+name)
    copy(validation.ROOT/'docs/PHASE-14.13.md','docs/PHASE-14.13.md')
    (out/'LEIA-ME.txt').write_text('Aegis 14.13: scanner, pacotes por SHA-256 e recuperacao USER32 basica.\n'
        'Windows x64/WHP, Python, MSVC/SDK e runtime ASan local necessarios.\n'
        'EXEs convidados nao devem ser executados no host. Leia docs/PHASE-14.13.md.\n'
        'Reproduza tools/validate_unified.py --build ESTA_PASTA --config Release ou Debug '
        '--reference CRT_LOCAL --schema-reference SCHEMA_LOCAL --corpus CACHE_LOCAL '
        '--asan-runtime ASAN_LOCAL --output PASTA_NOVA.\n'
        'Referencias Microsoft, corpus externo, registros provisionados e snapshots de memoria nao incluidos.\n'
        'Snapshots GDI/DirectX/audio, conexoes abertas e USER32 com timers/sends pendentes sao recusados.\n',encoding='utf-8')
    manifest={'schema':1,'phase':'14.13','scope':'unified-handoff-and-basic-user32-recovery','tests':{c:r['tests'] for c,r in accepted.items()},'earlierPhasesRevalidated':False,'microsoftInputsIncluded':False,'externalSourceOrBinaryIncluded':False,'memorySnapshotsIncluded':False,'liveGdiAudioSnapshots':False,'files':{f.relative_to(out).as_posix():snapshots.sha(f) for f in sorted(out.rglob('*')) if f.is_file()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(out).as_posix())
    print(json.dumps({'phase':'14.13','package':str(out),'zip':str(archive),'sha256':snapshots.sha(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
