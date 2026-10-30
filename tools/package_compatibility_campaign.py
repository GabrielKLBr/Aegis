"""Archive owned phase 14.14 controllers, source and measurement evidence only."""
from pathlib import Path
import argparse,json,shutil,zipfile
import compatibility_campaign as c

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','release','debug','tests','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();archive=out.with_suffix('.zip');c.require(not out.exists() and not archive.exists(),'Fresh package required')
    unit=json.loads(a.tests.read_bytes());c.require(unit['status']=='PASS' and unit['tests']==unit['passed']>=23,'Campaign tests incomplete')
    proofs={}
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((folder/'validation.json').read_bytes());rows=json.loads((folder/'rows.json').read_bytes());plan=json.loads((c.ROOT/'config/nt-compatibility/phase14.14-corpus.json').read_bytes())
        c.require(proof['status']=='MEASUREMENT_COMPLETE' and proof['configuration']==config and proof['cells']==96 and proof['rowsSha256']==c.identity(rows) and proof['summary']==c.summarize(plan,rows) and proof['inputs']==c.sources.inputs() and proof['binaries']==c.runtime_build.binaries(a.build.resolve(),config),'Stale/incomplete measurement evidence')
        proofs[config]=proof
    out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for config,folder in (('Release',a.release),('Debug',a.debug)):
        for name,sha in proofs[config]['binaries'].items():copy(a.build/name,name);c.require(c.digest(out/name)==sha,'Package binary changed')
        for name in ('validation.json','rows.json'):copy(folder/name,'validation/'+config+'/'+name)
        for report in folder.glob('win10-*/runtime.json'):copy(report,'validation/'+config+'/runtime/'+report.parent.name+'.json')
    for name in c.sources.inputs():copy(c.ROOT/name,name)
    for name in ('README.md','SECURITY.md','signatures.txt'):copy(c.ROOT/name,name)
    for file in (c.ROOT/'docs').glob('*'):
        if file.is_file() and file.suffix in ('.md','.json'):copy(file,'docs/'+file.name)
    copy(a.tests,'validation/campaign-tests.json')
    (out/'LEIA-ME.txt').write_text('Aegis 14.14 - medicao de compatibilidade, sem aceitacao ampla.\n'
        'Leia docs/PHASE-14.14.md. Os EXEs independentes e as DLLs Microsoft nao estao incluidos.\n'
        'Reproduzir a campanha exige os mesmos arquivos locais por SHA-256 e as referencias legais.\n'
        'EXEs em guest sao dados para o MiniNT; nao os execute no Windows.\n',encoding='utf-8')
    manifest={'schema':1,'phase':'14.14','assessmentComplete':True,'broadCompatibilityAccepted':False,'thirdPartyExecutablesIncluded':False,'microsoftInputsIncluded':False,'snapshotsIncluded':False,'tests':unit,'cellsPerConfiguration':96,'files':{f.relative_to(out).as_posix():c.digest(f) for f in sorted(out.rglob('*')) if f.is_file()}}
    c.write(out/'manifest.json',manifest)
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(out).as_posix())
    print(json.dumps({'zip':str(archive),'sha256':c.digest(archive),'files':len(manifest['files'])+1}))
if __name__=='__main__':main()
