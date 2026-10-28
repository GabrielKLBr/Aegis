"""Verify package/ZIP bytes and run only extracted phase 14.10G tests."""
from pathlib import Path
import argparse,json,subprocess,sys,zipfile,hashlib
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser()
    for key in ('package','reference','schema-reference','corpus-binaries','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.package=a.package.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);manifest=json.loads((a.package/'manifest.json').read_bytes());assert manifest['phase']=='14.10G' and not manifest['microsoftInputsIncluded'] and not manifest['independentCorpusIncluded']
    expected=set(manifest['files'])|{'manifest.json'}
    def verify():
        assert {path.relative_to(a.package).as_posix() for path in a.package.rglob('*') if path.is_file()}==expected
        for name,digest in manifest['files'].items():assert sha(a.package/name)==digest,name
    verify()
    with zipfile.ZipFile(a.package.with_name(a.package.name+'.zip')) as z:
        assert set(z.namelist())==expected
        for name in expected:assert z.read(name)==(a.package/name).read_bytes(),name
    cases=[]
    for config in ('Release','Debug'):
        commands=[('broker',[str(a.package/config/'audio_broker_tests.exe'),str(a.package/config/'guardian-audio-host.exe')]),('inventory',[sys.executable,'-B',str(a.package/'tools/audio_inventory.py'),'--build',str(a.package),'--config',config])]
        common=['--build',str(a.package),'--config',config,'--reference',str(a.reference.resolve()),'--schema-reference',str(a.schema_reference.resolve())]
        commands += [('integration',[sys.executable,'-B',str(a.package/'test/AudioIntegrationTests.py'),*common,'--evidence',str(out/(config+'-integration')),'--external',str(a.corpus_binaries.resolve()/config/'whisky-audio-test.exe')]),('tools',[sys.executable,'-B',str(a.package/'test/AudioRuntimeToolsTests.py'),*common])]
        for name,command in commands:
            run=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(out/(config+'-'+name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');assert not run.returncode,run.stdout+run.stderr;cases.append(config+'-'+name)
    verify();result={'phase':'14.10G','status':'PASS','configurations':['Release','Debug'],'tests':cases,'files':len(expected),'zipVerified':True,'unchangedAfterExecution':True,'microsoftInputsIncluded':False,'independentCorpusIncluded':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
